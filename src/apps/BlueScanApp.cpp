#include "apps/BlueScanApp.h"

#include "AppRegistry.h"
#include "ui/StatusIcons.h"
#include "ui/UiNav.h"
#include "ui/UiSessionState.h"
#include "ui/UiTrace.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include <ArduinoJson.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>

#if __has_include(<TheengsDecoder.h>)
#include <TheengsDecoder.h>
#define BLUE_SCAN_HAS_THEENGS 1
#elif __has_include(<decoder.h>)
#include <decoder.h>
#define BLUE_SCAN_HAS_THEENGS 1
#else
#define BLUE_SCAN_HAS_THEENGS 0
#endif
#else
#define BLUE_SCAN_HAS_THEENGS 0
#endif

APP_REGISTER_COLOR("BlueScan", LV_SYMBOL_BLUETOOTH, blue_scan_app_launch, 0x1971c2);

namespace {

constexpr int MAX_ROWS = 8;

struct DeviceRow {
  bool valid = false;
  std::string addr;
  std::string name;
  int rssi = 0;
  std::string decoded;
  uint32_t last_seen_ms = 0;
};

struct BlueScanAppCtx {
  lv_obj_t *screen = nullptr;
  lv_obj_t *status_label = nullptr;
  lv_obj_t *rows[MAX_ROWS]{};
  lv_obj_t *primary_labels[MAX_ROWS]{};
  lv_obj_t *secondary_labels[MAX_ROWS]{};
  lv_timer_t *refresh_timer = nullptr;
  bool closing = false;
};

static BlueScanAppCtx app_ctx;
static std::array<DeviceRow, MAX_ROWS> g_rows;

#if defined(ARDUINO) && defined(ESP_PLATFORM)
static BLEScan *g_scan = nullptr;

#if BLUE_SCAN_HAS_THEENGS
static TheengsDecoder g_decoder;
#endif

static std::string hex_encode(const std::string &raw)
{
  static const char *HEX = "0123456789ABCDEF";
  std::string out;
  out.reserve(raw.size() * 2);
  for (unsigned char c : raw) {
    out.push_back(HEX[(c >> 4) & 0x0F]);
    out.push_back(HEX[c & 0x0F]);
  }
  return out;
}

#if BLUE_SCAN_HAS_THEENGS
template <typename DecoderT>
static auto decoder_invoke(DecoderT &decoder, DynamicJsonDocument &doc, JsonObject &obj, int)
    -> decltype(decoder.decodeBLEJson(obj), bool())
{
  (void)doc;
  return decoder.decodeBLEJson(obj);
}

template <typename DecoderT>
static auto decoder_invoke(DecoderT &decoder, DynamicJsonDocument &doc, JsonObject &obj, long)
    -> decltype(decoder.decodeBLEJson(doc), bool())
{
  (void)obj;
  return decoder.decodeBLEJson(doc);
}

static bool decode_with_theengs(const std::string &addr,
                                int rssi,
                                const std::string &manufacturer,
                                const std::vector<std::pair<std::string, std::string>> &service_data,
                                std::string &decoded)
{
  DynamicJsonDocument doc(768);
  JsonObject payload = doc.to<JsonObject>();
  payload["id"] = addr;
  payload["rssi"] = rssi;

  if (!manufacturer.empty()) payload["manufacturerdata"] = hex_encode(manufacturer);

  if (!service_data.empty()) {
    JsonObject svc_obj = payload.createNestedObject("servicedatauuid");
    for (const auto &entry : service_data) svc_obj[entry.first] = hex_encode(entry.second);
  }

  const bool recognized = decoder_invoke(g_decoder, doc, payload, 0);
  if (!recognized) return false;

  char buf[96] = {0};
  bool has_metric = false;

  if (payload["tempc"].is<float>()) {
    std::snprintf(buf, sizeof(buf), "%.1f C", payload["tempc"].as<float>());
    decoded += std::string("Temp ") + buf;
    has_metric = true;
  }
  if (payload["hum"].is<float>()) {
    if (has_metric) decoded += " | ";
    std::snprintf(buf, sizeof(buf), "%.0f%%", payload["hum"].as<float>());
    decoded += std::string("Hum ") + buf;
    has_metric = true;
  }
  if (payload["batt"].is<int>()) {
    if (has_metric) decoded += " | ";
    std::snprintf(buf, sizeof(buf), "%d%%", payload["batt"].as<int>());
    decoded += std::string("Batt ") + buf;
    has_metric = true;
  }

  if (!has_metric) return false;
  return true;
}
#endif

static int find_slot_for_addr(const std::string &addr)
{
  for (int i = 0; i < MAX_ROWS; ++i) {
    if (g_rows[i].valid && g_rows[i].addr == addr) return i;
  }

  for (int i = 0; i < MAX_ROWS; ++i) {
    if (!g_rows[i].valid) return i;
  }

  int oldest = 0;
  for (int i = 1; i < MAX_ROWS; ++i) {
    if (g_rows[i].last_seen_ms < g_rows[oldest].last_seen_ms) oldest = i;
  }
  return oldest;
}

static void update_device_from_adv(BLEAdvertisedDevice advertised)
{
  const std::string addr = advertised.getAddress().toString();
  const int slot = find_slot_for_addr(addr);

  DeviceRow &row = g_rows[slot];
  row.valid = true;
  row.addr = addr;
  row.name = advertised.haveName() ? advertised.getName() : "Unnamed";
  row.rssi = advertised.getRSSI();
  row.last_seen_ms = millis();

  std::string decoded = "No decoder match";
#if BLUE_SCAN_HAS_THEENGS
  std::vector<std::pair<std::string, std::string>> service_data;
  for (int i = 0; i < advertised.getServiceDataCount(); ++i) {
    BLEUUID service_uuid = advertised.getServiceDataUUID(i);
    service_data.emplace_back(service_uuid.toString(), advertised.getServiceData(i));
  }

  if (decode_with_theengs(addr,
                          row.rssi,
                          advertised.getManufacturerData(),
                          service_data,
                          decoded)) {
    row.decoded = decoded;
  } else {
    row.decoded = "No decoded metrics";
  }
#else
  row.decoded = decoded;
#endif
}

class BlueScanAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override
  {
    update_device_from_adv(advertisedDevice);
  }
};

