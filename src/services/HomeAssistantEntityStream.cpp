#include "services/HomeAssistantEntityStream.h"
#include <algorithm>
#include <cctype>
namespace ha {
namespace {std::string lower(std::string s){for(char&c:s)if(static_cast<unsigned char>(c)<128)c=std::tolower(static_cast<unsigned char>(c));return s;}}
EntityStream::EntityStream(size_t offset,const std::string&q,const std::string&kind):_offset(offset),_query(lower(q)),_kind(kind){}
bool EntityStream::accept(){
 Entity e;if(!parse_entity(_object,e,error))return false;
 if(!valid_entity(e.id)||(!_kind.empty()&&domain(e.id)!=_kind)||(!_query.empty()&&lower(e.id+" "+e.name).find(_query)==std::string::npos))return true;
 if(matches++>=_offset&&items.size()<MaxEntities)items.push_back(std::move(e));return true;
}
bool EntityStream::feed(const char*data,size_t size){
 if(!error.empty())return false;
 _bytes+=size;if(_bytes>16*1024*1024){error="Entity discovery exceeds 16 MiB";return false;}
 for(size_t i=0;i<size;++i){char c=data[i];
  if(_phase==2){
   if(_object.size()==1048576){error="A single Home Assistant entity exceeds 1 MiB";return false;}_object+=c;
   if(_quoted){if(_escaped)_escaped=false;else if(c=='\\')_escaped=true;else if(c=='"')_quoted=false;}
   else if(c=='"')_quoted=true;
   else if(c=='{'||c=='[')++_depth;
   else if(c=='}'||c==']'){if(--_depth==0){if(!accept())return false;_object.clear();_phase=3;}}
   continue;
  }
  if(c==' '||c=='\t'||c=='\n'||c=='\r')continue;
  if(_phase==0&&c=='[')_phase=1;
  else if((_phase==1||_phase==5)&&c=='{'){_object="{";_depth=1;_quoted=false;_escaped=false;_phase=2;}
  else if((_phase==1||_phase==3)&&c==']')_phase=4;
  else if(_phase==3&&c==',')_phase=5;
  else{error="Invalid Home Assistant response";return false;}
 }return true;
}
bool EntityStream::finish(){if(error.empty()&&_phase!=4)error="Incomplete Home Assistant response";return error.empty();}
}
