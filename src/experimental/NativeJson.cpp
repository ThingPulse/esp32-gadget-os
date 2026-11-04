#if GADGET_OS_NATIVE_APP_LOADER || defined(NATIVE_APP_DESKTOP_TEST)
#include "experimental/NativeHttp.h"
#include <ArduinoJson.h>
#include <cmath>
#include <climits>
#include <cstring>
namespace native_app {
int32_t json_number_scaled(const char *body,uint32_t size,const char *key,int32_t scale,int32_t *out)
{
    if (!body || !size || size>max_http_body || !key || !*key || strlen(key)>48 || !out || scale<1 || scale>1000000) return -1;
    JsonDocument doc;
    if (deserializeJson(doc,body,size,DeserializationOption::NestingLimit(8)) || !doc.is<JsonObject>()) return -1;
    auto value=doc[key];
    if (!value.is<double>()) return -1;
    double scaled=std::round(value.as<double>()*scale);
    if (!std::isfinite(scaled) || scaled<INT32_MIN || scaled>INT32_MAX) return -1;
    *out=static_cast<int32_t>(scaled); return 0;
}
}
#endif
