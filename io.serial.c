//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#define	_GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <librustyaxe/core.h>
#include <librustyaxe/io.serial.h>

static bool baud_speed(unsigned baud, speed_t *speed) {
#define	RATE(n) case n: *speed = B ## n; return true

   switch (baud) {
   RATE(0);
   RATE(1200);
   RATE(2400);
   RATE(4800);
   RATE(9600);
   RATE(19200);
   RATE(38400);
   RATE(57600);
   RATE(115200);
#ifdef B230400
   RATE(230400);
#endif
#ifdef B460800
   RATE(460800);
#endif
#ifdef B921600
   RATE(921600);
#endif
      default: {
         errno = EINVAL;
         return false;
      }
   }
#undef RATE
}
bool rr_serial_mode_parse(const char *mode, rr_serial_settings_t *s) {
   if ( !mode || !s || strlen(mode) != 3 || mode[0] < '5' || mode[0] > '8' ||
        (mode[2] != '1' && mode[2] != '2') ) {
      return false;
   }
   char parity = tolower( (unsigned char)mode[1] );

   if (parity != 'n' && parity != 'e' && parity != 'o') {
      return false;
   }
   s->bits = mode[0] - '0';
   s->parity = parity;
   s->stops = mode[2] - '0';

   return true;
}
void rr_serial_mode_format(const rr_serial_settings_t *s, char mode[4]) {
   mode[0] = '0' + s->bits;
   mode[1] = s->parity;
   mode[2] = '0' + s->stops;
   mode[3] = '\0';
}
bool rr_serial_settings_apply(int fd, const rr_serial_settings_t *s) {
   struct termios settings;
   speed_t rate;

   if ( !s || s->bits < 5 || s->bits > 8 || s->stops < 1 || s->stops > 2 ||
        (s->parity != 'n' && s->parity != 'e' && s->parity != 'o') || !baud_speed(s->baud, &rate) || tcgetattr(fd,
           &settings) ) {
      return false;
   }
   char mode[4];
   rr_serial_mode_format(s, mode);
   rr_serial_settings_t validated = *s;

   if ( !rr_serial_mode_parse(mode, &validated) ) {
      errno = EINVAL;
      return false;
   }
   cfmakeraw(&settings);
   settings.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
#ifdef CRTSCTS
   settings.c_cflag &= ~CRTSCTS;
#endif
   static const tcflag_t bits[] = {
      CS5, CS6, CS7, CS8
   };
   settings.c_cflag |= bits[s->bits - 5] | CLOCAL | CREAD;

   if (s->parity != 'n') {
      settings.c_cflag |= PARENB;
   }

   if (s->parity == 'o') {
      settings.c_cflag |= PARODD;
   }

   if (s->stops == 2) {
      settings.c_cflag |= CSTOPB;
   }
   settings.c_cc[VMIN] = 1;
   settings.c_cc[VTIME] = 0;

   return !cfsetispeed(&settings, rate) && !cfsetospeed(&settings, rate) &&
          !tcsetattr(fd, TCSANOW, &settings);
}
bool rr_serial_settings_read(int fd, rr_serial_settings_t *s) {
   struct termios settings;

   if ( !s || tcgetattr(fd, &settings) ) {
      return false;
   }
   speed_t rate = cfgetospeed(&settings);
   const unsigned rates[] = {
      0, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600
   };
   bool found = false;

   for (unsigned i = 0 ; i < sizeof(rates) / sizeof(rates[0]) ; i++) {
      speed_t test;

      if (baud_speed(rates[i], &test) && test == rate) {
         s->baud = rates[i];
         found = true;
         break;
      }
   }

   if (!found) {
      return false;
   }

   switch (settings.c_cflag & CSIZE) {
      case CS5: {
         s->bits = 5;
         break;
      }
      case CS6: {
         s->bits = 6;
         break;
      }
      case CS7: {
         s->bits = 7;
         break;
      }
      default: {
         s->bits = 8;
         break;
      }
   }
   s->parity = settings.c_cflag & PARENB ? (settings.c_cflag & PARODD ? 'o' : 'e') : 'n';
   s->stops = settings.c_cflag & CSTOPB ? 2 : 1;

   return true;
}
int rr_serial_device_open(const char *path, const rr_serial_settings_t *settings, struct termios *original) {
   if (!path || !original) {
      errno = EINVAL;
      return -1;
   }
   int fd = open(path, O_RDWR | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);

   if (fd < 0) {
      return -1;
   }
   struct stat status;

   if ( fstat(fd, &status) || !S_ISCHR(status.st_mode) || tcgetattr(fd, original) ) {
      close(fd);
      errno = ENOTTY;
      return -1;
   }

   if ( !rr_serial_settings_apply(fd, settings) ) {
      int error = errno;
      tcsetattr(fd, TCSANOW, original);
      close(fd);
      errno = error;
      return -1;
   }

   return fd;
}
void rr_serial_device_close(int fd, const struct termios *original) {
   if (fd < 0) {
      return;
   }

   if (original) {
      tcsetattr(fd, TCSANOW, original);
   }
   close(fd);
}