static BlueScanAdvertisedDeviceCallbacks g_adv_cb;

static void start_scan()
{
  if (!g_scan) {
    BLEDevice::init("");
    g_scan = BLEDevice::getScan();
    g_scan->setAdvertisedDeviceCallbacks(&g_adv_cb, false);
    g_scan->setInterval(90);
    g_scan->setWindow(70);
    g_scan->setActiveScan(true);
  }

  if (g_scan) {
    g_scan->clearResults();
    g_scan->start(0, nullptr, false);
  }
}

static void stop_scan()
{
  if (g_scan) g_scan->stop();
}
#endif

static void nav_back_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  app_ctx.closing = true;
#if defined(ARDUINO) && defined(ESP_PLATFORM)
  stop_scan();
#endif
  if (app_ctx.refresh_timer) {
    lv_timer_delete(app_ctx.refresh_timer);
    app_ctx.refresh_timer = nullptr;
  }

  UiSessionState::set_route("home");
  lv_obj_t *target_scr = (lv_obj_t *)lv_event_get_user_data(e);
  if (!target_scr || !lv_obj_is_valid(target_scr)) return;
  ui_load_screen(target_scr, true);
}

static void blue_scan_screen_delete_event(lv_event_t *e)
{
  (void)e;
#if defined(ARDUINO) && defined(ESP_PLATFORM)
  stop_scan();
#endif
  if (app_ctx.refresh_timer) {
    lv_timer_delete(app_ctx.refresh_timer);
    app_ctx.refresh_timer = nullptr;
  }
  app_ctx = BlueScanAppCtx{};
}

