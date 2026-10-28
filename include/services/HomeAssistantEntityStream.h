#pragma once
#include "services/HomeAssistantModel.h"
namespace ha {
// Incremental JSON-array framing: retain one entity, filter attributes while
// parsing it, and keep only one page of matching metadata. HTTP chunk framing
// is decoded by HTTPClient/curl before bytes reach this class.
class EntityStream {
public:
 EntityStream(size_t offset=0,const std::string&query="",const std::string&kind="");
 bool feed(const char*,size_t);bool finish();
 std::vector<Entity> items;
 std::string error;
 size_t matches=0;
private:
 bool accept();
 size_t _offset=0,_bytes=0;int _phase=0,_depth=0;
 bool _quoted=false,_escaped=false;
 std::string _query,_kind,_object;
};
}
