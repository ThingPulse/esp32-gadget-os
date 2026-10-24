#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace transit {
constexpr size_t MaxStops=8, MaxDepartures=8, MaxCalls=128;
enum class Request { Search, Board, Trip };
struct Stop { std::string id,name,area; };
struct Call { std::string id,parent,name,platform; int64_t time=0; bool cancelled=false; };
struct Departure { std::string line,destination,trip,platform; int64_t scheduled=0,time=0; bool live=false,cancelled=false; uint32_t color=0x2196f3; };
struct Result {
 std::vector<Stop> stops; std::vector<Departure> departures; std::vector<Call> calls;
 std::string error; uint32_t retry_ms=0; bool ok() const {return error.empty();}
};
std::string encode(const std::string &value);
int64_t timestamp(const std::string &value);
std::string clock_text(int64_t value); // Device timezone; never changes process TZ.
std::string countdown(const Departure &d,int64_t now);
class Provider {
public: virtual ~Provider()=default;
 virtual const char *id() const=0;
 virtual Result fetch(Request kind,const std::string &argument)=0;
};
class TransitousProvider : public Provider {
public:
 const char *id() const override {return "transitous";}
 Result fetch(Request kind,const std::string &argument) override;
 static Result parse(Request kind,const std::string &body);
};
}
