//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Utility functions for dealing with maidenhead coordinates and translating to/from WGS-84.
 */
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <librustyaxe/maidenhead.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

#define RADIUS_EARTH 6371.0     // Earth's radius in kilometers

double rad2deg(double rad) {
   return(rad * 180 / M_PI);
}

// from SP6Q's code
static char *complete_mh(const char *locator) {
   static char locator2[11],    // our scratch storage
      locator2_init[11] = "LL55LL55LL"; // Contains an initial
   // value reloaded into
   // locator

   int len = strlen(locator);

   if (len > 10 || len < 4) {
      Log(LOG_CRIT, "librustyaxe",
         "complete_mh: grid square must be between 4 and 10 digits. More digits provides more accuracy. Current length: %d. Returning NULL!", len);

      return NULL;
   }

   // Alert that we got an odd length string
   if (len % 2 != 0) {
      Log(LOG_CRIT, "librustyaxe",
         "complete_mh: grid squares must contain an even number of digits. Your request of %d digits was invalid, returning NULL!\n\n", len);

      return NULL;
   }
   // always 'zero' strings.. This will ensure we get a full 10 digits and always clean...
   memcpy(locator2, locator2_init, 11);

   // This copies the locator, leaving the padding (from LL55LL55LL) in place to
   // get center of the gridsquare
   memcpy(locator2, locator, len);

   return locator2;
}

// BUG: This isn't safe...
// XXX: Sort this out ASAP
static char letterize(int x) {
   return (char)x + 65;
}

Coordinates maidenhead2latlon(const char *locator) {
   const char *lp = locator;    // pointer to locator
   // we'll use
   Coordinates c = {
      .latitude = 0, .longitude = 0, .precision = 0, .error = true
   };
   double field, square, subsquare, extsquare, precsquare;

   if (locator == NULL) {
      return c;
   }
   int len = strlen(locator);

   // If invalid grid square, return 0,0 to indicate it
   if (len > 10 || len < 4) {
      return c;
   }

   // if grid square is odd length, return error
   if ((len % 2) != 0) {
      Log(LOG_CRIT, "librustyaxe", "maidenhead2latlon: grid squares must be 4-10 digits (A-Z, 0-9) long and even length.");

      return c;
   }

   // if the grid square is less than 10 digits, pad it to the middle of squares
   // (LL55)
   if (len < 10) {
      if ((lp = complete_mh(locator)) == NULL) {
         // Invalid (uneven length?) grid square passed
         Log(LOG_CRIT, "librustyaxe", "maidenhead2latlon: grid squares must be 4-10 digits (A-Z, 0-9) long and even length.\n");

         return c;
      }
   }
   // set the precision so we have appropriate decimal places
   c.precision = (strlen(locator) / 2);

   // calculate latitude
   field = (toupper(lp[1]) - 'A') * 10.0;
   square = (lp[3] - '0') * 1.0;
   subsquare = (toupper(lp[5]) - 'A') / 24.0;
   extsquare = (lp[7] - '0') / 240.0;
   precsquare = (toupper(lp[9]) - 'A') / 5760.0;
   c.latitude = field + square + subsquare + extsquare + precsquare - 90;

   // calculate longitude
   field = (toupper(lp[0]) - 'A') * 20.0;
   square = (lp[2] - '0') * 2.0;
   subsquare = (toupper(lp[4]) - 'A') / 12.0;
   extsquare = (lp[6] - '0') / 120.0;
   precsquare = (toupper(lp[8]) - 'A') / 2880.0;
   c.longitude = (field + square + subsquare + extsquare + precsquare - 180);
   c.error = 0;

   return c;
}

// you should free this!
const char *latlon2maidenhead(Coordinates *c) {
   // These contain the precision in degrees of each pair (1 to 5 in a 2-10
   // digit grid square)
   double LON_F[] = {
      20, 2.0, 0.083333, 0.008333, 0.0003472083333333333
   };
   double LAT_F[] = {
      10, 1.0, 0.0416665, 0.004166, 0.0001735833333333333
   };
   int i;
   double lat = c->latitude;
   double lon = c->longitude;
   int size = 4;
   lon += 180;
   lat += 90;

   // minimum of 4, maximum of 10 digits
   if (c->precision > 2 && c->precision <= 5) {
      size = c->precision * 2;
   } else if (c->precision >= 5) {
      size = 10;
   } else {
      size = 4;
   }
   // make it even due to rounding
   size /= 2;
   size *= 2;

   // limit to 10
   if (size > 10) {
      size = 10;
   }
   // buffer
   static char locator[11];

   for (i = 0 ; i < size / 2 ; i++) {
      if (i % 2 == 1) {
         locator[i * 2] = (char)(lon / LON_F[i] + '0');
         locator[i * 2 + 1] = (char)(lat / LAT_F[i] + '0');
      } else {
         locator[i * 2] = letterize((int)(lon / LON_F[i]));
         locator[i * 2 + 1] = letterize((int)(lat / LAT_F[i]));
      }
      lon = fmod(lon, LON_F[i]);
      lat = fmod(lat, LAT_F[i]);
   }

   locator[i * 2] = 0;

   return locator;
}

