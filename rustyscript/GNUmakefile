# RustyScript shared library. SPDX-License-Identifier: MIT
.DEFAULT_GOAL := all
REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
CC ?= cc
PREFIX ?= /usr
LIBDIR ?= $(PREFIX)/lib/$(shell $(CC) -dumpmachine)
INCLUDEDIR ?= $(PREFIX)/include
DESTDIR ?=
BUILD_DIR ?= $(REPO_ROOT)/build/script
VERSION ?= $(strip $(shell cat $(REPO_ROOT)/.version))
SOVERSION ?= 0
AXE_BUILD_DIR ?= $(REPO_ROOT)/build
IRC_BUILD_DIR ?= $(AXE_BUILD_DIR)/irc
CFLAGS += -std=gnu23 -Wall -Wextra -Werror -pedantic
SOURCES := rrscript.c runtime.c
OBJECTS := $(addprefix $(BUILD_DIR)/,$(SOURCES:.c=.o))
REAL_LIB := $(BUILD_DIR)/librustyscript.so.$(SOVERSION).$(VERSION)
SONAME := librustyscript.so.$(SOVERSION)
PC_FILE := $(BUILD_DIR)/librustyscript.pc
.PHONY: all test clean install uninstall
all: $(REAL_LIB) $(PC_FILE)
$(BUILD_DIR):
	mkdir -p $@
$(BUILD_DIR)/%.o: %.c rrscript.h runtime.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@
$(REAL_LIB): $(OBJECTS)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$(SONAME) -o $@ $(OBJECTS) -L$(AXE_BUILD_DIR) -L$(IRC_BUILD_DIR) -lrustyaxe -lrustyirc
	ln -sfn $(notdir $@) $(BUILD_DIR)/$(SONAME)
	ln -sfn $(SONAME) $(BUILD_DIR)/librustyscript.so
$(PC_FILE): GNUmakefile | $(BUILD_DIR)
	@printf '%s\n' 'prefix=$(PREFIX)' 'libdir=$(LIBDIR)' 'includedir=$(INCLUDEDIR)' '' \
	 'Name: librustyscript' 'Description: RustyScript scripting runtime' \
	 'Version: $(VERSION)' 'Requires: librustyaxe librustyirc' \
	 'Libs: -L$${libdir} -lrustyscript' 'Cflags: -I$${includedir}' > $@
	@grep -Fqx 'Libs: -L$${libdir} -lrustyscript' $(PC_FILE)
	@grep -Fqx 'Cflags: -I$${includedir}' $(PC_FILE)
$(BUILD_DIR)/test_syntax: rrscript.c tests/test_syntax.c rrscript.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) rrscript.c tests/test_syntax.c -o $@
$(BUILD_DIR)/test_runtime: runtime.c rrscript.c tests/test_runtime.c runtime.h rrscript.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) runtime.c rrscript.c tests/test_runtime.c -o $@
test: $(BUILD_DIR)/test_syntax $(BUILD_DIR)/test_runtime
	$(BUILD_DIR)/test_syntax
	$(BUILD_DIR)/test_runtime
install: all
	install -Dm755 $(REAL_LIB) "$(DESTDIR)$(LIBDIR)/$(notdir $(REAL_LIB))"
	ln -sfn $(notdir $(REAL_LIB)) "$(DESTDIR)$(LIBDIR)/$(SONAME)"
	ln -sfn $(SONAME) "$(DESTDIR)$(LIBDIR)/librustyscript.so"
	install -d "$(DESTDIR)$(INCLUDEDIR)/librustyscript"
	install -m644 rrscript.h runtime.h "$(DESTDIR)$(INCLUDEDIR)/librustyscript/"
	install -Dm644 $(PC_FILE) "$(DESTDIR)$(LIBDIR)/pkgconfig/librustyscript.pc"
uninstall:
	rm -f "$(DESTDIR)$(LIBDIR)/$(notdir $(REAL_LIB))" "$(DESTDIR)$(LIBDIR)/$(SONAME)" "$(DESTDIR)$(LIBDIR)/librustyscript.so" "$(DESTDIR)$(LIBDIR)/pkgconfig/librustyscript.pc"
	rm -f "$(DESTDIR)$(INCLUDEDIR)/librustyscript/rrscript.h" "$(DESTDIR)$(INCLUDEDIR)/librustyscript/runtime.h"
	-rmdir "$(DESTDIR)$(INCLUDEDIR)/librustyscript"
clean:
	rm -rf "$(BUILD_DIR)"
