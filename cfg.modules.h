//
// librustyaxe/cfg.modules.h: [modules] config section
//    This is part of rustyray-fw / rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__librustyaxe_cfg_modules_h)
#define __librustyaxe_cfg_modules_h

// Install the [modules] section callback (call once before cfg_load()).
extern bool cfg_modules_init(void);

// Enumerate configured modules; index is 0-based. Returns the module name
// and (optionally) its options string ("" when none), or NULL when
// exhausted.
extern const char *cfg_modules_get(int index, const char **options_out);

// Convenience: read the options configured for a specific module name.
// Returns NULL if the module is not configured.
extern const char *cfg_modules_options(const char *name);

#endif // !defined(__librustyaxe_cfg_modules_h)
