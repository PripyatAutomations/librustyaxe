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

## Phase 1b: terminal workspace and floating panels

Goal: functional terminal workspace comparable to the GTK workspace, not a
pixel-level recreation. Chat remains available alongside VFO, audio and other
controls. Extend the common RustyUI form model rather than embedding RustyRig
business logic in the terminal library.

- [ ] Add nonmodal panel instances with stable IDs, cell rectangles, visibility,
  z-order, focus, minimum dimensions, move/resize and optional collapse.
- [ ] Draw a clipped character-cell workspace and composite panels back-to-front;
  preserve the chat viewport and prevent ANSI escapes from untrusted text.
- [ ] Mouse title-bar dragging, resize edges, raise-on-click and close/collapse
  controls; keyboard-only panel selection, move and resize as alternatives.
- [ ] Modal forms take input priority; focused nonmodal panels consume only their
  own input. Explicit PTT safety: no accidental transmit while editing fields.
- [ ] Support docked left/right/top/bottom panels, floating panels and a chat
  center region. On narrow terminals collapse or hide optional panels rather
  than overlap controls or shrink chat below its minimum useful size.
- [ ] Persist per-workspace geometry and visibility through host-owned storage;
  clamp restored positions to current terminal size and recover after resize.
- [ ] Share scripts, IDs, bindings and actions with GTK. Cell sizes are the TUI
  backend's interpretation of layout; allow backend-specific overrides.
- [ ] Add reusable numeric frequency entry, mode/width selector, compact sliders,
  checkboxes, status indicators and simple signal/level meters. No waterfalls,
  graphical spectrum, patchbay or video required.
- [ ] First workspace demonstration: chat + VFO + audio controls + station
  status, with host mock values; then integrate live RustyRig actions.
- [ ] Test z-order, clipping, hit testing, focus, resize, persistence and modal
  priority; verify SSH and keyboard-only use.

Implementation order: stabilize the existing modal form renderer and complete
server picker integration first, then extract a cell-rectangle compositor and
introduce nonmodal floating panels. Do not create another terminal input loop.
The initial modal renderer is commit f516f21; it is not yet compiled/tested by
this session. Floating panels are planned, not implemented.

### Workspace prototype checkpoint

Implemented nonmodal panel registration, cell geometry, clipped overlay drawing,
z-order, mouse focus/raise and title drag/resize, Ctrl+arrow and Ctrl+keypad
spatial selection, keypad 5/Escape chat focus, modal priority. Terminals may
not report modified keypad keys distinctly. Existing chat draws beneath panels;
reserved/docked chat geometry, persistence, script geometry, native widgets and
live RustyRig integration remain outstanding. Workspace tests added, not run.

### Shared keyboard focus conventions

- Active panel frame/title uses bright cyan; inactive panel frames use ordinary
  cyan. Preserve an unmistakable focus indicator independent of widget focus.
- Carry spatial Ctrl+arrow / Ctrl+keypad navigation, keypad 5 chat focus,
  click-to-focus and raise, remembered per-panel control focus, and ordinary
  Tab/Shift-Tab/arrows/Space/Enter form navigation into GTK3 and future backends.
- GTK must not intercept ordinary entry editing or allow panel-navigation keys
  to leak to PTT; respect modal priority and use theme-appropriate contrast.
- TUI frame implemented with ANSI 16-color SGR; visual testing still pending.

### Production TUI integration checkpoint

Foundation merged into Axe main (9949362) and RustyRig main (8d28f32).
User reports reinstall-debs.sh builds, legacy TUI works and GTK starts without
new errors. RustyRig e42cf10 adds packaged server-picker.rrs/server-editor.rrs,
user/admin overrides, native profile list/select/connect/disconnect and basic
name/URL/user/password editor. Legacy picker fallback retained. New code not
yet compiled/tested. Editor lacks full GTK profile options, atomic multi-field
save and inline error label. Alt-X while modal, list refresh, compact row layout
and terminal repaint/cursor behavior require follow-up testing. Next stabilize
picker, then add VFO panel and audio controls. No GTK implementation changes.

### Tiled workspace checkpoint

Default side-column tiling reserves chat width; ordered weighted tile heights,
minimum sizes, automatic hiding on narrow/short terminals, draggable column
divider, directional focus. Structured full-screen tabs suppress tiles.
Floating panels remain optional. Dynamic labels support host state readback.
This is a side-column prototype, not yet an arbitrary recursive split tree;
vertical divider dragging, persistence, user rearrangement and compact forms
remain pending. Test geometry and redraw with real terminal before expanding.
