# RustyScript interpreter (November 22, 2026)

For the complete current **RustyRig scripting API reference**, including
host-native functions, file operations, shared state, timers, and GTK script
console, see [RustyRig's RustyScript reference](https://github.com/PripyatAutomations/rustyrig-fw/blob/main/doc/RUSTYSCRIPT.md)
and [UI reference](https://github.com/PripyatAutomations/rustyrig-fw/blob/main/doc/RUSTYSCRIPT-UI.md).
This README describes the standalone interpreter, not host-specific APIs.

RustyScript is a separately built and packaged C library inside the
librustyaxe repository. The initial host integration is in rustyrig-fw
(`rrclient` and `rrserver`) and uses the installed librustyscript library.

Three host registration forms are recognized:

```c
script("/hello", {
    log.info("Hello from RustyScript")
});

hotkey _&^X {
    log.info("Shift+Alt+Ctrl+X")
}

event irc.privmsg {
    log.info(event.text)
}
```

`_` = Shift, `&` = Alt, `^` = Control. Braces are mandatory.

Comments may use `//` or `#` to end the current line, or `/* ... */`
to span multiple lines. They are allowed between registrations and within
handler blocks. Comment markers inside quoted strings/characters are literal.
Block comments must be terminated before the end of the script.

```c
# My custom script
script("/hello", {
    // This executes when the command is invoked
    log.info("Hello # not a comment") /* another comment */
});
```
The script loader owns registrations but does not execute handler bodies.
The host invokes handlers through `rrs_vm_invoke`. Native host functions
are registered through `rrs_vm_register_native`. Script load uses atomic
replacement of the registration table, preserving the last good table on
parse failure. The event callback can receive an owned dictionary snapshot.

The interpreter currently evaluates basic declarations, literals, numeric
expressions, string concatenation, dictionaries, function calls to registered
native functions, and braced `if`/`else`. It is an **initial executable
subset**, not a full language implementation. In particular, it does **not**
yet support script-defined functions, global/persistent declarations,
type enforcement on assignments/returns, SQLite, async/await, exceptions,
subprocess control or audio/pipeline APIs. A registration body is checked for
balanced braces at load time; comprehensive body syntax checking is still
pending.

### Arrays

RustyScript supports bounded array literals such as
`["#hamradio-test@efnet", "#test@station"]` and string-array declarations
such as `char rooms[][] = ["#hamradio-test@efnet"]` (the older `char[][] rooms` spelling remains accepted).
A single array may contain up to 256 entries. The embedding host can inspect
array entries using `rrs_array_length()` and `rrs_array_get()`; the latter
returns a borrowed value. Arrays are copied by `=` and when passed to host-native functions. RustyRig's `chat.send` accepts a configured array of up to
32 room destinations and routes each through its named connection profile.

### Assignment, managed references, and const

`=` copies a value; `:=` binds a managed reference to an existing
variable. Writing `=` through a reference updates its target. Reusing `:=`
on a mutable binding rebinds it to another variable. References use
reference-counted storage, so an aliased local value remains valid while
another binding holds it.

```c
int a = 10;
int b = a;
int c := a;
b = 20;          // a remains 10
c = 30;          // a becomes 30

const int limit = 100;
const int view := a;
a = 40;          // allowed
view = 50;       // error
```

`const` makes a binding read-only, not its target globally immutable.
Mutable references cannot bind to const sources; const references cannot
be rebound. No pointers or addresses are exposed.

**Not yet available:** script-defined functions (and therefore default
by-reference parameters), cyclic garbage collection, GC statistics,
or nested member mutation and recursively enforced const. Host-native
function arguments are currently evaluated as value copies.

### Interactive evaluation and diagnostics

`rrs_vm_eval(vm, source, &result)` evaluates expressions and simple statements
in a persistent **per-VM console scope**, independent of registered handlers.
The caller owns the result. `rrs_value_repr(result)` formats scalar values
and nested dictionaries into a bounded, printable representation; free the
returned string after use. `rrs_vm_error(vm)` reports a failed evaluation,
including line/column when available. `rrs_vm_position()` exposes the current
source location to native callbacks for warnings and diagnostic messages.

Shared `global` and named `state.*` namespaces are implemented by
the RustyRig host, **not** in the standalone interpreter. Separate script VMs
remain isolated unless they use those host bindings. A console snippet cannot
directly inspect another VM's handler-local variables; RustyRig exposes
read-only `debug.scripts()`, `debug.keys()`, and `debug.state()`
helpers for published script state.

Run `make -C rustyscript test` or the repository's normal `make test`.
The shared library can be built separately with `make -C rustyscript`.
Root `make install` installs `librustyscript` and its pkg-config metadata.
Debian packages are `librustyscript0` and `librustyscript-dev`.

The runtime does not depend on GTK or librrprotocol. The host integrations
provide event subscriptions, slash-command dispatch (rrclient), and GTK hotkey
dispatch. Server-side script management is implemented by RustyRig through its
admin-authorized command channel. Remote invocation of script-defined
slash commands remains unsupported.
