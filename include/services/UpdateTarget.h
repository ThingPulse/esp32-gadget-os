#pragma once
#include <ArduinoJson.h>

namespace ota {
inline const char *target_name() {
#if defined(COLOR_KIT_PRO) && COLOR_KIT_PRO
    return "esp32-color-kit-pro";
#else
    return "esp32-color-kit";
#endif
}

// Missing device entries must fail closed, never fall back to another board.
inline JsonObject manifest_target(JsonDocument &manifest) {
    return manifest["targets"][target_name()].as<JsonObject>();
}
}
