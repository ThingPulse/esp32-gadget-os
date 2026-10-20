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

int main(int argc, char **argv)
{
    auto &svc = PlanesService::instance();
    svc._center_lat = 47.4247;
    svc._center_lon = 8.5470;
    svc._range_km = 30;
    PlaneState planes[PlanesService::MAX_PLANES];
    int count = 0;
    std::string error, provider;
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

    if (argc > 1) {
        std::ifstream input(argv[1]);
        assert(input);
        std::string payload((std::istreambuf_iterator<char>(input)), {});
        assert(svc.parse_payload(payload, planes, count, error));
        std::cout << "Live response parsed: " << count << " nearby aircraft\n";
    }
    std::cout << "Plane provider regressions passed\n";
}
