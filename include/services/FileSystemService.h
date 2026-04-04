#pragma once
#include "ServiceRegistry.h"
#include <string>

// ─── Abstract filesystem interface ──────────────────────────────────────────
// Both the ESP32 (LittleFS) and native (POSIX) implementations satisfy this.
// Paths use a POSIX-like root, e.g. "/config/wifi.json".
class IFileSystem {
public:
    virtual bool exists(const char *path) = 0;
    virtual bool read(const char *path, std::string &out) = 0;
    virtual bool write(const char *path, const std::string &data) = 0;
    virtual bool remove(const char *path) = 0;
    // Create the directory at path (single level; parents must already exist).
    virtual bool mkdir_p(const char *path) = 0;
    virtual ~IFileSystem() = default;
};

// ─── FileSystemService ───────────────────────────────────────────────────────
// System service wrapper.  begin() mounts the underlying filesystem.
// Access the filesystem through fs() after begin() succeeds.
class FileSystemService : public SystemService {
public:
    static FileSystemService &instance();

    const char *name() const override { return "FileSystem"; }
    // Idempotent: safe to call multiple times.
    bool begin() override;

    IFileSystem &fs();

private:
    IFileSystem *_impl = nullptr;
};
