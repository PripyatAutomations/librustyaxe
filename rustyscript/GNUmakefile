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
SOVERSION ?= 1
AXE_BUILD_DIR ?= $(REPO_ROOT)/build
CFLAGS += -std=gnu2x -Wall -Wextra -Werror -pedantic
SOURCES := rrscript.c runtime.c audio.logic.c control.graph.c patchbay.c
OBJECTS := $(addprefix $(BUILD_DIR)/,$(SOURCES:.c=.o))
REAL_LIB := $(BUILD_DIR)/librustyscript.so.$(SOVERSION).$(VERSION)
SONAME := librustyscript.so.$(SOVERSION)
PC_FILE := $(BUILD_DIR)/librustyscript.pc
.PHONY: all test clean install uninstall
all: $(REAL_LIB) $(PC_FILE)
$(BUILD_DIR):
	mkdir -p $@
$(BUILD_DIR)/%.o: %.c rrscript.h runtime.h audio.logic.h control.graph.h patchbay.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@
$(REAL_LIB): $(OBJECTS)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$(SONAME) -o $@ $(OBJECTS) -L$(AXE_BUILD_DIR) -lrustyaxe
	ln -sfn $(notdir $@) $(BUILD_DIR)/$(SONAME)
	ln -sfn $(SONAME) $(BUILD_DIR)/librustyscript.so
$(PC_FILE): GNUmakefile | $(BUILD_DIR)
	@printf '%s\n' 'prefix=$(PREFIX)' 'libdir=$(LIBDIR)' 'includedir=$(INCLUDEDIR)' '' \
	 'Name: librustyscript' 'Description: RustyScript scripting runtime' \
	 'Version: $(VERSION)' 'Requires: librustyaxe' \
	 'Libs: -L$${libdir} -lrustyscript' 'Cflags: -I$${includedir}' > $@
	@grep -Fqx 'Libs: -L$${libdir} -lrustyscript' $(PC_FILE)
	@grep -Fqx 'Cflags: -I$${includedir}' $(PC_FILE)
$(BUILD_DIR)/test_syntax: rrscript.c tests/test_syntax.c rrscript.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) rrscript.c tests/test_syntax.c -o $@
$(BUILD_DIR)/test_runtime: runtime.c rrscript.c tests/test_runtime.c runtime.h rrscript.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) runtime.c rrscript.c tests/test_runtime.c -o $@
$(BUILD_DIR)/test_audio_logic: audio.logic.c tests/test_audio_logic.c audio.logic.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) audio.logic.c tests/test_audio_logic.c -o $@
$(BUILD_DIR)/test_control_graph: control.graph.c audio.logic.c tests/test_control_graph.c control.graph.h audio.logic.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) control.graph.c audio.logic.c tests/test_control_graph.c -o $@
$(BUILD_DIR)/test_patchbay: patchbay.c tests/test_patchbay.c patchbay.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) patchbay.c tests/test_patchbay.c -o $@
test: $(BUILD_DIR)/test_syntax $(BUILD_DIR)/test_runtime $(BUILD_DIR)/test_audio_logic $(BUILD_DIR)/test_control_graph $(BUILD_DIR)/test_patchbay
	$(BUILD_DIR)/test_syntax
	$(BUILD_DIR)/test_runtime
	$(BUILD_DIR)/test_audio_logic
	$(BUILD_DIR)/test_control_graph
	$(BUILD_DIR)/test_patchbay
install: all
	install -Dm755 $(REAL_LIB) "$(DESTDIR)$(LIBDIR)/$(notdir $(REAL_LIB))"
	ln -sfn $(notdir $(REAL_LIB)) "$(DESTDIR)$(LIBDIR)/$(SONAME)"
	ln -sfn $(SONAME) "$(DESTDIR)$(LIBDIR)/librustyscript.so"
	install -d "$(DESTDIR)$(INCLUDEDIR)/librustyscript"
	install -m644 rrscript.h runtime.h audio.logic.h control.graph.h patchbay.h "$(DESTDIR)$(INCLUDEDIR)/librustyscript/"
	install -Dm644 $(PC_FILE) "$(DESTDIR)$(LIBDIR)/pkgconfig/librustyscript.pc"
uninstall:
	rm -f "$(DESTDIR)$(LIBDIR)/$(notdir $(REAL_LIB))" "$(DESTDIR)$(LIBDIR)/$(SONAME)" "$(DESTDIR)$(LIBDIR)/librustyscript.so" "$(DESTDIR)$(LIBDIR)/pkgconfig/librustyscript.pc"
	rm -f "$(DESTDIR)$(INCLUDEDIR)/librustyscript/rrscript.h" "$(DESTDIR)$(INCLUDEDIR)/librustyscript/runtime.h" "$(DESTDIR)$(INCLUDEDIR)/librustyscript/audio.logic.h" "$(DESTDIR)$(INCLUDEDIR)/librustyscript/control.graph.h" "$(DESTDIR)$(INCLUDEDIR)/librustyscript/patchbay.h"
	-rmdir "$(DESTDIR)$(INCLUDEDIR)/librustyscript"
clean:
	rm -rf "$(BUILD_DIR)"
