#pragma once
#include <cstddef>
#include <cstdint>
namespace native_app {
bool sha256_parts(const uint8_t*,size_t,const uint8_t*,size_t,const uint8_t*,size_t,uint8_t[32]);
bool verify_signature(const char *pem,const uint8_t digest[32],const uint8_t signature[64]);
}
