#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <librustyaxe/core.h>
#include <librustyaxe/io.serial.h>
int main(void) {
   char sentence[128];
   assert(rr_nmea_rmc(381234567, -807654321, 3, 1791288000, sentence, sizeof(sentence)));
   assert(rr_nmea_valid(sentence));
   assert(strstr(sentence, ",A,3807.407402,N,08045.925926,W,") && strstr(sentence, ",M*"));
   assert(rr_nmea_rmc(-900000000, 1800000000, 1, 0, sentence, sizeof(sentence)));
   assert(rr_nmea_valid(sentence));
   assert(strstr(sentence, ",9000.000000,S,18000.000000,E,"));
   assert(!rr_nmea_rmc(900000001, 0, 1, 0, sentence, sizeof(sentence)));
   assert(!rr_nmea_rmc(0, -1800000001, 1, 0, sentence, sizeof(sentence)));
   assert(!rr_nmea_rmc(0, 0, 4, 0, sentence, sizeof(sentence)));
   assert(!rr_nmea_rmc(0, 0, 1, 0, sentence, 8));
   assert(rr_nmea_rmc(0, 0, 0, 0, sentence, sizeof(sentence)));
   assert(rr_nmea_valid(sentence) && strstr(sentence, ",V,,,,,") && strstr(sentence, ",N*"));
   char path[] = "/tmp/rr-serial-config-XXXXXX";
   int fd = mkstemp(path);
   assert(fd >= 0);
   FILE *f = fdopen(fd, "w");
   assert(f);
   fputs("[serial]\nttyHOST0=serial:/dev/ttyUSB0@115200,8n1\nttyGPS0=rig0.gps-in\n"
      "[serial:ttyHOST0]\nbuffer-bytes=0\n[serial:ttyGPS0]\ntype=serial\npath=/dev/ttyUSB1\n", f);
   fclose(f);
   default_cfg = dict_new();
   assert(default_cfg);
   assert(rr_serial_config_register());
   cfg = cfg_load(path);
   assert(cfg);
   assert(!strcmp(cfg_get("serial.ttyHOST0"), "serial:/dev/ttyUSB0@115200,8n1"));
   assert(!strcmp(cfg_get("serial:ttyGPS0.path"), "/dev/ttyUSB1"));
   assert(cfg_get_int("serial:ttyHOST0.buffer-bytes", 99) == 0);
   assert(cfg_save(cfg, path));
   dict *saved = cfg;
   cfg = cfg_load(path);
   assert(cfg);
   assert(!strcmp(cfg_get("serial.ttyHOST0"), "serial:/dev/ttyUSB0@115200,8n1"));
   assert(!strcmp(cfg_get("serial:ttyGPS0.path"), "/dev/ttyUSB1"));
   assert(cfg_get_int("serial:ttyHOST0.buffer-bytes", 99) == 0);
   dict_free(saved);
   cfg_fini();
   unlink(path);
   puts("PASS: serial section loading and saved configuration round trip");

   return 0;
}
