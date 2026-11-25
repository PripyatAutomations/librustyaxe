# RustyUI terminal backend, independent of GTK. SPDX-License-Identifier: MIT
.DEFAULT_GOAL := all
REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
CC ?= cc
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr
LIBDIR ?= $(PREFIX)/lib/$(shell $(CC) -dumpmachine)
INCLUDEDIR ?= $(PREFIX)/include
DESTDIR ?=
BUILD_DIR ?= $(REPO_ROOT)/build/ui
AXE_BUILD_DIR ?= $(REPO_ROOT)/build
VERSION ?= $(strip $(shell cat $(REPO_ROOT)/.version))
SOVERSION ?= 1
TERM_PKG := $(shell $(PKG_CONFIG) --exists tinfo && echo tinfo || echo ncurses)
DEPS := glib-2.0 gio-2.0 ncursesw
ifeq ($(filter clean uninstall,$(MAKECMDGOALS)),)
ifeq ($(shell $(PKG_CONFIG) --exists $(DEPS) && echo yes),)
$(error RustyUI TUI requires development packages: $(DEPS))
endif
endif
SOURCES := core/layout.c core/form.c core/workspace.c core/theme.c tui/theme.palette.c tui/backend.curses.c tui/form.c tui/tui.c tui/tui.completion.c tui/tui.keys.c tui/tui.theme.c tui/tui.window.c vendor/libtermkey/termkey.c vendor/libtermkey/driver-csi.c vendor/libtermkey/driver-ti.c
OBJECTS := $(addprefix $(BUILD_DIR)/,$(SOURCES:.c=.o))
HEADERS := $(wildcard *.h) vendor/libtermkey/termkey.h
SCRIPT_BUILD_DIR ?= $(AXE_BUILD_DIR)/script
SCRIPT_LIB := $(SCRIPT_BUILD_DIR)/librustyscript.so.$(SOVERSION).$(VERSION)
AXE_LIB := $(AXE_BUILD_DIR)/librustyaxe.so.$(SOVERSION).$(VERSION)
REAL_LIB := $(BUILD_DIR)/librustyui.so.$(SOVERSION).$(VERSION)
SONAME := librustyui.so.$(SOVERSION)
PC_FILE := $(BUILD_DIR)/librustyui.pc
UI_CFLAGS := -std=gnu23 -I$(REPO_ROOT) -I$(REPO_ROOT)/rustyscript -I$(CURDIR)/vendor/libtermkey -I$(AXE_BUILD_DIR)/include -I$(AXE_BUILD_DIR)/generated $(shell $(PKG_CONFIG) --cflags $(DEPS))
UI_LIBS := -L$(SCRIPT_BUILD_DIR) -lrustyscript -L$(AXE_BUILD_DIR) -lrustyaxe $(shell $(PKG_CONFIG) --libs $(DEPS)) -lbsd -pthread -lm
TESTS := $(addprefix $(BUILD_DIR)/,test_mirc test_completion_labels test_layout test_form test_workspace test_input)
.PHONY: all test tests install uninstall clean
all: $(REAL_LIB) $(PC_FILE)

$(AXE_LIB):
	$(MAKE) -C "$(REPO_ROOT)" WITH_UI=false BUILD_DIR="$(abspath $(AXE_BUILD_DIR))" VERSION="$(VERSION)" SOVERSION="$(SOVERSION)" "$(abspath $@)"

$(SCRIPT_LIB): $(AXE_LIB)
	$(MAKE) -C "$(REPO_ROOT)/rustyscript" BUILD_DIR="$(abspath $(SCRIPT_BUILD_DIR))" AXE_BUILD_DIR="$(abspath $(AXE_BUILD_DIR))" VERSION="$(VERSION)" SOVERSION="$(SOVERSION)"

$(BUILD_DIR)/%.o: %.c $(HEADERS) $(wildcard tui/*.h) $(wildcard vendor/libtermkey/*.h) $(wildcard $(REPO_ROOT)/rustyscript/*.h) | $(AXE_LIB) $(SCRIPT_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(UI_CFLAGS) $(CFLAGS) -fPIC -c $< -o $@

$(REAL_LIB): $(OBJECTS) $(AXE_LIB) $(SCRIPT_LIB)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$(SONAME) -o $@ $(OBJECTS) $(UI_LIBS)
	ln -sfn $(notdir $@) $(BUILD_DIR)/$(SONAME)
	ln -sfn $(SONAME) $(BUILD_DIR)/librustyui.so

$(PC_FILE): GNUmakefile
	@mkdir -p $(BUILD_DIR)
	@printf '%s\n' 'prefix=$(PREFIX)' 'libdir=$(LIBDIR)' 'includedir=$(INCLUDEDIR)' '' \
	 'Name: librustyui' 'Description: RustyUI reusable terminal presentation' \
	 'Version: $(VERSION)' 'Requires: librustyaxe librustyscript' \
	 'Libs: -L$${libdir} -lrustyui' 'Cflags: -I$${includedir}' > $@

$(BUILD_DIR)/test_%: tests/test_%.c $(REAL_LIB)
	$(CC) $(CPPFLAGS) $(UI_CFLAGS) $(CFLAGS) -o $@ $< -L$(BUILD_DIR) -lrustyui $(UI_LIBS)

test tests: all $(TESTS)
	@set -e; for t in $(TESTS); do LD_LIBRARY_PATH="$(abspath $(BUILD_DIR)):$(abspath $(AXE_BUILD_DIR)):$(abspath $(SCRIPT_BUILD_DIR)):$LD_LIBRARY_PATH" "$$t"; done
	@! readelf -d $(REAL_LIB) | grep -E 'NEEDED.*libgtk'

install: all
	install -Dm755 $(REAL_LIB) "$(DESTDIR)$(LIBDIR)/$(notdir $(REAL_LIB))"
	ln -sfn $(notdir $(REAL_LIB)) "$(DESTDIR)$(LIBDIR)/$(SONAME)"
	ln -sfn $(SONAME) "$(DESTDIR)$(LIBDIR)/librustyui.so"
	install -d "$(DESTDIR)$(INCLUDEDIR)/librustyui"
	install -m644 $(HEADERS) "$(DESTDIR)$(INCLUDEDIR)/librustyui/"
	install -Dm644 $(PC_FILE) "$(DESTDIR)$(LIBDIR)/pkgconfig/librustyui.pc"

uninstall:
	rm -f "$(DESTDIR)$(LIBDIR)/$(notdir $(REAL_LIB))" "$(DESTDIR)$(LIBDIR)/$(SONAME)" "$(DESTDIR)$(LIBDIR)/librustyui.so" "$(DESTDIR)$(LIBDIR)/pkgconfig/librustyui.pc"
	@for h in $(HEADERS); do rm -f "$(DESTDIR)$(INCLUDEDIR)/librustyui/${h##*/}"; done
	-rmdir "$(DESTDIR)$(INCLUDEDIR)/librustyui"

clean:
	rm -rf "$(BUILD_DIR)"

# Optional interactive backend feasibility probe; not part of normal tests.
.PHONY: test-ncurses-rgb
test-ncurses-rgb:
	@$(PKG_CONFIG) --exists ncursesw || { echo "Install libncurses-dev (ncursesw)"; exit 1; }
	@mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=gnu11 -Wall -Wextra $$( $(PKG_CONFIG) --cflags ncursesw ) tests/test_ncurses_rgb.c -o "$(BUILD_DIR)/test_ncurses_rgb" $$( $(PKG_CONFIG) --libs ncursesw )
	@"$(BUILD_DIR)/test_ncurses_rgb"
