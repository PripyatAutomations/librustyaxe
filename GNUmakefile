# Standalone Debian/Linux build. RustyRig's parent build uses rules.mk.
# SPDX-License-Identifier: MIT
.DEFAULT_GOAL := all
CC ?= cc
CFLAGS += -std=gnu23
PKG_CONFIG ?= pkg-config
# Derive native distro layouts from the selected toolchain's pkg-config.
# Explicit PREFIX/LIBDIR remain authoritative (including cross builds).
BUILD_PC_PREFIX := $(shell $(PKG_CONFIG) --variable=prefix glib-2.0 2>/dev/null)
PREFIX ?= $(if $(BUILD_PC_PREFIX),$(BUILD_PC_PREFIX),/usr)
LIBDIR ?= $(if $(filter $(BUILD_PC_PREFIX),$(PREFIX)),$(shell $(PKG_CONFIG) --variable=libdir glib-2.0 2>/dev/null),$(PREFIX)/lib)
INCLUDEDIR ?= $(PREFIX)/include
DESTDIR ?=
BUILD_DIR ?= build
VERSION ?= 0.1.0
SOVERSION ?= 0
USE_EEPROM ?= false
LIBRUSTYAXE_STANDALONE := true

# Source-only Debian package: mongoose-webserver-source.
# Set WITH_MONGOOSE=false to explicitly disable this optional backend.
MONGOOSE_SOURCE ?= $(if $(wildcard /usr/src/mongoose.c),/usr/src/mongoose.c,)
MONGOOSE_INCLUDE_DIR ?= $(if $(wildcard /usr/include/mongoose.h),/usr/include,)
WITH_MONGOOSE ?= auto
ifeq ($(WITH_MONGOOSE),auto)
WITH_MONGOOSE := $(if $(and $(MONGOOSE_SOURCE),$(MONGOOSE_INCLUDE_DIR)),true,false)
endif
ifeq ($(filter true false,$(WITH_MONGOOSE)),)
$(error WITH_MONGOOSE must be auto, true or false)
endif
ifeq ($(WITH_MONGOOSE),true)
ifeq ($(wildcard $(MONGOOSE_SOURCE)),)
$(error Mongoose requested but source missing: $(MONGOOSE_SOURCE))
endif
ifeq ($(wildcard $(MONGOOSE_INCLUDE_DIR)/mongoose.h),)
$(error Mongoose requested but header missing: $(MONGOOSE_INCLUDE_DIR)/mongoose.h)
endif
endif

include sources.mk
OBJECTS := $(addprefix $(BUILD_DIR)/objects/,$(librustyaxe_objs))
ifeq ($(WITH_MONGOOSE),true)
OBJECTS += $(BUILD_DIR)/objects/mongoose.o
endif
PUBLIC_HEADERS := $(filter-out termkey-internal.h tui.internal.h build_config.h,$(notdir $(wildcard *.h)))
TERM_LIB := $(shell $(PKG_CONFIG) --exists tinfo && echo tinfo || echo ncurses)
DEPS := glib-2.0 gio-2.0 $(TERM_LIB)
ifeq ($(filter clean distclean uninstall src-deb source-deb,$(MAKECMDGOALS)),)
ifeq ($(shell $(PKG_CONFIG) --exists $(DEPS) && echo yes),)
$(error Missing development dependencies: $(DEPS))
endif
endif
AXE_CPPFLAGS := -I$(BUILD_DIR)/generated -I$(BUILD_DIR)/include $(shell $(PKG_CONFIG) --cflags $(DEPS)) -DLIBRUSTYAXE_STANDALONE=1 -DHOST_POSIX=1
AXE_LDLIBS := $(shell $(PKG_CONFIG) --libs $(DEPS)) -lbsd -lcrypt -ldl -lm -pthread
ifeq ($(WITH_MONGOOSE),true)
AXE_CPPFLAGS += -DUSE_MONGOOSE=1 -I$(MONGOOSE_INCLUDE_DIR)
endif
REAL_LIB := $(BUILD_DIR)/librustyaxe.so.$(SOVERSION).$(VERSION)
SONAME := librustyaxe.so.$(SOVERSION)
PC_FILE := $(BUILD_DIR)/librustyaxe.pc

