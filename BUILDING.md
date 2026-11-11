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

The standalone build defaults to `WITH_MONGOOSE=false`, even if
`mongoose-webserver-source` is installed. To explicitly build the legacy
Mongoose adapter, use `WITH_MONGOOSE=true` with `/usr/src/mongoose.c` and
`/usr/include/mongoose.h` installed, or `WITH_MONGOOSE=auto` to enable it
only when both are present. Set `MONGOOSE_SOURCE` and
`MONGOOSE_INCLUDE_DIR` for custom source installations. The
installed `<librustyaxe/build_config.h>` records the compiled feature;
consumers do not need Mongoose headers. It must not require librrprotocol, because
librrprotocol depends on librustyaxe.

RustyRig's in-tree build continues to include `librustyaxe/rules.mk`.
Both modes share the maintained object inventory in `sources.mk`.

Next for Debian: create separate source packaging in this repository and
verify the ABI/SONAME, clean sbuild and tests before switching RustyRig
to installed library development packages.

**Licensing:** Mongoose is GPL-2.0-only or commercial licensed. Including
its source in the RustyAxe shared library means the resulting combined binary
must meet the selected Mongoose license (GPLv2 for Debian), even though
RustyAxe's own source remains MIT. The Mongoose-free build remains MIT-only.

## Native WebSocket client

The reusable `<librustyaxe/transport.wss.h>` driver implements
`rr_transport_t` using POSIX sockets, OpenSSL and libwslay. It supports
`ws://` and `wss://`, verifies TLS server certificates and DNS names, and
performs DNS resolution on a worker thread rather than the application's
audio/event polling thread. Applications call `rr_transport_poll(t, 0)` for
nonblocking progress, then release the transport following CLOSE.
HTTP 307 redirects are bounded and same-origin by default.

Debian build prerequisites additionally include `libssl-dev` and
`libwslay-dev`; the pkg-config module is called `libwslay`. The
`make check` suite includes a local test of WebSocket upgrade followed by
a binary message in the same TCP packet.

The Mongoose adapter remains optional and separate from the native client.
