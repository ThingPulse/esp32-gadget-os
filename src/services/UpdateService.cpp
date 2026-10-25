#include "ui/I18n.h"
#include "services/UpdateService.h"

#include "EventBus.h"
#include "SystemVersion.h"
#include "services/ConfigStoreService.h"
#include "services/WifiService.h"
#include "ui/UiTrace.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#if MBEDTLS_VERSION_MAJOR < 3
// mbedTLS 3 removed the _ret suffix; keep compatibility with older firmware builds.
#define mbedtls_sha256_starts mbedtls_sha256_starts_ret
#define mbedtls_sha256_update mbedtls_sha256_update_ret
#define mbedtls_sha256_finish mbedtls_sha256_finish_ret
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#ifndef GADGET_OS_STABLE_MANIFEST_URL
#define GADGET_OS_STABLE_MANIFEST_URL "https://github.com/ThingPulse/esp32-gadget-os/releases/latest/download/ota-manifest.json"
#endif
#ifndef GADGET_OS_RELEASES_API_URL
#define GADGET_OS_RELEASES_API_URL "https://api.github.com/repos/ThingPulse/esp32-gadget-os/releases?per_page=10"
#endif

namespace {
int compare_version(const std::string &a, const std::string &b)
{
    const char *pa=a.c_str(), *pb=b.c_str();
    if (*pa=='v') ++pa; if (*pb=='v') ++pb;
    for (int part=0; part<3; ++part) {
        int va=0,vb=0; while (std::isdigit((unsigned char)*pa)) va=va*10+(*pa++-'0');
        while (std::isdigit((unsigned char)*pb)) vb=vb*10+(*pb++-'0');
        if (va!=vb) return va<vb ? -1 : 1;
        if (*pa=='.') ++pa; if (*pb=='.') ++pb;
    }
    const bool apre=*pa=='-', bpre=*pb=='-';
    if (apre!=bpre) return apre ? -1 : 1;
    return std::strcmp(pa,pb)<0 ? -1 : std::strcmp(pa,pb)>0 ? 1 : 0;
}

#if defined(ARDUINO) && defined(ESP_PLATFORM)
bool https_get(const std::string &url, std::string &body, int *status_code=nullptr)
{
    UI_TRACE("Update", "TLS heap free=%u largest=%u psram=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setConnectTimeout(7000); http.setTimeout(12000);
    if (!http.begin(client,url.c_str())) return false;
    // HTTP header names are protocol tokens, never translated UI text.
    http.addHeader("Accept","application/json"); http.addHeader("User-Agent","gadget-os/" GADGET_OS_VERSION);
    int code=http.GET();
    if (status_code) *status_code=code;
    if (code!=HTTP_CODE_OK) { http.end(); return false; }
    body=http.getString().c_str(); http.end(); return !body.empty();
}
#endif
}

UpdateService &UpdateService::instance() { static UpdateService instance; return instance; }
const char *UpdateService::current_version() { return GADGET_OS_VERSION; }
bool UpdateService::supported()
{
#if defined(GADGET_OS_OTA_ENABLED) && GADGET_OS_OTA_ENABLED
    return true;
#else
    return false;
#endif
}

bool UpdateService::begin()
{
    load_preferences();
    return true;
}

void UpdateService::load_preferences()
{
    auto &cfg=ConfigStoreService::instance();
    if (!cfg.begin() || !cfg.load("update")) return;
    _channel=cfg.get("channel","stable")=="prerelease" ? Channel::Prerelease : Channel::Stable;
    _ignored_version=cfg.get("ignored_version","");
    const std::string pending=cfg.get("installed_version_pending","");
    if (!pending.empty() && pending==current_version()) _installed_version_notice=pending;
    else if (!pending.empty()) {
        cfg.set("installed_version_pending",std::string());
        cfg.save();
    }
}

void UpdateService::set_channel(Channel value)
{
    _channel=value; _ignored_version.clear();
    auto &cfg=ConfigStoreService::instance(); cfg.load("update");
    cfg.set("channel",value==Channel::Prerelease ? "prerelease" : "stable");
    cfg.set("ignored_version",std::string()); cfg.save();
}

bool UpdateService::check(bool manual)
{
    if (!supported()) { _error=i18n::tr("OTA is not enabled for this build"); _state=State::Error; return false; }
    if (_check_running || _state==State::Checking || _state==State::Downloading ||
        _state==State::ReadyToReboot) return false;
    _check_running=true;
    _manual_check=manual; _error.clear(); _available_version.clear();
    _available_was_ignored=false;
    _progress=0; _state=State::Checking;
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    if (xTaskCreate(check_task,"update_check",10240,this,1,nullptr)!=pdPASS) {
        _error=i18n::tr("Could not start update check"); _state=State::Error;
        _check_result.store(2, std::memory_order_release); return false;
    }
#else
    _error=i18n::tr("Update checks are available on the device build"); _state=State::Error;
    _check_running=false;
#endif
    return true;
}

#if defined(ARDUINO) && defined(ESP_PLATFORM)
void UpdateService::check_task(void *user_data)
{
    auto *self=static_cast<UpdateService*>(user_data);
    if (!self->fetch_manifest() && self->_error.empty()) self->_error=i18n::tr("Could not check for updates");
    if (!self->_error.empty()) self->_state=State::Error;
    else if (self->_available_version.empty()) self->_state=State::Idle;
    else self->_state=State::Available;
    self->_check_result.store(self->_error.empty() ? 1 : 2, std::memory_order_release);
    vTaskDelete(nullptr);
}
#endif

#if defined(ARDUINO) && defined(ESP_PLATFORM)
bool UpdateService::fetch_manifest()
{
    std::string manifest_url=GADGET_OS_STABLE_MANIFEST_URL, payload;
    if (_channel==Channel::Prerelease) {
        int status=0;
        if (!https_get(GADGET_OS_RELEASES_API_URL,payload,&status)) {
            // A repository without any published releases has no update yet.
            if (status==HTTP_CODE_NOT_FOUND) return true;
            return false;
        }
        JsonDocument releases; if (deserializeJson(releases,payload)!=DeserializationError::Ok) return false;
        manifest_url.clear(); std::string selected_version;
        for (JsonObject release : releases.as<JsonArray>()) {
            if (release["draft"]|false) continue;
            for (JsonObject asset : release["assets"].as<JsonArray>()) {
                if (std::string(asset["name"]|"")=="ota-manifest.json") {
                    std::string candidate=release["tag_name"]|"";
                    if (selected_version.empty() || compare_version(candidate,selected_version)>0) {
                        selected_version=candidate;
                        manifest_url=asset["browser_download_url"].as<std::string>();
                    }
                    break;
                }
            }
        }
        if (manifest_url.empty()) { _error=i18n::tr("No prerelease update feed found"); return false; }
    }
    payload.clear();
    int status=0;
    if (!https_get(manifest_url,payload,&status)) {
        // This is normal before the first OTA-enabled release, or when the
        // latest older release predates the OTA manifest.
        if (status==HTTP_CODE_NOT_FOUND) return true;
        return false;
    }
    JsonDocument doc; if (deserializeJson(doc,payload)!=DeserializationError::Ok) { _error=i18n::tr("Invalid update manifest"); return false; }
    std::string version=doc["version"]|"";
    JsonObject target=doc["targets"]["esp32-color-kit"].as<JsonObject>();
    if (version.empty() || target.isNull()) { _error=i18n::tr("Update does not support this device"); return false; }
    if (compare_version(version,current_version())<=0) {
        _available_version.clear(); return true;
    }
    if (version==_ignored_version) {
        if (!_manual_check) { _available_version.clear(); return true; }
        _available_was_ignored=true;
    }
    _firmware_url=target["url"]|""; _sha256=target["sha256"]|"";
    _firmware_size=target["size"]|0; _release_notes=doc["release_notes"]|"";
    if (_firmware_url.empty() || _sha256.size()!=64 || !_firmware_size) { _error=i18n::tr("Incomplete update manifest"); return false; }
    _available_version=version; return true;
}
#endif

#if !defined(ARDUINO)
namespace {
bool ota_tutorial_enabled() {
    return std::getenv("GADGET_OS_TUTORIAL_PATH") && std::getenv("GADGET_OS_OTA_FIXTURE");
}
}
bool UpdateService::tutorial_available() {
    if (!ota_tutorial_enabled() || _state != State::Idle) return false;
    _available_version = "1.0.0-demo";
    _release_notes = i18n::tr("A new version is ready to install.");
    _available_was_ignored = false; _error.clear(); _progress = 0;
    _state = State::Available; return true;
}
bool UpdateService::tutorial_progress(int percent) {
    if (!ota_tutorial_enabled() || _state != State::Downloading) return false;
    if (_cancel_requested) { _state = State::Idle; return false; }
    _progress = std::max(0, std::min(100, percent));
    if (_progress == 100) _state = State::ReadyToReboot;
    return true;
}
bool UpdateService::tutorial_installed() {
    if (!ota_tutorial_enabled() || _state != State::ReadyToReboot) return false;
    _installed_version_notice = _available_version; _state = State::Idle; return true;
}
#endif

bool UpdateService::install()
{
    if (_state!=State::Available) return false;
    _cancel_requested=false; _state=State::Downloading; _progress=0; _error.clear();
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    if (xTaskCreate(install_task,"update_install",12288,this,1,nullptr)!=pdPASS) {
        _error=i18n::tr("Could not start installation"); _state=State::Error; return false;
    }
    return true;
#else
#if !defined(ARDUINO)
    if (ota_tutorial_enabled()) return true;
#endif
    _error=i18n::tr("Installation is available on the device build"); _state=State::Error; return false;
#endif
}

bool UpdateService::cancel_install()
{
    if (_state!=State::Downloading) return false;
    _cancel_requested=true;
    return true;
}

#if defined(ARDUINO) && defined(ESP_PLATFORM)
void UpdateService::install_task(void *user_data)
{
    auto *self=static_cast<UpdateService*>(user_data);
    if (self->download_and_install()) { self->_state=State::ReadyToReboot; delay(300); ESP.restart(); }
    else self->_state=State::Error;
    vTaskDelete(nullptr);
}
#endif

#if defined(ARDUINO) && defined(ESP_PLATFORM)
bool UpdateService::download_and_install()
{
    if (_cancel_requested) { _error=i18n::tr("Update cancelled"); return false; }
    const esp_partition_t *next=esp_ota_get_next_update_partition(nullptr);
    if (!next || _firmware_size>next->size) { _error=i18n::tr("Firmware does not fit OTA partition"); return false; }
    WiFiClientSecure client; client.setInsecure(); HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); http.setTimeout(15000);
    if (!http.begin(client,_firmware_url.c_str()) || http.GET()!=HTTP_CODE_OK) { _error=i18n::tr("Firmware download failed"); http.end(); return false; }
    if (!Update.begin(_firmware_size,U_FLASH)) { _error=i18n::tr("Could not open OTA partition"); http.end(); return false; }
    mbedtls_sha256_context sha; mbedtls_sha256_init(&sha); mbedtls_sha256_starts(&sha,0);
    auto *stream=http.getStreamPtr(); uint8_t buffer[2048]; size_t written=0;
    while (written<_firmware_size) {
        if (_cancel_requested) break;
        int available=stream->available();
        if (available<=0) { if (!http.connected()) break; delay(10); continue; }
        size_t want=std::min((size_t)available,std::min(sizeof(buffer),_firmware_size-written));
        int got=stream->readBytes(buffer,want); if (got<=0) break;
        if (Update.write(buffer,got)!=(size_t)got) break;
        mbedtls_sha256_update(&sha,buffer,got); written+=got;
        _progress=(int)(written*100/_firmware_size);
    }
    if (_cancel_requested) {
        mbedtls_sha256_free(&sha); http.end(); Update.abort();
        _error=i18n::tr("Update cancelled"); return false;
    }
    unsigned char digest[32]; mbedtls_sha256_finish(&sha,digest); mbedtls_sha256_free(&sha); http.end();
    char hex[65]; for (int i=0;i<32;++i) std::sprintf(hex+i*2,"%02x",digest[i]); hex[64]=0;
    if (written!=_firmware_size || _sha256!=hex) { Update.abort(); _error=i18n::tr("Firmware verification failed"); return false; }
    if (_cancel_requested) { Update.abort(); _error=i18n::tr("Update cancelled"); return false; }
    auto &cfg=ConfigStoreService::instance();
    cfg.load("update");
    cfg.set("installed_version_pending",_available_version);
    if (!cfg.save()) { _error=i18n::tr("Could not save update confirmation"); return false; }
    if (!Update.end(true)) { _error=i18n::tr("Could not activate firmware"); return false; }
    _progress=100; return true;
}
#endif