bool rr_serial_spec_parse(const char *spec, char *target, size_t capacity, rr_serial_settings_t *settings) {
   if (!spec || !target || !capacity || !settings) {
      return false;
   }
   const char *at = strrchr(spec, '@');
   size_t len = at ? (size_t)(at - spec) : strlen(spec);

   if (!len || len >= capacity) {
      return false;
   }
   rr_serial_settings_t parsed = *settings;

   if (at) {
      const char *tail = at + 1;

      if ( !rr_serial_mode_parse(tail, &parsed) ) {
         char *end = NULL;
         errno = 0;
         unsigned long baud = strtoul(tail, &end, 10);

         if ( errno || end == tail || baud > 921600 || (*end && *end != ',') ) {
            return false;
         }
         parsed.baud = baud;

         if ( *end == ',' && !rr_serial_mode_parse(end + 1, &parsed) ) {
            return false;
         }
      }
   }
   speed_t rate;

   if ( !baud_speed(parsed.baud, &rate) ) {
      return false;
   }
   memcpy(target, spec, len);
   target[len] = '\0';
   *settings = parsed;
   return true;
}
bool rr_nmea_valid(const char *line) {
   if ( !line || (line[0] != '$' && line[0] != '!') ) {
      return false;
   }
   const char *end = strchr(line, '*');

   if ( !end || end == line + 1 || strlen(end) != 3 || !isxdigit( (unsigned char)end[1] ) || !isxdigit( (unsigned char)end[2] ) ) {
      return false;
   }
   unsigned checksum = 0;

   for (const char *p = line + 1 ; p < end ; p++) {
      if ( (unsigned char)*p < 32 || (unsigned char)*p > 126 ) {
         return false;
      }
      checksum ^= (unsigned char)*p;
   }

   return checksum == strtoul(end + 1, NULL, 16);
}
static void nmea_angle(int32_t angle, unsigned width, char *out, size_t capacity) {
   uint32_t absolute = angle < 0 ? -(int64_t)angle : angle;
   unsigned degrees = absolute / 10000000;
   uint64_t minutes = ( (uint64_t)(absolute % 10000000) * 60 + 5) / 10;

   if (minutes == 60000000) {
      degrees++;
      minutes = 0;
   }
   snprintf( out, capacity, "%0*u%02u.%06u", width, degrees, (unsigned)(minutes / 1000000),
      (unsigned)(minutes % 1000000) );
}
size_t rr_nmea_rmc(int32_t latitude, int32_t longitude, uint8_t flags, time_t utc, char *out, size_t capacity) {
   if (!out || capacity < 8 || latitude < -900000000 || latitude > 900000000 ||
       longitude < -1800000000 || longitude > 1800000000 || (flags & ~3) ) {
      return 0;
   }
   struct tm nmea_tm;

   if (!gmtime_r(&utc, &nmea_tm) ) {
      return 0;
   }
   char time_text[16], date_text[16], lat[24], lon[24];

   if (!strftime(time_text, sizeof(time_text), "%H%M%S", &nmea_tm) ||
       !strftime(date_text, sizeof(date_text), "%d%m%y", &nmea_tm) ) {
      return 0;
   }
   int len;

   if (flags & 1) {
      nmea_angle( latitude, 2, lat, sizeof(lat) );
      nmea_angle( longitude, 3, lon, sizeof(lon) );
      len = snprintf(out, capacity, "$GPRMC,%s,A,%s,%c,%s,%c,0.0,,%s,,,%c", time_text, lat, latitude < 0 ? 'S' : 'N',
         lon, longitude < 0 ? 'W' : 'E', date_text, flags & 2 ? 'M' : 'A');
   } else {
      len = snprintf(out, capacity, "$GPRMC,%s,V,,,,,0.0,,%s,,,N", time_text, date_text);
   }

   if (len < 0 || (size_t)len + 4 > capacity) {
      return 0;
   }
   unsigned nmea_checksum = 0;

   for (int i = 1 ; i < len ; i++) {
      nmea_checksum ^= (unsigned char)out[i];
   }

   int suffix = snprintf(out + len, capacity - (size_t)len, "*%02X", nmea_checksum);

   return suffix == 3 ? (size_t)len + (size_t)suffix : 0;
}

