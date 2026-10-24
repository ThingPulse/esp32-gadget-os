#include <string>
#include <unordered_map>
#define private public
#include "services/PlanesService.h"
#undef private
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <iostream>
#include <limits>
#include <chrono>
#include <thread>

int main(int argc, char **argv)
{
    auto &svc = PlanesService::instance();
    svc._center_lat = 47.4247;
    svc._center_lon = 8.5470;
    svc._range_km = 30;
    PlaneState planes[PlanesService::MAX_PLANES];
    int count = 0;
    std::string error, provider;
    svc._home_lat = 47.4247;
    svc._home_lon = 8.5470;
    svc._home_range_km = 30;
    svc._configured = true;
    svc._fetch_in_progress = true;
    assert(svc.set_search_area(48, 9, 40));
    assert(svc.center_latitude() == 47.4247); // Worker coordinates stay stable.
    assert(svc.set_search_area(49, 10, 50)); // Latest request wins.
    svc._fetch_in_progress = false;
    svc.apply_search_area();
    assert(svc.center_latitude() == 49 && svc.range_km() == 50 && !svc.has_data());
    svc.reset_search_area();
    assert(svc.center_latitude() == 47.4247 && svc.range_km() == 30);
    assert(!svc.set_search_area(91, 0, 30));
    assert(!svc.set_search_area(0, 0, 0));
    assert(!svc.set_search_area(std::numeric_limits<double>::quiet_NaN(), 0, 30));
    // An old worker response must never repopulate the newly selected area.
    setenv("GADGET_OS_TUTORIAL_PATH", "test-fixture", 1);
    svc.prioritize_aircraft("first");
    assert(svc.start_async_fetch());
    svc.prioritize_aircraft("next");
    assert(svc._fetch_priority_icao=="first" && svc._priority_icao=="next");
    assert(svc.set_search_area(48, 9, 40));
    for (int attempt = 0; attempt < 300 && svc.is_fetching(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        svc.loop();
    }
    assert(!svc.is_fetching() && !svc.has_data());
    assert(svc.center_latitude() == 48 && svc.range_km() == 40);
    svc.reset_search_area();
    unsetenv("GADGET_OS_TUTORIAL_PATH");
    const std::string aircraft = R"({"hex":"4b1234","flight":"SWR123  ","lat":47.43,"lon":8.55,"alt_baro":3280.84,"gs":194.3844,"track":90})";
    for (const char *key : {"ac", "aircraft"}) {
        assert(svc.parse_payload(std::string("{\"") + key + "\":[" + aircraft + "]}", planes, count, error));
        assert(count == 1 && planes[0].callsign == "SWR123");
        assert(std::fabs(planes[0].altitude_m - 1000) < 0.1);
        assert(std::fabs(planes[0].velocity_ms - 100) < 0.1);
        assert(svc.parse_payload(std::string("{\"") + key + "\":[]}", planes, count, error));
        assert(count == 0);
    }
    assert(svc.parse_payload(R"({"states":[["4b1234","SWR123","Switzerland",0,0,8.55,47.43,1000,false,100,90]]})", planes, count, error));
    assert(count == 1);
    assert(svc.parse_payload(R"({"ac":[{"hex":"4b1234","lat":47.43,"lon":8.55}]})", planes, count, error));
    assert(count==1 && !std::isfinite(planes[0].altitude_m));
    assert(svc.parse_payload(R"({"states":[["4b1234","SWR123","Switzerland",0,0,8.55,47.43,null,false,100,90]]})", planes, count, error));
    assert(count==1 && !std::isfinite(planes[0].altitude_m));
    assert(svc.parse_payload(R"({"states":[["4b1234","SWR123","Switzerland",0,0,8.55,47.43,0,false,100,90]]})", planes, count, error));
    assert(count==1 && planes[0].altitude_m==0); // Sea level is known, not missing.
    assert(!svc.parse_payload(R"({"error":"unavailable"})", planes, count, error));
    assert(!svc.parse_payload("<html>unavailable</html>", planes, count, error));

    // Fake curl: first provider returns an unusable 200, second returns 403,
    // third returns adsb.fi's schema. No real network is used.
    unsetenv("GADGET_OS_TUTORIAL_PATH");
    assert(svc.fetch_aircraft(planes, count, provider, error));
    assert(provider == "adsb.fi" && count == 1 && error.empty());
    setenv("PLANES_TEST_EMPTY", "1", 1);
    assert(svc.fetch_aircraft(planes, count, provider, error));
    assert(provider == "adsb.lol" && count == 0 && error.empty());
    unsetenv("PLANES_TEST_EMPTY");
    setenv("PLANES_TEST_FAIL", "1", 1);
    assert(!svc.fetch_aircraft(planes, count, provider, error));
    assert(!error.empty());

    // The active aircraft gets the first lookup, even outside the nearest three.
    PlaneState routes[5];
    for (int i=0;i<5;++i) { routes[i].icao24="hex"+std::to_string(i); routes[i].callsign="TEST"+std::to_string(i); }
    svc._fetch_priority_icao="hex4";
    svc.enrich_routes(routes,5);
    assert(routes[4].route=="ZRH->EGLL" && routes[4].destination_airport_name=="Heathrow Airport");
    assert(routes[0].route=="ZRH->EGLL" && routes[1].route=="ZRH->EGLL");
    assert(routes[2].route.empty() && routes[3].route.empty());
    std::ifstream route_log(std::getenv("PLANES_ROUTE_LOG"));
    std::string first;std::getline(route_log,first);assert(first=="TEST4");
    // Cached active routes don't consume the remaining network budget.
    svc.enrich_routes(routes,5);assert(routes[2].route=="ZRH->EGLL" && routes[3].route=="ZRH->EGLL");

    if (argc > 1) {
        std::ifstream input(argv[1]);
        assert(input);
        std::string payload((std::istreambuf_iterator<char>(input)), {});
        assert(svc.parse_payload(payload, planes, count, error));
        std::cout << "Live response parsed: " << count << " nearby aircraft\n";
    }
    std::cout << "Plane provider regressions passed\n";
}
