#pragma once
#include "ServiceRegistry.h"
#include <string>

// JSON-backed key-value configuration store.
//
// Each "domain" maps to the file /config/<domain>.json on the filesystem.
//
// Typical usage:
//   auto &cfg = ConfigStoreService::instance();
//   cfg.load("wifi");
//   cfg.set("ssid", std::string("MyNetwork"));
//   cfg.save();           // saves the currently loaded domain
//
//   std::string ssid = cfg.get("ssid", "");
//
// Note: only one domain is held in memory at a time.  Call load() again to
// switch domains (unsaved changes to the previous domain are discarded).
class ConfigStoreService : public SystemService {
public:
    static ConfigStoreService &instance();

    const char *name() const override { return "ConfigStore"; }
    bool begin() override;
    ~ConfigStoreService() override;

    // Load domain from disk.  Creates an empty store if the file is absent.
    bool load(const char *domain);
    // Persist the current in-memory state.
    // If domain is nullptr, re-uses the last loaded domain name.
    bool save(const char *domain = nullptr);

    // Typed getters with defaults
    std::string get(const char *key, const char *default_val = "") const;
    bool        get_bool(const char *key, bool default_val = false) const;
    int         get_int(const char *key, int  default_val = 0)     const;

    // Typed setters
    void set(const char *key, const std::string &value);
    void set(const char *key, bool  value);
    void set(const char *key, int   value);

private:
    // pimpl keeps ArduinoJson out of every header that includes this file
    struct Impl;
    Impl *_impl = nullptr;
};
