#include "services/AppUsageLedger.h"
#include <cassert>
#include <iostream>
using namespace AppUsage;
int main(){
    ConsentGate gate;assert(!gate.accepts(gate.token()));gate.set(true);auto token=gate.token();assert(gate.accepts(token));gate.set(false);assert(!gate.accepts(token));gate.set(true);assert(!gate.accepts(token));assert(gate.accepts(gate.token()));
    Ledger l;l.start("builtin.weather","1",0xffffff00);l.tick(0x2e8);assert(l.entries[0].seconds==1);
    l.tick(0x4dc);l.stop(0x6d0);assert(l.entries[0].seconds==2);
    l.tick(10000);assert(l.entries[0].seconds==2);
    l.start("builtin.weather","1",10000);assert(l.entries[0].launches==2);
    for(unsigned i=0;i<100;++i)Ledger::add(l.get("native.app-"+std::to_string(i),"1").installs);
    assert(l.entries.size()==32&&l.active==0&&l.entries[0].id=="builtin.weather");
    l.stop(12000);assert(l.entries[0].seconds==4);
    l.start("builtin.weather","2",12000);assert(l.entries[l.active].version=="2"&&l.entries[l.active].launches==1);
    uint32_t count=UINT32_MAX-1;Ledger::add(count,2);assert(count==UINT32_MAX);
    std::cout<<"PASS: usage duration, millis wrap, idle time, versions, bounded retention and saturation\n";
}
