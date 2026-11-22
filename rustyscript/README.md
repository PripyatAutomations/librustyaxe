# RustyScript — parser foundation (work in progress)

This directory is the beginning of the independent `librustyscript` library
within the librustyaxe repository.

The initial parser recognizes and reports three **top-level registrations**:

```c
script("/hello", {
    window.input("#test", "Hello")
});

hotkey _&^X {
    window.input("#test", "/who")
}

event irc.privmsg {
    if (event.text == "!ping") {
        irc.say(event.target, "pong")
    }
}
```

`_` = Shift, `&` = Alt, `^` = Control. Modifier order is not significant.
The host will decide whether key names are valid and where bindings apply.

All control blocks must use braces. Semicolon after a registration is optional.
The callback gets the kind, original name, body source span, and line.

**Not implemented:** executing script bodies, declarations/functions, typed
values/JSON, the event bridge, hotkey registration, command registration,
threaded scheduler, persistent variables, database host adapters, and the
`security.scripting.allow-unsafe-exec` enforcement point. Do not load
untrusted scripts or treat this parser as a complete language validator.

Build/test the independent syntax module with `make test` from this directory.
It uses only the C standard library; subsequent runtime integration may link
to librustyaxe and librustyirc.
