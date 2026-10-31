#pragma once
#include "services/HomeAssistantModel.h"
namespace ha {
class EntityStream;
bool request(const Config&,const std::string&path,const std::string&payload,std::string&out,std::string&error,EntityStream*discovery=nullptr,int*response_status=nullptr);
// Setup server is driven from the main loop; no worker accesses UI/configuration.
bool server_start();void server_stop();void server_loop();std::string server_address();
}
