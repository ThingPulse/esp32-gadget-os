#include "services/FileSystemService.h"
#include "ServiceRegistry.h"

// ─── Platform implementations ────────────────────────────────────────────────

#ifdef ARDUINO
// ESP32: LittleFS (SPIFFS successor, part of Arduino-ESP32 core)
#include <LittleFS.h>

class PlatformFS : public IFileSystem {
public:
    bool exists(const char *path) override
    {
        return LittleFS.exists(path);
    }

    bool read(const char *path, std::string &out) override
    {
        File f = LittleFS.open(path, "r");
        if (!f) return false;
        out.clear();
        while (f.available())
            out += static_cast<char>(f.read());
        f.close();
        return true;
    }

    bool write(const char *path, const std::string &data) override
    {
        File f = LittleFS.open(path, "w");
        if (!f) return false;
        f.print(data.c_str());
        f.close();
        return true;
    }

    bool remove(const char *path) override
    {
        return LittleFS.remove(path);
    }

    bool mkdir_p(const char *path) override
    {
        return LittleFS.mkdir(path);
    }
};

bool FileSystemService::begin()
{
    if (_impl) return true;
    if (!LittleFS.begin(/*formatOnFail=*/true)) return false;
    _impl = new PlatformFS();
    return true;
}

#else
// Native: POSIX file I/O; files are stored under ./data/ relative to CWD.
#include <cerrno>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

static constexpr const char *FS_ROOT = "./data";

class PlatformFS : public IFileSystem {
    static std::string full(const char *path)
    {
        return std::string(FS_ROOT) + path;
    }

public:
    bool exists(const char *path) override
    {
        struct stat st{};
        return stat(full(path).c_str(), &st) == 0;
    }

    bool read(const char *path, std::string &out) override
    {
        std::ifstream f(full(path));
        if (!f) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }

    bool write(const char *path, const std::string &data) override
    {
        std::ofstream f(full(path));
        if (!f) return false;
        f << data;
        return true;
    }

    bool remove(const char *path) override
    {
        return std::remove(full(path).c_str()) == 0;
    }

    bool mkdir_p(const char *path) override
    {
        return ::mkdir(full(path).c_str(), 0755) == 0 || errno == EEXIST;
    }
};

bool FileSystemService::begin()
{
    if (_impl) return true;
    ::mkdir(FS_ROOT, 0755);  // ensure root exists; ignore EEXIST
    _impl = new PlatformFS();
    return true;
}

#endif  // ARDUINO

// ─── Singleton + service wiring ──────────────────────────────────────────────

FileSystemService &FileSystemService::instance()
{
    static FileSystemService inst;
    return inst;
}

IFileSystem &FileSystemService::fs()
{
    return *_impl;
}

SERVICE_REGISTER(FileSystemService);