static void update_ui()
{
  if (app_ctx.closing) return;
  if (!app_ctx.screen || !lv_obj_is_valid(app_ctx.screen)) return;
  if (!app_ctx.status_label || !lv_obj_is_valid(app_ctx.status_label)) return;

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#if BLUE_SCAN_HAS_THEENGS
  lv_label_set_text(app_ctx.status_label, "Scanning BLE + decoding telemetry...");
#else
  lv_label_set_text(app_ctx.status_label, "Scanning BLE (decoder library unavailable)");
#endif
#else
  lv_label_set_text(app_ctx.status_label, "BLE scan available on ESP32 hardware builds only");
#endif

  for (int i = 0; i < MAX_ROWS; ++i) {
    if (!app_ctx.rows[i] || !lv_obj_is_valid(app_ctx.rows[i])) continue;
    if (!app_ctx.primary_labels[i] || !lv_obj_is_valid(app_ctx.primary_labels[i])) continue;
    if (!app_ctx.secondary_labels[i] || !lv_obj_is_valid(app_ctx.secondary_labels[i])) continue;

    if (!g_rows[i].valid) {
      lv_obj_add_flag(app_ctx.rows[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_clear_flag(app_ctx.rows[i], LV_OBJ_FLAG_HIDDEN);

    char primary_buf[96] = {0};
    std::snprintf(primary_buf, sizeof(primary_buf), "%s (%d dBm)", g_rows[i].name.c_str(), g_rows[i].rssi);
    lv_label_set_text(app_ctx.primary_labels[i], primary_buf);

    std::string secondary = g_rows[i].addr + " | " + g_rows[i].decoded;
    lv_label_set_text(app_ctx.secondary_labels[i], secondary.c_str());
  }
}

static void update_timer_cb(lv_timer_t *timer)
{
  (void)timer;
  update_ui();
}

}  // namespace

void blue_scan_app_launch(void)
{
  app_ctx.closing = false;
  for (auto &row : g_rows) row = DeviceRow{};

  lv_obj_t *home_scr = lv_screen_active();

  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_add_event_cb(scr, blue_scan_screen_delete_event, LV_EVENT_DELETE, nullptr);

  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_size(bar, lv_pct(100), 44);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x14162a), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);

  lv_obj_t *back_btn = lv_button_create(bar);
  lv_obj_set_size(back_btn, 44, 36);
  lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(back_btn, 0, 0);
  lv_obj_set_style_border_width(back_btn, 0, 0);
  lv_obj_add_event_cb(back_btn, nav_back_event, LV_EVENT_CLICKED, home_scr);

  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
  lv_obj_center(back_label);

  lv_obj_t *title = lv_label_create(bar);
  lv_label_set_text(title, "BlueScan");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  create_time_status_label(bar, 56);
  create_wifi_status_icon(bar, -6);

  lv_obj_t *content = lv_obj_create(scr);
  lv_obj_set_pos(content, 0, 44);
  lv_obj_set_size(content, lv_pct(100), 480 - 44);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 12, 0);
  lv_obj_set_style_pad_row(content, 8, 0);
  lv_obj_set_layout(content, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

  app_ctx.status_label = lv_label_create(content);
  lv_obj_set_style_text_color(app_ctx.status_label, lv_color_hex(0xb7bdd7), 0);
  lv_label_set_text(app_ctx.status_label, "Starting scanner...");

  for (int i = 0; i < MAX_ROWS; ++i) {
    app_ctx.rows[i] = lv_obj_create(content);
    lv_obj_set_width(app_ctx.rows[i], lv_pct(100));
    lv_obj_set_style_bg_color(app_ctx.rows[i], lv_color_hex(0x1f2438), 0);
    lv_obj_set_style_bg_opa(app_ctx.rows[i], LV_OPA_70, 0);
    lv_obj_set_style_border_width(app_ctx.rows[i], 0, 0);
    lv_obj_set_style_radius(app_ctx.rows[i], 8, 0);
    lv_obj_set_style_pad_all(app_ctx.rows[i], 8, 0);
    lv_obj_set_layout(app_ctx.rows[i], LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(app_ctx.rows[i], LV_FLEX_FLOW_COLUMN);

    app_ctx.primary_labels[i] = lv_label_create(app_ctx.rows[i]);
    lv_obj_set_style_text_color(app_ctx.primary_labels[i], lv_color_white(), 0);

    app_ctx.secondary_labels[i] = lv_label_create(app_ctx.rows[i]);
    lv_obj_set_style_text_color(app_ctx.secondary_labels[i], lv_color_hex(0xb7bdd7), 0);
    lv_obj_set_style_text_font(app_ctx.secondary_labels[i], &lv_font_montserrat_14, 0);

    lv_obj_add_flag(app_ctx.rows[i], LV_OBJ_FLAG_HIDDEN);
  }

#if defined(ARDUINO) && defined(ESP_PLATFORM)
  start_scan();
#endif

  app_ctx.screen = scr;
  app_ctx.refresh_timer = lv_timer_create(update_timer_cb, 1000, nullptr);

  update_ui();
  UiSessionState::set_route("blue_scan");
  ui_load_screen(scr, true);
}
