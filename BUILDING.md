# Standalone librustyaxe build

librustyaxe remains MIT licensed. It builds independently of rustyrig-fw and
librrprotocol on Debian/Linux.

Install prerequisites:

```sh
sudo apt install build-essential pkg-config libglib2.0-dev libbsd-dev libncurses-dev
make -j4
make check
make DESTDIR=/tmp/axe-root install
```

The build emits a versioned shared library with SONAME `librustyaxe.so.0`,
a development linker symlink, public headers and pkg-config metadata.
Supported overrides include PREFIX, LIBDIR, INCLUDEDIR, DESTDIR, CC,
CPPFLAGS, CFLAGS and LDFLAGS.

The standalone build intentionally omits bundled Mongoose code and generates
its own minimal build_config.h. It must not require librrprotocol, because
librrprotocol depends on librustyaxe.

RustyRig's in-tree build continues to include `librustyaxe/rules.mk`.
Both modes share the maintained object inventory in `sources.mk`.

Next for Debian: create separate source packaging in this repository and
verify the ABI/SONAME, clean sbuild and tests before switching RustyRig
to installed library development packages.