int rr_serial_pty_open(const char *path, const rr_serial_settings_t *settings, int *keeper, char *slave,
                       size_t capacity) {
   if (!path || !keeper || !slave || capacity < 2) {
      errno = EINVAL;
      return -1;
   }
   int fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC), hold = -1;

   if (fd < 0) {
      return -1;
   }

   if ( grantpt(fd) || unlockpt(fd) || ptsname_r(fd, slave, capacity) ) {
      goto failed;
   }
   hold = open(slave, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);

   if ( hold < 0 || !rr_serial_settings_apply(hold, settings) || symlink(slave, path) ) {
      goto failed;
   }
   *keeper = hold;
   return fd;
failed: {
      int error = errno;

      if (hold >= 0) {
         close(hold);
      }
      close(fd);
      errno = error;
      return -1;
   }
}
void rr_serial_pty_close(int fd, int keeper, const char *path, const char *slave) {
   if (fd >= 0) {
      close(fd);
   }

   if (keeper >= 0) {
      close(keeper);
   }

   if (path && slave) {
      char target[PATH_MAX];
      ssize_t len = readlink(path, target, sizeof(target) - 1);

      if (len >= 0) {
         target[len] = '\0';

         if ( !strcmp(target, slave) ) {
            unlink(path);
         }
      }
   }
}

/* The loader dispatches named sections to their component's callback. The dotted form
 * also accepts cfg_save()'s [serial] NAME.option representation. */
static bool serial_config(const char *path, int line, const char *section, const char *buf) {
   char *copy = strdup(buf);

   if (!copy) {
      return true;
   }
   char *value = strchr(copy, '=');
   bool failed = true;

   if (value) {
      *value++ = '\0';
      char *key = copy;
      while ( isspace( (unsigned char)*key ) ) {
         key++;
      }
      char *end = key + strlen(key);
      while ( end > key && isspace( (unsigned char)end[-1] ) ) {
         *--end = '\0';
      }
      while ( isspace( (unsigned char)*value ) ) {
         value++;
      }
      end = value + strlen(value);
      while ( end > value && isspace( (unsigned char)end[-1] ) ) {
         *--end = '\0';
      }
      char full[256];
      int len = snprintf(full, sizeof(full), "%s.%s", section, key);

      if ( !strcmp(section, "serial") && strchr(key, '.') ) {
         full[6] = ':';
      }

      if ( *key && len > 0 && (size_t)len < sizeof(full) ) {
         failed = dict_add(cfg, full, value) != 0;
      }
   }

   if (failed) {
      Log(LOG_WARN, "cfg.serial", "Invalid [%s] setting at %s:%d", section, path, line);
   }
   free(copy);

   return failed;
}
bool rr_serial_config_register(void) {
   return cfg_add_callback(NULL, "serial", serial_config) &&
          cfg_add_callback(NULL, "serial:*", serial_config);
}
