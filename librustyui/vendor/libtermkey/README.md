# Bundled libtermkey

MIT-licensed terminal input decoder, version 0.22. Upstream mirror:
https://github.com/deepin-community/libtermkey/

See LICENSE and CHANGES.rrui for attribution, reference revision and local
patches. RustyUI compiles these sources into its terminal library. System
libtinfo (or ncurses where terminfo is not split) is used for terminal
capabilities; no external libtermkey or GTK dependency is required.