.PHONY: all clean distclean check install uninstall irc-build deb src-deb source-deb
all: $(BUILD_DIR)/generated/librustyaxe/build_config.h $(REAL_LIB) $(PC_FILE) irc-build

irc-build: $(REAL_LIB)
	$(MAKE) -C librustyirc all BUILD_DIR="$(abspath $(BUILD_DIR))/irc" AXE_BUILD_DIR="$(abspath $(BUILD_DIR))" VERSION=$(VERSION) SOVERSION=$(SOVERSION) PREFIX=$(PREFIX) LIBDIR=$(LIBDIR) INCLUDEDIR=$(INCLUDEDIR)

# The generated public header describes this library, never its consumers.
# Recalculate on each invocation but preserve timestamps when unchanged.
.PHONY: axe-config-force
$(BUILD_DIR)/generated/librustyaxe/build_config.h: axe-config-force
	@mkdir -p $(BUILD_DIR)/generated/librustyaxe
	@printf '%s\n' '/* SPDX-License-Identifier: MIT */' \
	 '#ifndef LIBRUSTYAXE_PUBLIC_BUILD_CONFIG_H' \
	 '#define LIBRUSTYAXE_PUBLIC_BUILD_CONFIG_H' \
	 '#define LIBRUSTYAXE_HAVE_MONGOOSE $(if $(filter true,$(WITH_MONGOOSE)),1,0)' \
	 '#define LIBRUSTYAXE_PUBLIC_MONGOOSE_HEADERS 0' '#endif' > $@.new
	@cmp -s $@.new $@ && rm -f $@.new || mv -f $@.new $@

$(BUILD_DIR)/include/librustyaxe $(BUILD_DIR)/generated/build_config.h &:
	@mkdir -p $(BUILD_DIR)/include $(BUILD_DIR)/generated
	@ln -sfn ../.. $(BUILD_DIR)/include/librustyaxe
	@printf '%s\n' '/* Standalone library: no RustyRig board config. */' \
	 '#ifndef LIBRUSTYAXE_BUILD_CONFIG_H' '#define LIBRUSTYAXE_BUILD_CONFIG_H' '#endif' > $(BUILD_DIR)/generated/build_config.h

$(BUILD_DIR)/objects/%.o: %.c $(wildcard *.h) $(BUILD_DIR)/generated/librustyaxe/build_config.h | $(BUILD_DIR)/include/librustyaxe $(BUILD_DIR)/generated/build_config.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(AXE_CPPFLAGS) $(CFLAGS) -fPIC -c -o $@ $<

ifeq ($(WITH_MONGOOSE),true)
$(BUILD_DIR)/objects/mongoose.o: $(MONGOOSE_SOURCE) $(MONGOOSE_INCLUDE_DIR)/mongoose.h $(BUILD_DIR)/generated/librustyaxe/build_config.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(AXE_CPPFLAGS) $(CFLAGS) -fPIC -c -o $@ $(MONGOOSE_SOURCE)
endif

$(REAL_LIB): $(OBJECTS)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$(SONAME) -o $@ $^ $(AXE_LDLIBS)
	ln -sfn $(notdir $@) $(BUILD_DIR)/$(SONAME)
	ln -sfn $(SONAME) $(BUILD_DIR)/librustyaxe.so

$(PC_FILE): GNUmakefile
	@mkdir -p $(BUILD_DIR)
	@printf '%s\n' 'prefix=$(PREFIX)' 'libdir=$(LIBDIR)' 'includedir=$(INCLUDEDIR)' '' \
	    'Name: librustyaxe' 'Description: Shared RustyRig utility library' 'Version: $(VERSION)' \
	    'Libs: -L$${libdir} -lrustyaxe' 'Cflags: -I$${includedir}' 'Requires.private: glib-2.0 gio-2.0' > $@

check: all
	$(MAKE) -C librustyirc check BUILD_DIR="$(abspath $(BUILD_DIR))/irc" AXE_BUILD_DIR="$(abspath $(BUILD_DIR))" VERSION=$(VERSION) SOVERSION=$(SOVERSION)
	$(MAKE) -C tests check TEST_BUILD_DIR=../$(BUILD_DIR)/tests \
	    INCLUDES="-I../$(BUILD_DIR)/include -I../$(BUILD_DIR)/generated $(shell $(PKG_CONFIG) --cflags $(DEPS))" \
	    LIBS="-L../$(BUILD_DIR) -lrustyaxe -Wl,-rpath,$(abspath $(BUILD_DIR)) $(AXE_LDLIBS)"

