# RustyScript (early implementation)

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
The script loader owns registrations but does not execute handler bodies.
The host invokes handlers through `rrs_vm_invoke`. Native host functions
are registered through `rrs_vm_register_native`. Script load uses atomic
replacement of the registration table, preserving the last good table on
parse failure. The event callback can receive an owned dictionary snapshot.

The interpreter currently evaluates basic declarations, literals, numeric
expressions, string concatenation, dictionaries, function calls to registered
native functions, and braced `if`/`else`. It is an **initial executable
subset**, not a full language implementation. In particular, it does **not**
yet support script-defined functions, global/persistent declarations, arrays,
type enforcement on assignments/returns, SQLite, async/await, exceptions,
subprocess control or audio/pipeline APIs. A registration body is checked for
balanced braces at load time; comprehensive body syntax checking is still
pending.

Run `make -C rustyscript test` or the repository's normal `make test`.
The shared library can be built separately with `make -C rustyscript`.
Root `make install` installs `librustyscript` and its pkg-config metadata.
Debian packages are `librustyscript0` and `librustyscript-dev`.

The runtime does not depend on GTK or librrprotocol. The host integrations
provide event subscriptions, slash-command dispatch (rrclient), and GTK hotkey
dispatch. Server-side script management and remote slash-command invocation
need their own authorized administrative API.
