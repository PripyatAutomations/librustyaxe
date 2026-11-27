//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(_maidenhead_h)
#define _maidenhead_h
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


typedef struct Coordinates {
   float latitude;
   float longitude;
   uint8_t precision;           // how many digits of precision
   // were given for the longest
   // of
   // lat/lon
   int error;
} Coordinates;


// Only FT4 and FT8 are supported by ft8_lib, but we can talk to ardop
typedef enum {
   TX_MODE_NONE = 0,
   TX_MODE_FT8,
   TX_MODE_FT4,
//     TX_MODE_JS8,
//     TX_MODE_PSK11,
//     TX_MODE_ARDOP_FEC,
   TX_MODE_END                  // invalid, end of list marker
} tx_mode_t;

extern double deg2rad(double deg);
extern double rad2deg(double rad);
extern Coordinates maidenhead2latlon(const char *locator);
extern const char *latlon2maidenhead(Coordinates *c);
extern double calculateBearing(double lat1, double lon1, double lat2, double lon2);
extern double calculateDistance(double lat1, double lon1, double lat2, double lon2);

/* Shared location formatting. Coordinate strings are "lat, lon" in decimal degrees; a valid coordinate fix wins over a Maidenhead locator. Grid-only locations
 * use the approximate center of that square. The resulting locator always has six characters. Output belongs to the caller. */
typedef struct {
   double latitude, longitude;
   bool valid, approximate;
   char locator[7];
} rr_location_t;
bool rr_location_resolve(const char *coordinates, const char *grid, rr_location_t *out);
bool rr_location_format(const char *coordinates, const char *grid, bool compact, char *out, size_t capacity);

#endif // !defined(_maidenhead_h)
