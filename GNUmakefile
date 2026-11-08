# Standalone Debian/Linux build. RustyRig's parent build uses rules.mk.
# SPDX-License-Identifier: MIT
.DEFAULT_GOAL := all
CC ?= cc
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr
LIBDIR ?= $(PREFIX)/lib/$(shell dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || $(CC) -dumpmachine)
INCLUDEDIR ?= $(PREFIX)/include
DESTDIR ?=
BUILD_DIR ?= build
VERSION ?= 0.1.0
SOVERSION ?= 0
USE_EEPROM ?= false
LIBRUSTYAXE_STANDALONE := true

include sources.mk
OBJECTS := $(addprefix $(BUILD_DIR)/objects/,$(librustyaxe_objs))
PUBLIC_HEADERS := $(filter-out termkey-internal.h tui.internal.h,$(notdir $(wildcard *.h)))
TERM_LIB := $(shell $(PKG_CONFIG) --exists tinfo && echo tinfo || echo ncurses)
DEPS := glib-2.0 gio-2.0 $(TERM_LIB)
ifeq ($(filter clean distclean src-deb source-deb,$(MAKECMDGOALS)),)
ifeq ($(shell $(PKG_CONFIG) --exists $(DEPS) && echo yes),)
$(error Missing development dependencies: $(DEPS))
endif
endif
AXE_CPPFLAGS := -I$(BUILD_DIR)/include -I$(BUILD_DIR)/generated $(shell $(PKG_CONFIG) --cflags $(DEPS)) -DLIBRUSTYAXE_STANDALONE=1 -DHOST_POSIX=1
AXE_LDLIBS := $(shell $(PKG_CONFIG) --libs $(DEPS)) -lbsd -lcrypt -ldl -lm -pthread
REAL_LIB := $(BUILD_DIR)/librustyaxe.so.$(SOVERSION).$(VERSION)
SONAME := librustyaxe.so.$(SOVERSION)
PC_FILE := $(BUILD_DIR)/librustyaxe.pc

.PHONY: all clean distclean check install deb src-deb source-deb
all: $(REAL_LIB) $(PC_FILE)

$(BUILD_DIR)/include/librustyaxe $(BUILD_DIR)/generated/build_config.h &:
	@mkdir -p $(BUILD_DIR)/include $(BUILD_DIR)/generated
	@ln -sfn ../.. $(BUILD_DIR)/include/librustyaxe
	@printf '%s\n' '/* Standalone library: no RustyRig board config. */' '#ifndef LIBRUSTYAXE_BUILD_CONFIG_H' '#define LIBRUSTYAXE_BUILD_CONFIG_H' '#endif' > $(BUILD_DIR)/generated/build_config.h

$(BUILD_DIR)/objects/%.o: %.c $(wildcard *.h) | $(BUILD_DIR)/include/librustyaxe $(BUILD_DIR)/generated/build_config.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(AXE_CPPFLAGS) $(CFLAGS) -fPIC -c -o $@ $<

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
	$(MAKE) -C tests check TEST_BUILD_DIR=../$(BUILD_DIR)/tests \
	    INCLUDES="-I../$(BUILD_DIR)/include -I../$(BUILD_DIR)/generated $(shell $(PKG_CONFIG) --cflags $(DEPS))" \
	    LIBS="-L../$(BUILD_DIR) -lrustyaxe -Wl,-rpath,$(abspath $(BUILD_DIR)) $(AXE_LDLIBS)"

install: all
	install -Dm755 $(REAL_LIB) "$(DESTDIR)$(LIBDIR)/$(notdir $(REAL_LIB))"
	ln -sfn $(notdir $(REAL_LIB)) "$(DESTDIR)$(LIBDIR)/$(SONAME)"
	ln -sfn $(SONAME) "$(DESTDIR)$(LIBDIR)/librustyaxe.so"
	install -d "$(DESTDIR)$(INCLUDEDIR)/librustyaxe"
	@for h in $(PUBLIC_HEADERS); do install -m644 "$$h" "$(DESTDIR)$(INCLUDEDIR)/librustyaxe/$$h"; done
	install -Dm644 $(PC_FILE) "$(DESTDIR)$(LIBDIR)/pkgconfig/librustyaxe.pc"

clean:
	rm -rf $(BUILD_DIR)

# Build only the independently versioned librustyaxe Debian binaries.
deb:
	dpkg-buildpackage -b -us -uc

# Return the checkout to source-distribution condition. Do not delete the
# .deb files in the parent directory: they are intentional release outputs.
distclean: clean
	@if command -v dh_clean >/dev/null 2>&1; then dh_clean; fi
	rm -rf debian/tmp debian/.debhelper debian/files debian/debhelper-build-stamp
	rm -rf debian/librustyaxe0 debian/librustyaxe-dev

# Debian source packages are optional outputs, separate from normal builds.
# dpkg-source's quilt format needs an upstream orig tarball. Create it from
# committed sources (never from build/ or untracked generated artifacts).
src-deb source-deb:
	@git rev-parse --is-inside-work-tree >/dev/null 2>&1 && git diff --quiet && git diff --cached --quiet || { echo "Commit tracked changes before source packaging" >&2; exit 1; }
	@v=$$(dpkg-parsechangelog -S Version | sed 's/-[^-]*$$//'); \
	 git archive --format=tar --prefix="librustyaxe-$$v/" HEAD -- . ':!debian' | gzip -n > "../librustyaxe_$$v.orig.tar.gz"
	dpkg-buildpackage -S -us -uc
