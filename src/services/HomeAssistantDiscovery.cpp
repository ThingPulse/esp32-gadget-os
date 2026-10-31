#include "services/HomeAssistantDiscovery.h"
#include <ArduinoJson.h>
#include <algorithm>
namespace ha {
std::string discovery_request(size_t offset,const std::string&query,const std::string&kind,bool hide_unavailable){
 static const char *template_text=R"({%- set q = query | trim | lower -%}
{%- set source = states[kind] if kind else states -%}
{%- set ns = namespace(total=0, items=[]) -%}
{%- for s in source | sort(attribute='entity_id') -%}
{%- if s.entity_id.split('.')[0] in ['light','switch','sensor','binary_sensor','scene','script'] and s.entity_id | length <= 128 and (not hide_unavailable or s.state not in ['unknown','unavailable']) and q in ((s.entity_id ~ ' ' ~ s.name) | lower) -%}
{%- if ns.total >= offset and ns.items | length < limit -%}
{%- set ns.items = ns.items + [{'id': s.entity_id, 'name': s.name[:128], 'available': s.state not in ['unknown','unavailable']}] -%}
{%- endif -%}
{%- set ns.total = ns.total + 1 -%}
{%- endif -%}
{%- endfor -%}
{{- {'entities': ns.items, 'total': ns.total, 'offset': offset} | to_json -}})";
 JsonDocument d;d["template"]=template_text;d["variables"]["query"]=query;d["variables"]["kind"]=kind;d["variables"]["offset"]=offset;d["variables"]["limit"]=DiscoveryPageSize;d["variables"]["hide_unavailable"]=hide_unavailable;
 std::string body;serializeJson(d,body);return body;
}
bool parse_discovery(const std::string&body,size_t offset,std::vector<Entity>&entities,size_t&total,std::string&error){
 auto fail=[&](){error="Invalid entity search response";return false;};
 JsonDocument d;if(body.size()>32768||deserializeJson(d,body)||!d.is<JsonObject>()||!d["total"].is<size_t>()||!d["offset"].is<size_t>()||d["offset"].as<size_t>()!=offset||!d["entities"].is<JsonArray>())return fail();
 auto list=d["entities"].as<JsonArrayConst>();size_t count=d["total"].as<size_t>();
 if(list.size()>DiscoveryPageSize||list.size()!=(offset<count?std::min(DiscoveryPageSize,count-offset):0))return fail();
 std::vector<Entity> result;
 for(JsonObjectConst item:list){
  if(!item["id"].is<const char*>()||!item["name"].is<const char*>()||!item["available"].is<bool>())return fail();
  Entity e;e.id=item["id"].as<std::string>();e.name=item["name"].as<std::string>();e.state=item["available"].as<bool>()?"available":"unavailable";
  if(!valid_entity(e.id)||e.name.size()>512||std::any_of(result.begin(),result.end(),[&](const Entity&other){return other.id==e.id;}))return fail();
  result.push_back(std::move(e));
 }
 entities=std::move(result);total=count;error.clear();return true;
}
}
