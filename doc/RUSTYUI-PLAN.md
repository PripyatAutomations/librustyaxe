# RustyUI implementation plan and handoff

## Architecture decisions

- Keep librustyui in the librustyaxe repository but independently packaged.
- librustyaxe depends only on system libraries. librustyscript depends on
  librustyaxe, not IRC or UI. librustyui requires both Axe and Script.
- GTK dependencies belong only to optional graphical backends. Start with TUI,
  then GTK3; GTK4 is future work.
- Bundle MIT libtermkey in librustyui/vendor/libtermkey, preserve attribution
  and CHANGES.rrui; use system libtinfo/ncurses terminfo, not curses rendering.
- RustyScript describes creation, placement, sizing, properties and bindings;
  native code owns rendering, interaction, widget lifetime and application logic.
- Share functional forms, not graphical parity. Waterfalls, spectrum, patchbay
  and video need not have terminal equivalents.
- Use a single native form state and action interface for all renderers.
- Preserve existing working frontends until their replacements are tested.

## Current checkpoint (2026-11-24)

Branches: PripyatAutomations/librustyaxe:librustyui-foundation and
PripyatAutomations/rustyrig-fw:librustyui-foundation. Not merged.
RustyScript anonymous dictionary expressions also on Axe main.

- [x] TUI extraction into librustyui.
- [x] Vendored libtermkey separation and attribution (cc2060a).
- [x] Common layout model, mandatory scripting dependency, capability query,
  transactional script loading (b93f9b8).
- [x] Anonymous dict expressions and nested dict shorthand (5770ea0 on main,
  3481f5e on foundation).
- [x] User reports core, headless and TUI color/completion tests passed.
- [x] User reports shared layout, property blocks, backend selection and
  atomic reload test passed.
- [ ] Standalone new RustyScript regression suite and Debian packaging verified.
- [ ] Full RustyRig consumer migration and test harness linker flags verified.
- [ ] Interactive renderers implemented. Current model is layout-only.

## Phase 1: common interactive state, TUI first

- [ ] Owned form instance independent of layout lifetime; stable widget IDs.
- [ ] Native action callback with form state, widget ID and action name.
- [ ] Shared values, checkbox state and list items with stable keys and
  selection preserved across refresh; distinguish display text from identity.
- [ ] Focus traversal skips hidden/disabled controls and ancestors.
- [ ] UTF-8 entry editing, cursor movement, delete/backspace, max character
  count, masked display and horizontal scrolling.
- [ ] List navigation, viewport, selection, activation.
- [ ] Modal dialog handling, Escape cancellation, Enter activation, Tab and
  Shift-Tab; no leaked input into chat or PTT hotkeys.
- [ ] Terminal-size-aware renderer with clipping and scrollable viewport,
  keyboard and mouse hit testing; sanitize untrusted terminal text.
- [ ] Integrate with existing libtermkey decoder, GLib input loop and redraw
  infrastructure. Do not introduce a second terminal input loop.
- [ ] Tests for focus, text, selection, actions, bounds and lifecycle.

## Phase 2: first production consumer (RustyRig)

- [ ] Inspect current TUI/GTK server picker and profile store APIs.
- [ ] Shared server picker script and profile editor script: profile list,
  folders/status, connect/new/edit/delete/cancel, host/port, transport,
  identity and TLS options as supported by existing profile model.
- [ ] Use application-owned profile IDs and actions; keep networking,
  credential handling and profile persistence in RustyRig.
- [ ] Confirm destructive operations, keep password fields masked.
- [ ] Package default scripts under /usr/share/rustyrig/scripts/client/ui/.
  Resolve per file: user ~/.config/rustyrig/scripts/client/ui/ overrides
  administrator /etc/rustyrig/scripts/client/ui/ overrides packaged default.
- [ ] Transactional reload: keep last working model on error; recover default.
  Future /ui list, reload, edit, reset, inspect commands.
- [ ] Test actual TUI picker and profile editing before replacing legacy code.

## Phase 3: GTK3

- [ ] Optional backend consuming same form/layout state and action interface.
- [ ] Native GTK sizing, focus, accessibility, list/tree and dialog controls.
- [ ] Reuse same server picker/editor scripts, test functional parity.
- [ ] Extract reusable GTK components incrementally; preserve source licenses.
- [ ] Keep GTK out of core and TUI linkage.

## Build and release gates

Run make test, make -C librustyscript test, make -C librustyui test,
headless build WITH_UI=false, Debian binary package build, and RustyRig test
suite. Verify runtime dependencies with readelf and package metadata. Audit
consumer includes, test linker flags, SDK header installation and package
installation scripts before merging. Record actual test results, not assumptions.

## Script example

```c
ui.add(dict {
   kind = "dialog";
   id = "settings";
   label = "Server settings";
});
ui.add(dict {
   kind = "entry";
   id = "host";
   parent = "settings";
   label = "Hostname";
   width_chars = 30;
   max_length = 255;
});
```

The existing ui.add API consumes a dictionary snapshot. Native renderers,
live bindings and nested validation properties are not yet implemented.
Do not treat example syntax beyond the implemented API as shipped features.

## Handoff procedure

Update this document with each implementation milestone, relevant commit IDs,
tests actually run, unresolved defects, and the next concrete task. Keep this
document on the foundation branch alongside the implementation. The next task
is Phase 1 shared form state and nonblocking terminal renderer integration.

## Interactive TUI checkpoint

Implemented owned form snapshots, native actions, focus traversal, entry editing,
checkbox state, keyed list selection, and a nonblocking modal terminal renderer
using the existing input loop. Initial renderer uses vertical stacking (row/grid
geometry not yet implemented). Tests added; not yet run. Next: verify compile,
fix redraw and geometry edge cases, integrate actual RustyRig server picker.
