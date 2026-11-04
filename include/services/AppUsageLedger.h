#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
namespace AppUsage {
// UI changes consent; the reporting worker checks a snapshot token without
// touching UI-owned strings or storage. Off/on invalidates an older payload.
struct ConsentGate {
    std::atomic<uint32_t> generation{0};
    void set(bool enabled){if(bool(generation.load()&1)!=enabled)generation.fetch_add(1);}
    uint32_t token()const{return generation.load();}
    bool accepts(uint32_t value)const{return (value&1)&&generation.load()==value;}
};
struct Entry {
    std::string id,version;
    uint32_t launches=0,seconds=0,installs=0,failures=0;
};
struct Ledger {
    static constexpr size_t Capacity=32;
    std::vector<Entry> entries;
    int active=-1;
    uint32_t last=0,remainder=0;
    static void add(uint32_t &n,uint32_t amount=1){n=amount>UINT32_MAX-n?UINT32_MAX:n+amount;}
    void tick(uint32_t now){
        if(active>=0){uint64_t ms=uint32_t(now-last)+uint64_t(remainder);add(entries[active].seconds,uint32_t(ms/1000));remainder=ms%1000;}
        last=now;
    }
    Entry &get(const std::string &id,const std::string &version){
        for(auto &e:entries)if(e.id==id&&e.version==version)return e;
        if(entries.size()==Capacity){
            // Retain the running entry when installation events fill the ledger.
            size_t drop=active==0?1:0;entries.erase(entries.begin()+drop);if(active>int(drop))--active;
        }
        entries.push_back({id,version});return entries.back();
    }
    void start(const std::string &id,const std::string &version,uint32_t now){
        tick(now);active=-1;remainder=0;
        auto &e=get(id,version);add(e.launches);active=int(&e-entries.data());
    }
    void stop(uint32_t now){tick(now);active=-1;remainder=0;}
};
}