double deg2rad(double degrees) {
   return degrees * M_PI / 180.0;
}

double calculateBearing(double lat1, double lon1, double lat2, double lon2) {
   double dLon = deg2rad(lon2 - lon1);
   double y = sin(dLon) * cos(deg2rad(lat2));
   double x = cos(deg2rad(lat1)) * sin(deg2rad(lat2)) - sin(deg2rad(lat1)) * cos(deg2rad(lat2)) * cos(dLon);
   double bearing = atan2(y, x);
   bearing = fmod(bearing + 2 * M_PI, 2 * M_PI);   // Convert to positive value
   bearing = bearing * 180.0 / M_PI;   // Convert to degrees

   return bearing;
}

double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
   double dLat = deg2rad(lat2 - lat1);
   double dLon = deg2rad(lon2 - lon1);
   double a = sin(dLat / 2) * sin(dLat / 2) + cos(deg2rad(lat1)) * cos(deg2rad(lat2)) * sin(dLon / 2) * sin(dLon / 2);
   double c = 2 * atan2(sqrt(a), sqrt(1 - a));
   double distance = RADIUS_EARTH * c;

   return distance;
}

/* Validate before invoking the legacy converter, which assumes valid pairs. */
static bool rr_grid_valid(const char *grid) {
   if (!grid) {
      return false;
   }
   size_t n = strlen(grid);

   if (n != 4 && n != 6 && n != 8 && n != 10) {
      return false;
   }

   for (size_t i = 0 ; i < n ; ++i) {
      unsigned char c = (unsigned char)toupper((unsigned char)grid[i]);
      bool letter = ((i / 2) % 2 == 0);

      if (letter) {
         unsigned limit = i < 2 ? 18 : 24;

         if (c < 'A' || c >= 'A' + limit) {
            return false;
         }
      } else if (c < '0' || c > '9') {
         return false;
      }
   }

   return true;
}
bool rr_location_resolve(const char *coordinates, const char *grid, rr_location_t *out) {
   if (!out) {
      return false;
   }
   memset(out, 0, sizeof(*out));
   double lat = 0, lon = 0;
   char extra;
   bool measured = coordinates && sscanf(coordinates, " %lf , %lf %c", &lat, &lon, &extra) == 2 &&
      isfinite(lat) && isfinite(lon) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;

   if (!measured) {
      if (!rr_grid_valid(grid)) {
         return false;
      }
      Coordinates center = maidenhead2latlon(grid);

      if (center.error) {
         return false;
      }
      lat = center.latitude;
      lon = center.longitude;
   }
   /* The existing converter returns a static buffer; copy before returning. */
   Coordinates location = {
      .latitude = lat, .longitude = lon, .precision = 3, .error = 0
   };
   const char *locator = latlon2maidenhead(&location);

   if (!locator || strlen(locator) != 6) {
      return false;
   }
   out->latitude = lat;
   out->longitude = lon;
   out->approximate = !measured;
   memcpy(out->locator, locator, 7);
   out->valid = true;

   return true;
}
bool rr_location_format(const char *coordinates, const char *grid, bool compact, char *out, size_t capacity) {
   if (!out || !capacity) {
      return false;
   }
   out[0] = 0;
   rr_location_t location;

   if (!rr_location_resolve(coordinates, grid, &location)) {
      return false;
   }
   int n = compact ? snprintf(out, capacity, "%s(%.2f, %.2f) [%s]", location.approximate ? "~" : "", location.latitude, location.longitude, location.locator) :
      snprintf(out, capacity, "%sCoordinates: %.6f, %.6f · Grid square: %s", location.approximate ? "Approximate " : "", location.latitude, location.longitude,
      location.locator);

   return n >= 0 && (size_t)n < capacity;
}
