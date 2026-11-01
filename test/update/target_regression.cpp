#include "services/UpdateTarget.h"
#include <cassert>
#include <string>
#include <cstdio>

int main() {
    JsonDocument manifest;
    assert(!deserializeJson(manifest, R"({"targets":{
        "esp32-color-kit":{"url":"grande.bin","size":123},
        "esp32-color-kit-pro":{"url":"pro.bin","size":456}
    }})"));
#if defined(COLOR_KIT_PRO) && COLOR_KIT_PRO
    const char *expected="esp32-color-kit-pro", *other="esp32-color-kit", *file="pro.bin";
    const int size=456;
#else
    const char *expected="esp32-color-kit", *other="esp32-color-kit-pro", *file="grande.bin";
    const int size=123;
#endif
    assert(std::string(ota::target_name())==expected);
    auto target=ota::manifest_target(manifest);
    assert(std::string(target["url"].as<const char*>())==file);
    assert(target["size"].as<int>()==size);
    manifest["targets"].remove(expected);
    assert(!manifest["targets"][other].isNull());
    assert(ota::manifest_target(manifest).isNull());
    manifest["targets"][expected]="not an object";
    assert(ota::manifest_target(manifest).isNull());
    manifest.clear();
    assert(ota::manifest_target(manifest).isNull());
    std::printf("%s: correct image; missing/malformed target rejected\n",expected);
}
