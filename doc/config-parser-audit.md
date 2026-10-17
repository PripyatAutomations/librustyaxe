# Config parser audit (October 2026)

## Fixed in config.c

- C-style `/* ... */` comments may start/end mid-line, span physical
  lines, or occur multiple times on one line.
- Literal comment delimiters inside single/double quotes are preserved.
- Entirely stripped/blank lines are skipped before trailing-pointer work.
- Section names no longer suffer an off-by-one snprintf truncation;
  oversized/empty section names produce a diagnostic.
- `cfg_load()` closes its input stream after parsing.
- Removed unconditional stderr pointer dumps and CRIT logging from
  the normal config callback dispatch path.

## Further correctness work (not part of the comment patch)

- Refactor logical-line assembly so an overlong physical line or continuation
  cannot silently truncate values; detect and report the condition.
- Define whether inline # / ; / // are comments or literal value data.
  Currently they are whole-line markers only. Keep URLs and paths safe.
- Clearly define and test quoting and escapes for values; current parser
  keeps quotes as part of values.
- Replace ad-hoc section dispatch with tested key/section mapping while
  preserving registered callback behavior.
- Add integration tests for [general], [server:name], dynamically registered
  sections and backslash continuations, including a multi-line comment inside
  a continued value.
- `cfg_dispatch_callback()` currently returns false on all paths; make its
  return contract explicit before relying on 'unknown section' diagnostics.
- Audit `cfg_set_defaults()` and other inverted bool success conventions.
- `cfg_get_bool/int/uint()` currently use `cfg` directly, unlike `cfg_get()`
  which falls back to `default_cfg`. Unify typed default behavior.
- Audit allocation and ownership in `cfg_detect_and_load()`,
  `cfg_apply_new()`, `dict_diff()` and callback/reload-list removal.
- Avoid saving a merged generated config that discards comments; retain an
  override-only output or explicitly document generated files.

## Configuration storage strategy

Keep the parser and typed configuration model independent of SQLite.
Text files and compiled defaults remain first-class for ESP32 and other
resource-constrained deployments. On larger systems SQLite can optionally
store mutable operator/runtime state, accounting, users, and per-instance
settings. Database-backed values should be one optional adapter with clear
precedence and persistence rules, not an unconditional dependency of core
configuration parsing.

Suggested precedence (future proposal, not implemented):
compiled defaults < system config < optional persistent runtime overrides <
explicit command-line overrides. Expose the resolved origin of each value
for debugging. Do not migrate secrets or account permissions automatically.
