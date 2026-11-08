# GPLv2 IRC library built alongside (but separately from) MIT RustyAxe.
.DEFAULT_GOAL := all
CC ?= cc
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr
LIBDIR ?= $(PREFIX)/lib/$(shell $(CC) -dumpmachine)
INCLUDEDIR ?= $(PREFIX)/include
DESTDIR ?=
BUILD_DIR ?= ../build/irc
AXE_BUILD_DIR ?= ../build
VERSION ?= 0.1.0
SOVERSION ?= 0
SOURCES := irc.c connection.c irc.channel.c irc.modes.c irc.user.c irc.capab.c irc.commands.c irc.numerics.c irc.event.c irc.dispatch.c irc.audio.c irc.dcc.c
OBJECTS := $(addprefix $(BUILD_DIR)/,$(SOURCES:.c=.o))
HEADERS := $(filter-out internal.h,$(notdir $(wildcard *.h)))
REAL_LIB := $(BUILD_DIR)/librustyirc.so.$(SOVERSION).$(VERSION)
SONAME := librustyirc.so.$(SOVERSION)
PC_FILE := $(BUILD_DIR)/librustyirc.pc
IRC_CFLAGS := -I$(AXE_BUILD_DIR)/include $(shell $(PKG_CONFIG) --cflags glib-2.0 2>/dev/null) -std=gnu23
IRC_LIBS := -L$(AXE_BUILD_DIR) -lrustyaxe -lbsd
.PHONY: all check install uninstall clean distclean
all: $(REAL_LIB) $(PC_FILE)

$(BUILD_DIR)/%.o: %.c $(wildcard *.h)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(IRC_CFLAGS) $(CFLAGS) -fPIC -c -o $@ $<

$(REAL_LIB): $(OBJECTS) $(AXE_BUILD_DIR)/librustyaxe.so.$(SOVERSION).$(VERSION)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$(SONAME) -o $@ $(OBJECTS) $(IRC_LIBS)
	ln -sfn $(notdir $@) $(BUILD_DIR)/$(SONAME)
	ln -sfn $(SONAME) $(BUILD_DIR)/librustyirc.so

$(PC_FILE): GNUmakefile
	@mkdir -p $(BUILD_DIR)
	@printf '%s\n' 'prefix=$(PREFIX)' 'libdir=$(LIBDIR)' 'includedir=$(INCLUDEDIR)' '' \
	 'Name: librustyirc' 'Description: GPLv2 IRC protocol library built with RustyAxe' \
	 'Version: $(VERSION)' 'Requires: librustyaxe' \
	 'Libs: -L${libdir} -lrustyirc' 'Cflags: -I${includedir}' > $@

check: all
	@readelf -d $(REAL_LIB) | grep -q 'SONAME.*$(SONAME)'
	@test -L $(BUILD_DIR)/$(SONAME)

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
