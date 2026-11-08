# RustyAxe library tree

This repository builds two separate ABI/libraries from a single source tree:

- **librustyaxe** (MIT): utility, configuration, event and platform helpers.
- **librustyirc** (MIT): IRC parser, client protocol machinery, SASL, DCC.
  It links to and requires librustyaxe, but librustyaxe does not require IRC.

Normal POSIX development from this repository:

```sh
make -j
make check
make install PREFIX=/usr/local
make uninstall PREFIX=/usr/local
```

Pass matching PREFIX/LIBDIR/DESTDIR to uninstall. The uninstall target removes
only these libraries' shipped files, not application configs or databases.
For packages on Debian/Devuan/Ubuntu use `make deb` for binary packages
(`librustyaxe0`, `librustyaxe-dev`, `librustyirc0`, `librustyirc-dev`) and
`make src-deb` for the source package. The runtime RustyAxe package
recommends IRC; IRC depends on RustyAxe.

The ordinary build is not Debian-specific. On Red Hat and Arch systems, use
the target platform's native headers, compiler, pkg-config paths and library
directory. Windows x64 (preferably MinGW-w64 cross-compiled), macOS and ESP32
are planned first-class targets; the current POSIX shared-object make recipes
are **not yet a validated native Windows/macOS/ESP-IDF implementation**.
Portable source and platform-specific shared-library recipes will be kept
separate; keep both RustyAxe libraries MIT-licensed when reorganizing components.
