#pragma once
#include <algorithm>
#include <string>
#include <vector>
namespace launcher {
constexpr size_t page_capacity = 9;
inline size_t page_count(size_t count) { return std::max(size_t(1),(count+page_capacity-1)/page_capacity); }
// Retain known identities in saved order, drop removals, append new identities.
inline std::vector<std::string> reconcile(const std::string &saved,const std::vector<std::string> &available) {
    std::vector<std::string> result;
    auto add=[&](const std::string &id){
        if(std::find(available.begin(),available.end(),id)!=available.end() &&
           std::find(result.begin(),result.end(),id)==result.end())result.push_back(id);
    };
    size_t start=0;
    while(start<saved.size()){auto end=saved.find('\n',start);add(saved.substr(start,end-start));if(end==std::string::npos)break;start=end+1;}
    for(const auto &id:available)add(id);
    return result;
}
inline std::string serialize(const std::vector<std::string> &ids){
    std::string result;for(const auto &id:ids){if(!result.empty())result+='\n';result+=id;}return result;
}
}
