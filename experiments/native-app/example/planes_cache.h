/* Persistent airport layout shared only with the one-time firmware migration bridge.
 * Version 1 prefixes from the earlier 24-airport package remain readable. */
#include <stdint.h>
typedef struct {char code[9],name[97];double lat,lon;} PlanesAirport;
typedef struct {uint32_t magic,count;double lat,lon,range;PlanesAirport airports[64];} PlanesAirportCache;
