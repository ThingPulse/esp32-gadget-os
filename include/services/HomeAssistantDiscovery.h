#pragma once
#include "services/HomeAssistantModel.h"
namespace ha {
constexpr size_t DiscoveryPageSize=25;
// The fixed template runs in Home Assistant; user input is passed only as variables.
std::string discovery_request(size_t offset,const std::string&query,const std::string&kind,bool hide_unavailable=true);
bool parse_discovery(const std::string&body,size_t offset,std::vector<Entity>&entities,size_t&total,std::string&error);
}
