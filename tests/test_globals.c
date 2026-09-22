#include <time.h>

// librustyaxe expects the host application to provide its shared clock value.
// Standalone tests provide the symbol here when they do not define their own.
time_t now;
