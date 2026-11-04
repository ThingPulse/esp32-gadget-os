#if GADGET_OS_NATIVE_APP_LOADER
#include "experimental/NativePackage.h"
#include <ArduinoJson.h>
#include <cstring>
#include <algorithm>
#include <vector>
namespace native_app {
static bool fail(std::string &e, const char *s) { e = s; return false; }
bool parse_manifest(const std::string &json, const char *chip, Manifest &out, std::string &e)
{
    if (json.empty() || json.size() > 2048) return fail(e, "Manifest missing or too large");
    JsonDocument d;
    if (deserializeJson(d, json) || !d.is<JsonObject>()) return fail(e, "Malformed manifest");
    for (const char *key : {"id", "version", "chip", "executable"})
        if (!d[key].is<const char *>() || !d[key].as<const char *>()[0]) return fail(e, "Missing manifest field");
    if (d["chip"].as<std::string>() != chip) return fail(e, "Package is for a different chip");
    if (!d["api_major"].is<unsigned>() || !d["api_minor"].is<unsigned>() ||
        d["api_major"].as<unsigned>() != 1 || d["api_minor"].as<unsigned>() > 1)
        return fail(e, "Unsupported host API version");
    out = {d["id"].as<std::string>(), d["version"].as<std::string>(), d["executable"].as<std::string>()};
    if ((out.id != "native-demo" && out.id != "native-iss") || out.version.size() > 32 || out.executable != "app.elf")
        return fail(e, "Expected native-demo or native-iss package with app.elf");
    return true;
}
static uint16_t u16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
static uint32_t u32(const uint8_t *p) { return u16(p) | uint32_t(u16(p+2)) << 16; }
struct Section { uint32_t name,type,flags,addr,off,size,link,info,align,entsize; const char *label; bool loaded; };
bool validate_elf(const uint8_t *b, size_t n, std::string &e)
{
    auto range = [n](uint32_t off, uint32_t len) { return off <= n && len <= n-off; };
    if (!b || n < 52 || n > max_elf_bytes || memcmp(b, "\177ELF\1\1\1", 7) ||
        u16(b+16) != 3 || u16(b+18) != 94 || u32(b+20) != 1 || u16(b+40) != 52)
        return fail(e, "Expected bounded ELF32 little-endian Xtensa shared image");
    unsigned count=u16(b+48), names=u16(b+50); uint32_t table=u32(b+32);
    if (!count || count > 64 || names >= count || u16(b+46) != 40 || table%4 || !range(table,count*40))
        return fail(e, "Invalid ELF section table");
    if (u16(b+44) && (u16(b+42)!=32 || !range(u32(b+28), uint32_t(u16(b+44))*32)))
        return fail(e, "Invalid ELF program table");
    std::vector<Section> s(count);
    for (unsigned i=0;i<count;++i) {
        const uint8_t *p=b+table+i*40;
        s[i]={u32(p),u32(p+4),u32(p+8),u32(p+12),u32(p+16),u32(p+20),u32(p+24),u32(p+28),u32(p+32),u32(p+36),nullptr,false};
        auto &v=s[i];
        if (v.size > max_elf_bytes || (v.type!=8 && !range(v.off,v.size)) ||
            v.addr > UINT32_MAX-v.size || v.align>16 || (v.align && (v.align&(v.align-1))))
            return fail(e,"Invalid ELF section bounds/alignment");
    }
    if (s[names].type!=3) return fail(e,"Invalid ELF string table");
    size_t allocated=0; bool entry_ok=false;
    for (auto &v:s) {
        if (v.name>=s[names].size || !memchr(b+s[names].off+v.name,0,s[names].size-v.name))
            return fail(e,"Invalid ELF section name");
        v.label=reinterpret_cast<const char *>(b+s[names].off+v.name);
        v.loaded = !strcmp(v.label,".text") || !strcmp(v.label,".rodata") || !strcmp(v.label,".data") ||
                   !strcmp(v.label,".data.rel.ro") || !strcmp(v.label,".bss");
        if (v.loaded) {
            if (v.align<4 || v.addr%4 || !(v.flags&2) || (v.type!=1 && v.type!=8) ||
                (v.type==8 && strcmp(v.label,".bss"))) return fail(e,"Invalid loadable section");
            if ((!strcmp(v.label,".data") || !strcmp(v.label,".bss")) && !(v.flags&1))
                return fail(e,"Writable section lacks write flag");
            if (!strcmp(v.label,".bss") && v.type!=8) return fail(e,"BSS must be NOBITS");
            allocated+=v.size+v.align;
            if (!strcmp(v.label,".text")) {
                if (!(v.flags&4) || v.off%4 || v.addr%4 || v.size%4) return fail(e,"Unaligned ELF text");
                entry_ok=u32(b+24)>=v.addr && u32(b+24)-v.addr<v.size;
            }
        } else if ((v.flags&2) && v.type!=3 && v.type!=4 && v.type!=5 && v.type!=11 && v.type!=0x6ffffff6)
            return fail(e,"Unsupported allocated section (no constructors, TLS or PLT)");
    }
    if (!entry_ok || allocated>max_elf_bytes) return fail(e,"Invalid entry or excessive ELF allocation");
    for (unsigned i=0;i<count;++i) for (unsigned j=i+1;j<count;++j)
        if (s[i].loaded && s[j].loaded &&
            (!strcmp(s[i].label,s[j].label) || (s[i].size && s[j].size && s[i].addr<s[j].addr+s[j].size && s[j].addr<s[i].addr+s[i].size)))
            return fail(e,"Overlapping/duplicate loaded sections");
    auto mapped = [&s](uint32_t address, uint32_t bytes)->const Section * {
        for (auto &v:s) if (v.loaded && address>=v.addr && address-v.addr<=v.size && bytes<=v.size-(address-v.addr)) return &v;
        return nullptr;
    };
    std::vector<uint32_t> destinations;
    for (auto &v:s) {
        if (v.type==11 || v.type==2) {
            if (v.entsize!=16 || v.size%16 || v.link>=count || s[v.link].type!=3 || v.off%4)
                return fail(e,"Invalid symbol table");
            for (unsigned k=0;k<v.size;k+=16) {
                const uint8_t *p=b+v.off+k; uint32_t name=u32(p);
                if (name>=s[v.link].size || !memchr(b+s[v.link].off+name,0,s[v.link].size-name)) return fail(e,"Invalid symbol name");
                if (k && u16(p+14)==0 && name) return fail(e,"External symbols are not allowed");
            }
        }
        if (v.type==9) return fail(e,"REL relocations unsupported");
        if (v.type!=4) continue;
        if (v.entsize!=12 || v.size%12 || v.off%4 || v.link>=count || s[v.link].type!=11 || s[v.link].size<16)
            return fail(e,"Invalid relocation table");
        for (unsigned k=0;k<v.size;k+=12) {
            const uint8_t *p=b+v.off+k; uint32_t where=u32(p), info=u32(p+4);
            // This SDK links locally with -Bsymbolic: only relative relocations.
            if (info!=5 || u32(p+8)!=0 || where%4) return fail(e,"Unsupported relocation");
            destinations.push_back(where);
            auto *dest=mapped(where,4);
            if (!dest || dest->type==8) return fail(e,"Invalid relocation destination");
            if (!mapped(u32(b+dest->off+where-dest->addr),1)) return fail(e,"Invalid relocation value");
        }
    }
    std::sort(destinations.begin(),destinations.end());
    if (std::adjacent_find(destinations.begin(),destinations.end())!=destinations.end())
        return fail(e,"Duplicate relocation destination");
    return true;
}
}
#endif
