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

**Host entry-point contract:** `script`, `hotkey`, and `event` are the
only source constructs that register entry points. Loading a source file does not
execute handler bodies. The script's other declared functions are helpers; they
run only when invoked by a registered handler or another function. A function
definition is not an event subscription.

- `script("/name", { ... });` installs a handler in the host's normal command
  dispatcher. Arguments and originating user/window/session context will be
  supplied when that command is invoked; normal permissions still apply.
- `hotkey _&^X { ... }` installs a keyboard callback in hosts with a frontend.
  `_` = Shift, `&` = Alt, `^` = Control.
- `event irc.privmsg { ... }` subscribes to the named event and receives a
  copy of its payload through the event bus. Subscriptions must be removed when
  the script is unloaded.

Host availability is separate from syntax validity: a headless host may report
a hotkey registration as unsupported, without rejecting an otherwise valid
script. Registration/unregistration must be transactional on reload: never
leave duplicate handlers or stale callbacks after a failed reload.

Top-level declarations and persistent variables are initialized at load; the
language will not allow arbitrary top-level executable statements. Host
integration must use the registration callback in this syntax foundation, not
reparse script body text itself.

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
