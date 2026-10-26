# librustyaxe public C API (1.0 draft)

librustyaxe supplies shared utilities for RustyRig and other applications: typed
dictionaries, configuration, JSON conversion, events, logging, modules, and I/O.
Public entry points are declared in the installed `librustyaxe/*.h` headers;
this guide is a starting point, not a replacement for those declarations.

## Return conventions

* Operational `bool` functions should return **true on success**, false on
  failure. Predicates instead return the truth of the named condition.
* Dictionary mutation functions return **0 on success**, nonzero on failure.
  Enumeration returns the next cursor, or -1 when finished.
* Allocating functions return NULL on failure unless documented otherwise.
* OS-style functions returning descriptors use -1 on failure.
* `cfg_save_cb_t` callbacks return true on success, false on failure,
  consistent with section-load callbacks and callback registration.

## Dictionaries — `dict.h`

Create with `dict_new()`, destroy with `dict_free()`.
`dict_add*` copies keys; string values are also duplicated. Stored `VAL_PTR`
values are borrowed pointers, not deep-copied objects.
Use `dict_add_int`, `dict_add_bool`, `dict_add_double`, etc. when a value
has a native type. Use `dict_get_type` or `dict_enumerate_typed` to examine
types. `dict_get()` and `dict_enumerate()` are string-only compatibility
functions: they cannot represent non-string values faithfully.
`dict_merge(dst, src)` overwrites matching keys and preserves stored types.
`dict_clone()` and `dict_merge_new()` return independently freeable dictionaries;
pointer payloads remain borrowed. Borrowed keys/values from lookups and enumeration
must not be freed and should not be retained across mutations.

```c
dict *d = dict_new();
if (!d || dict_add_int(d, "port", 8420) != 0 ||
          dict_add_bool(d, "enabled", true) != 0) {
   dict_free(d);
   return false;
}
int port = dict_get_int(d, "port", 0);
dict_free(d);
```

## Configuration — `config.h`

`cfg_load(path)` returns a dictionary owned by the caller, or NULL on failure.
The process-wide `cfg` dictionary is managed by the configuration subsystem.
`cfg_reload()` updates the live configuration, and `cfg_fini()` tears down
process-wide config state. Avoid keeping dictionary entry pointers across reloads.
`cfg_get_path()` returns allocated memory to free. Other accessors should
be treated as borrowed data unless their declaration explicitly says otherwise.

`[general]` keys share one flat namespace, even across `.include` and
`!include` files; subsequent assignments override earlier entries.
Section parsers registered with `cfg_add_callback()` return true when a line
was handled successfully. Include directives inside comments are ignored.
Explicitly typed values inserted by callbacks survive dictionary merging.

## Events — `event-bus.h`

Prefer `event_on_token()` or `event_on_binary_token()` and retain the returned
registration token. Call `event_off_token()` before destroying the user
context or unloading the module that owns the handler. Dispatch variants
allow invoking handlers via an application-supplied dispatcher; review that
dispatcher's lifetime and thread rules separately. The callback's data and
user pointer are not implicitly owned by the event bus.

## Modules — `module.h`

`rr_load_module()` and `rr_unload_module()` return true on success.
A module exports `bool rr_module_init(void)` (true = success) and optionally
`void rr_module_shutdown(void)`. It may export
`bool rr_module_can_unload(void)` (true = safe to unload). A module must
remove callbacks, event subscriptions, timers, and other references into its
code before `dlclose()`. Module implementations must match the loader's ABI.

## JSON, logging, and serial I/O

`dict2json()` returns a string to free; `json2dict()` returns a dictionary to
free. JSON is a serialization boundary, not a reason to replace typed dictionary
entries with strings. `Log()` uses a subsystem and priority;
`log_add_callback_token()` provides a registration handle for explicit removal.
Serial parsing/settings operations return true on success, but device-opening
functions return an OS descriptor or -1.

## API review notes before tagging 1.0

* Prefer typed callback declarations instead of old-style `bool (*)()`.
* Review global-singleton initialization, thread affinity, and pointer lifetimes
  before promising reentrant or thread-safe behavior to plugin authors.
* Add failure-path tests for callbacks, module unload, and configuration reload.

The current library is **not documented as globally thread-safe**. Callbacks that
touch GTK/GLib or process-global configuration should execute in the appropriate
application context.

## Logging: verbosity and rate limiting

The existing `log.level` configuration accepts comma- or whitespace-separated
`subsystem:level` rules, optionally followed by `@count/interval`:

```ini
log.level = *:info,pcm.hub:debug@1/30s,ws.media:crazy@5/s,bridge.*:debug@10/m
```

The optional suffix admits up to `count` messages per monotonic time window.
Intervals accept seconds (`s`), minutes (`m`), or hours (`h`), such as
`1/30s`, `10/m`, and `60/h`. Rules without `@` have no rate limit.
Rate counters are independent for each subsystem and priority. The most
specific matching filter controls both verbosity and rate, as with existing
filter precedence. On the next admitted message after a window with
suppression, the logger adds the suppressed-message count.

`LOG_AUDIT` and `LOG_CRIT` bypass the rate limiter. Rate limiting only affects logfile/console emission. All messages still reach
registered callbacks, including the audit database and UI event consumers,
even when verbosity filters or rate limits hide their console output. If rate tracking fills
its bounded 256-subsystem/severity table or the monotonic clock fails, logging
fails open rather than silently discarding messages.

Rate limiting is not a substitute for assigning appropriate severities:
frequent polling belongs at `LOG_CRAZY`, periodic diagnostics at
`LOG_DEBUG`, and meaningful transitions at `LOG_INFO`.