void UpdateService::acknowledge_install_notice()
{
    if (_installed_version_notice.empty()) return;
    _installed_version_notice.clear();
    auto &cfg=ConfigStoreService::instance();
    cfg.load("update"); cfg.set("installed_version_pending",std::string()); cfg.save();
}

void UpdateService::ignore_available()
{
    if (_available_version.empty()) return; _ignored_version=_available_version;
    auto &cfg=ConfigStoreService::instance(); cfg.load("update"); cfg.set("ignored_version",_ignored_version); cfg.save();
    _state=State::Idle;
}
void UpdateService::dismiss() { if (_state==State::Available || _state==State::Error) _state=State::Idle; }

void UpdateService::publish_if_changed()
{
    State current=_state; if (current==_published_state) return; _published_state=current;
    if (current==State::Available) EventBus::instance().publish({EventType::UpdateAvailable});
    EventBus::instance().publish({EventType::UpdateStateChanged});
    if (current==State::Idle || current==State::Error) EventBus::instance().publish({EventType::UpdateCheckFinished});
}
void UpdateService::loop()
{
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    if (supported()) {
        const uint32_t now = millis();
        const int result = _check_result.exchange(0, std::memory_order_acquire);
        if (result) {
            _check_running = false;
            _check_schedule.completed(now, result == 1, esp_random());
        }
        const bool due = _check_schedule.due(now);
        // Keep a pending update dialog/installation intact. Manual checks remain
        // available; overdue background checks resume when the dialog is dismissed.
        if (due && !_check_running && WifiService::instance().is_connected() &&
            (_state == State::Idle || _state == State::Error)) check(false);
    }
#endif
    publish_if_changed();
}

SERVICE_REGISTER(UpdateService);
