# MIT IRC library built alongside (but separately from) MIT RustyAxe.
.DEFAULT_GOAL := all
# Locate the owning librustyaxe checkout, regardless of the invocation cwd.
REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
CC ?= cc
RR_C_STD := $(shell if printf 'int x;\n' | $(CC) -std=gnu23 -Werror -x c -fsyntax-only - >/dev/null 2>&1; then echo -std=gnu23; elif printf 'int x;\n' | $(CC) -std=gnu2x -Werror -x c -fsyntax-only - >/dev/null 2>&1; then echo -std=gnu2x; fi)
ifeq ($(strip $(RR_C_STD)),)
$(error $(CC) does not support -std=gnu23 or -std=gnu2x; a C23-capable compiler is required)
endif
export RR_C_STD
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr
LIBDIR ?= $(PREFIX)/lib/$(shell $(CC) -dumpmachine)
INCLUDEDIR ?= $(PREFIX)/include
DESTDIR ?=
BUILD_DIR ?= $(REPO_ROOT)/build/irc
AXE_BUILD_DIR ?= $(REPO_ROOT)/build
VERSION ?= $(strip $(shell cat $(REPO_ROOT)/.version))
SOVERSION ?= 1
SOURCES := irc.c connection.c irc.channel.c irc.modes.c irc.user.c irc.capab.c irc.commands.c irc.numerics.c irc.event.c irc.dispatch.c irc.audio.c irc.dcc.c
OBJECTS := $(addprefix $(BUILD_DIR)/,$(SOURCES:.c=.o))
HEADERS := $(filter-out internal.h,$(notdir $(wildcard *.h)))
REAL_LIB := $(BUILD_DIR)/librustyirc.so.$(SOVERSION).$(VERSION)
SONAME := librustyirc.so.$(SOVERSION)
PC_FILE := $(BUILD_DIR)/librustyirc.pc
IRC_CFLAGS := -I$(AXE_BUILD_DIR)/include $(shell $(PKG_CONFIG) --cflags glib-2.0 2>/dev/null) $(RR_C_STD)
IRC_LIBS := -L$(AXE_BUILD_DIR) -lrustyaxe -lbsd
AXE_LIB := $(AXE_BUILD_DIR)/librustyaxe.so.$(SOVERSION).$(VERSION)
.PHONY: all test tests install uninstall clean distclean
all: $(REAL_LIB) $(PC_FILE)

$(AXE_LIB):
	$(MAKE) -C "$(REPO_ROOT)" BUILD_DIR="$(abspath $(AXE_BUILD_DIR))" VERSION="$(VERSION)" SOVERSION="$(SOVERSION)" "$(abspath $@)"

# The parent creates generated headers and the include symlink used here.
$(BUILD_DIR)/%.o: %.c $(wildcard *.h) | $(AXE_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(IRC_CFLAGS) $(CFLAGS) -fPIC -c -o $@ $<

$(REAL_LIB): $(OBJECTS) $(AXE_LIB)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$(SONAME) -o $@ $(OBJECTS) $(IRC_LIBS)
	ln -sfn $(notdir $@) $(BUILD_DIR)/$(SONAME)
	ln -sfn $(SONAME) $(BUILD_DIR)/librustyirc.so

$(PC_FILE): GNUmakefile
	@mkdir -p $(BUILD_DIR)
	@printf '%s\n' 'prefix=$(PREFIX)' 'libdir=$(LIBDIR)' 'includedir=$(INCLUDEDIR)' '' \
	 'Name: librustyirc' 'Description: MIT IRC protocol library built with RustyAxe' \
	 'Version: $(VERSION)' 'Requires: librustyaxe' \
	 'Libs: -L$${libdir} -lrustyirc' 'Cflags: -I$${includedir}' > $@

test tests: all
	@readelf -d $(REAL_LIB) | grep -q 'SONAME.*$(SONAME)'
	@test -L $(BUILD_DIR)/$(SONAME)
	@grep -Fqx 'Libs: -L$${libdir} -lrustyirc' $(PC_FILE)
	@grep -Fqx 'Cflags: -I$${includedir}' $(PC_FILE)

install: all
	install -Dm755 $(REAL_LIB) "$(DESTDIR)$(LIBDIR)/$(notdir $(REAL_LIB))"
	ln -sfn $(notdir $(REAL_LIB)) "$(DESTDIR)$(LIBDIR)/$(SONAME)"
	ln -sfn $(SONAME) "$(DESTDIR)$(LIBDIR)/librustyirc.so"
	install -d "$(DESTDIR)$(INCLUDEDIR)/librustyirc"
	@for h in $(HEADERS); do install -m644 "$$h" "$(DESTDIR)$(INCLUDEDIR)/librustyirc/$$h"; done
	install -Dm644 $(PC_FILE) "$(DESTDIR)$(LIBDIR)/pkgconfig/librustyirc.pc"

uninstall:
	rm -f "$(DESTDIR)$(LIBDIR)/$(notdir $(REAL_LIB))" "$(DESTDIR)$(LIBDIR)/$(SONAME)" "$(DESTDIR)$(LIBDIR)/librustyirc.so" "$(DESTDIR)$(LIBDIR)/pkgconfig/librustyirc.pc"
	@for h in $(HEADERS); do rm -f "$(DESTDIR)$(INCLUDEDIR)/librustyirc/$$h"; done
	-rmdir "$(DESTDIR)$(INCLUDEDIR)/librustyirc"

clean distclean:
	rm -rf $(BUILD_DIR)
