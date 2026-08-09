librustyaxe: Utility stuff drug in from various programs i've written.

Here we store code that is intended for use in current/future projects.
Unified into a library to reduce duplication and motivate progress.

This code has bugs. Feel free to fix/report them! ;)

cat*:		Support for parsing and sending Yaesu ft[89]91 style CAT 
config: 	Loading of ini-style configuration into dicts
daemon:		Daemonization (background server) on posix
dict.c:		Hashmap dictionary object
eeprom.c:	EEPROM (real or file backed) for storing persistent data
event-bus.c:	Event message (json) passing support
io.c:		Future abstraction layer for serial/socket/pipe
json.c:		dict based json handling (json path as key)
kvstore.c:	Alternative key-value store (deprecated in favor of dict)
list.c:		Linked lists
logger.c:	Advanced logging facility
maidenhead.c:	Maidenhead coordinate conversions
module.c:	Loadable modules on posix/win64 machines
posix.c:	Things only relevant on posix-ish hosts
ringbuffer.c:	Basic implementation of ring buffer, for audio/video
subproc.c:	Subprocess management (for managing fwdsp, etc)
termkey.c:	libtermkey keyboard processing
tui.c:		Simple Text User Interface library
tui.colorpick.c: Future color selector popup for IRC
tui.completion.c: Future context-sensistive text completion (tab/space/arrows)
tui.keys.c:	Keyboard interface for TUI, using libtermkey
tui.theme.c:	TUI theme/named colors support
tui.window.c:	Window manager for the TUI
util.file.c:	File related utilities
util.math.c:	Portable implementations of some maths
util.string.c:	String utilities
util.time.c:	Time conversion utilities

