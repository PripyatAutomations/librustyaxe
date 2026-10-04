// POSIX serial line settings shared by native-client and server transports.
#ifndef RUSTYAXE_IO_SERIAL_H
#define RUSTYAXE_IO_SERIAL_H
#include <stdbool.h>
#include <stddef.h>
#include <termios.h>
typedef struct rr_serial_settings {
   unsigned baud;
   unsigned bits, stops;
   char parity;
} rr_serial_settings_t;
bool rr_serial_config_register(void);
bool rr_serial_spec_parse(const char *spec, char *target, size_t capacity,
   rr_serial_settings_t *settings);
bool rr_nmea_valid(const char *line);
int rr_serial_pty_open(const char *path, const rr_serial_settings_t *settings,
   int *keeper, char *slave, size_t capacity);
void rr_serial_pty_close(int fd, int keeper, const char *path, const char *slave);
bool rr_serial_mode_parse(const char *mode, rr_serial_settings_t *settings);
void rr_serial_mode_format(const rr_serial_settings_t *settings, char mode[4]);
bool rr_serial_settings_read(int fd, rr_serial_settings_t *settings);
bool rr_serial_settings_apply(int fd, const rr_serial_settings_t *settings);
// Opens only a character TTY, nonblocking and raw, preserving settings for close.
int rr_serial_device_open(const char *path, const rr_serial_settings_t *settings,
   struct termios *original);
void rr_serial_device_close(int fd, const struct termios *original);
#endif
