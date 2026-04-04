#include "services/ConfigStoreService.h"
#include "services/FileSystemService.h"
#include "ServiceRegistry.h"
#include <ArduinoJson.h>

struct ConfigStoreService::Impl {
    JsonDocument  doc;
    std::string   loaded_domain;
};

// ─── Singleton ───────────────────────────────────────────────────────────────

ConfigStoreService &ConfigStoreService::instance()
{
    static ConfigStoreService inst;
    return inst;
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

bool ConfigStoreService::begin()
{
    // Ensure the filesystem is ready first (idempotent)
    if (!FileSystemService::instance().begin()) return false;
    if (!_impl) _impl = new Impl();
    FileSystemService::instance().fs().mkdir_p("/config");
    return true;
}

ConfigStoreService::~ConfigStoreService()
{
    delete _impl;
}

// ─── Persistence ─────────────────────────────────────────────────────────────

bool ConfigStoreService::load(const char *domain)
{
    if (!_impl) return false;
    _impl->doc.clear();
    _impl->loaded_domain = domain;

    std::string raw;
    const std::string path = std::string("/config/") + domain + ".json";
    if (FileSystemService::instance().fs().read(path.c_str(), raw))
        deserializeJson(_impl->doc, raw.c_str(), raw.size());

    return true;
}

bool ConfigStoreService::save(const char *domain)
{
    if (!_impl) return false;
    const char *d = domain ? domain : _impl->loaded_domain.c_str();
    if (!d || d[0] == '\0') return false;

    // Serialize into a heap buffer sized by measureJson() to avoid depending
    // on ARDUINOJSON_ENABLE_STD_STRING on Arduino targets.
    const size_t len = measureJson(_impl->doc);
    std::string out(len, '\0');
    serializeJson(_impl->doc, &out[0], len + 1);

    const std::string path = std::string("/config/") + d + ".json";
    return FileSystemService::instance().fs().write(path.c_str(), out);
}

// ─── Typed getters ───────────────────────────────────────────────────────────

std::string ConfigStoreService::get(const char *key, const char *default_val) const
{
    if (!_impl) return default_val;
    if (_impl->doc[key].isNull()) return default_val;
    const char *s = _impl->doc[key].as<const char *>();
    return s ? s : default_val;
}

bool ConfigStoreService::get_bool(const char *key, bool default_val) const
{
    if (!_impl) return default_val;
    if (_impl->doc[key].isNull()) return default_val;
    return _impl->doc[key].as<bool>();
}

int ConfigStoreService::get_int(const char *key, int default_val) const
{
    if (!_impl) return default_val;
    if (_impl->doc[key].isNull()) return default_val;
    return _impl->doc[key].as<int>();
}

// ─── Typed setters ───────────────────────────────────────────────────────────

void ConfigStoreService::set(const char *key, const std::string &value)
{
    if (_impl) _impl->doc[key] = value.c_str();
}

void ConfigStoreService::set(const char *key, bool value)
{
    if (_impl) _impl->doc[key] = value;
}

void ConfigStoreService::set(const char *key, int value)
{
    if (_impl) _impl->doc[key] = value;
}

SERVICE_REGISTER(ConfigStoreService);
