#pragma once
#include <cstddef>
#include <string>
namespace native_app { namespace companion {
struct Request {
    std::string method,route,key;
    size_t length=0,body_offset=0;
};
// 0 = incomplete, 1 = parsed, negative HTTP status = reject. No browser origins,
// redirects, chunking or ambiguous duplicate credentials/length/Host headers.
int parse_request(const std::string &headers,const std::string &host,Request &out);
}}
