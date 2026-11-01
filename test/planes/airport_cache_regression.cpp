#include "services/AirportCache.h"
#include "services/FileSystemService.h"
#include <cassert>
#include <limits>
#include <string>
int main() {
    AirportLocation zrh{"LSZH","ZRH",47.458,8.555};
    AirportLocation lhr{"EGLL","LHR",51.47,-0.454};
    AirportCache c;
    c.select_view(47.4247,8.547,25);
    assert(c.entries().empty());
    assert(c.merge({zrh,lhr}));
    assert(c.entries().size()==1); // Only airports in this view are stored.
    assert(!c.merge({zrh})); // No writes on unchanged sightings.
    AirportCache restarted;
    restarted.select_view(47.4247,8.547,25);
    assert(restarted.entries().size()==1 && restarted.entries()[0].code=="ZRH");
    restarted.select_view(47.4247,8.547,25);
    assert(restarted.entries().size()==1);
    restarted.select_view(47.4247,8.547,10);
    assert(restarted.entries().empty());
    assert(!FileSystemService::instance().fs().exists("/cache/plane-airports.json"));
    assert(restarted.merge({zrh}));
    AirportCache different;
    different.select_view(48,9,10);
    assert(different.entries().empty());
    assert(!FileSystemService::instance().fs().exists("/cache/plane-airports.json"));
    auto invalid=zrh;invalid.latitude=std::numeric_limits<double>::quiet_NaN();
    assert(!different.merge({invalid}));
    invalid=zrh;invalid.icao="<bad>";assert(!different.merge({invalid}));
    FileSystemService::instance().fs().write("/cache/plane-airports.json","{broken");
    AirportCache corrupt;corrupt.select_view(47.4247,8.547,25);assert(corrupt.entries().empty());
    for(int i=0;i<80;++i) {
        AirportLocation a{"T"+std::to_string(100+i),"ABC",47.43,8.55};
        corrupt.merge({a});
    }
    assert(corrupt.entries().size()==AirportCache::LIMIT);
    AirportCache bounded;bounded.select_view(47.4247,8.547,25);
    assert(bounded.entries().size()==AirportCache::LIMIT);
    bounded.clear();assert(bounded.entries().empty());
    assert(!FileSystemService::instance().fs().exists("/cache/plane-airports.json"));
    assert(AirportCache::distance(0,179.99,0,-179.99)<3); // Date-line wrap.
}