install: all
	install -Dm755 $(REAL_LIB) "$(DESTDIR)$(LIBDIR)/$(notdir $(REAL_LIB))"
	ln -sfn $(notdir $(REAL_LIB)) "$(DESTDIR)$(LIBDIR)/$(SONAME)"
	ln -sfn $(SONAME) "$(DESTDIR)$(LIBDIR)/librustyaxe.so"
	install -d "$(DESTDIR)$(INCLUDEDIR)/librustyaxe"
	@for h in $(PUBLIC_HEADERS); do install -m644 "$h" "$(DESTDIR)$(INCLUDEDIR)/librustyaxe/$h"; done
	install -m644 $(BUILD_DIR)/generated/librustyaxe/build_config.h "$(DESTDIR)$(INCLUDEDIR)/librustyaxe/build_config.h"
	install -Dm644 $(PC_FILE) "$(DESTDIR)$(LIBDIR)/pkgconfig/librustyaxe.pc"
	$(MAKE) -C librustyirc install BUILD_DIR="$(abspath $(BUILD_DIR))/irc" AXE_BUILD_DIR="$(abspath $(BUILD_DIR))" VERSION=$(VERSION) SOVERSION=$(SOVERSION) PREFIX=$(PREFIX) LIBDIR=$(LIBDIR) INCLUDEDIR=$(INCLUDEDIR) DESTDIR=$(DESTDIR)

uninstall:
	$(MAKE) -C librustyirc uninstall BUILD_DIR="$(abspath $(BUILD_DIR))/irc" AXE_BUILD_DIR="$(abspath $(BUILD_DIR))" VERSION=$(VERSION) SOVERSION=$(SOVERSION) PREFIX=$(PREFIX) LIBDIR=$(LIBDIR) INCLUDEDIR=$(INCLUDEDIR) DESTDIR=$(DESTDIR)
	rm -f "$(DESTDIR)$(LIBDIR)/$(notdir $(REAL_LIB))" "$(DESTDIR)$(LIBDIR)/$(SONAME)" "$(DESTDIR)$(LIBDIR)/librustyaxe.so" "$(DESTDIR)$(LIBDIR)/pkgconfig/librustyaxe.pc"
	@for h in $(PUBLIC_HEADERS); do rm -f "$(DESTDIR)$(INCLUDEDIR)/librustyaxe/$h"; done
	rm -f "$(DESTDIR)$(INCLUDEDIR)/librustyaxe/build_config.h"
	-rmdir "$(DESTDIR)$(INCLUDEDIR)/librustyaxe"

clean:
	$(MAKE) -C librustyirc clean BUILD_DIR="$(abspath $(BUILD_DIR))/irc" AXE_BUILD_DIR="$(abspath $(BUILD_DIR))"
	rm -rf $(BUILD_DIR)

# Build only the independently versioned librustyaxe Debian binaries.
deb:
	dpkg-buildpackage -b -us -uc

# Return the checkout to source-distribution condition. Do not delete the
# .deb files in the parent directory: they are intentional release outputs.
distclean: clean
	@if command -v dh_clean >/dev/null 2>&1; then dh_clean; fi
	rm -rf debian/tmp debian/.debhelper debian/files debian/debhelper-build-stamp
	rm -rf debian/librustyaxe0 debian/librustyaxe-dev debian/librustyirc0 debian/librustyirc-dev

# Debian source packages are optional outputs, separate from normal builds.
# dpkg-source's quilt format needs an upstream orig tarball. Create it from
# committed sources (never from build/ or untracked generated artifacts).
src-deb source-deb:
	@git rev-parse --is-inside-work-tree >/dev/null 2>&1 && git diff --quiet && git diff --cached --quiet || { echo "Commit tracked changes before source packaging" >&2; exit 1; }
	@v=$$(dpkg-parsechangelog -S Version | sed 's/-[^-]*$$//'); \
	 git archive --format=tar --prefix="librustyaxe-$$v/" HEAD -- . ':!debian' | gzip -n > "../librustyaxe_$$v.orig.tar.gz"
	dpkg-buildpackage -S -us -uc
