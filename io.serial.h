//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// POSIX serial line settings shared by native-client and server transports.
#ifndef RUSTYAXE_IO_SERIAL_H
#define	RUSTYAXE_IO_SERIAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <termios.h>
typedef struct rr_serial_settings {
   unsigned baud;
   unsigned bits, stops;
   char parity;
} rr_serial_settings_t;
bool rr_serial_config_register(void);
bool rr_serial_spec_parse(const char *spec, char *target, size_t capacity, rr_serial_settings_t *settings);
bool rr_nmea_valid(const char *line);
// Format signed decimal-degree coordinates (degrees * 1e7) as a checksum-correct
// GPRMC sentence without CRLF. Flags are bit 0 valid and bit 1 manual.
size_t rr_nmea_rmc(int32_t latitude, int32_t longitude, uint8_t flags, time_t utc,
                   char *out, size_t capacity);
int rr_serial_pty_open(const char *path, const rr_serial_settings_t *settings, int *keeper, char *slave,
                       size_t capacity);
void rr_serial_pty_close(int fd, int keeper, const char *path, const char *slave);
bool rr_serial_mode_parse(const char *mode, rr_serial_settings_t *settings);
void rr_serial_mode_format(const rr_serial_settings_t *settings, char mode[4]);
bool rr_serial_settings_read(int fd, rr_serial_settings_t *settings);
bool rr_serial_settings_apply(int fd, const rr_serial_settings_t *settings);
// Opens only a character TTY, nonblocking and raw, preserving settings for close.
int rr_serial_device_open(const char *path, const rr_serial_settings_t *settings, struct termios *original);
void rr_serial_device_close(int fd, const struct termios *original);
#endif
