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

The interpreter evaluates basic declarations, literals, numeric
expressions, string concatenation, dictionaries, script-defined typed functions,
native functions, and braced `if`/`else`. It remains a limited executable
subset: global/persistent variable declarations, full type checking,
SQLite, async/await, exceptions, and subprocess control are not yet supported.
The loader checks registration/function braces, but still defers most body
expression errors until execution.

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

**Still unavailable:** cyclic garbage collection and recursive const
enforcement on nested member mutation. Host-native arguments remain value
copies; script-defined functions pass caller variables by reference.

### Functions, reference parameters, and memory

Typed functions can be defined at the top level of a `.rrs` script. Their
function bodies run when called, not when the script loads.

```c
void increment(int value) {
    value++;
}

int square(const int value) {
    return value * value;
}

script("/example", {
    int counter = 5;
    increment(counter);       // counter is now 6
    log.info(string.str(square(counter)));
});
```

**An existing variable passed to a script-defined function is passed by
managed reference by default.** Expressions and literals receive temporary
parameter storage. Assignments to ordinary parameters write back to the
caller's variable. Const parameters cannot modify their arguments, and const
variables cannot be passed to mutable parameters. The `:=` operator explicitly
aliases existing variables, and `=` copies values unless assigning through a
reference. User functions can return values and call other functions, with a
64-call recursion limit. Host-native functions retain the existing borrowed,
value-copy argument API.

Single-quoted character literals and double-quoted one-character strings
are both strings; binary data should use separate byte-oriented host APIs.

`runtime.memory()` returns a dictionary with `used` and `peak` managed
bytes, `objects` (live managed cells), `allocations`, `releases`,
`references`, and `gc` fields (`collections`, `requests`, `reclaimed`).
`runtime.gc.collect()` returns the number of unreachable cells collected.

**Current memory model:** cells use reference counting. Arrays and dictionaries
contain copied values, not references to cells, so managed reference cycles
cannot currently be created. Consequently `runtime.gc.collect()` returns
zero and `gc.collections` is zero; `gc.requests` counts explicit calls,
and `gc.reclaimed` counts immediate reference-count releases. Used/peak bytes
cover managed cell storage and nested values, not all native host allocations.
This is not yet a tracing or cycle-collecting GC.

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

### Anonymous typed dictionaries

```c
ui.add(dict {
   kind = "entry";
   id = "host";
   dict validation { required = true; max_length = 255; };
});
```
`dict { ... }` is an expression usable in arguments, arrays, initializers and returns. Fields accept `=` (or `:`), with comma or semicolon separators, including mixed and trailing separators. `dict name { ... }` inside the literal is shorthand for `name = dict { ... }`. Ordinary `{key: value}` literals remain supported.
