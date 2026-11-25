CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L -O0 -g
# Always use the librustyaxe checkout's own generated headers and binaries.
REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
INCLUDES = -I$(REPO_ROOT)/build/generated -I$(REPO_ROOT)/build/include $(shell pkg-config --cflags glib-2.0 gio-2.0)
# Link against the independently built RustyAxe shared library.
LIBS = -L$(REPO_ROOT)/build -lrustyaxe -Wl,-rpath,$(REPO_ROOT)/build

TEST_SOURCES = test_json.c test_dict.c test_list.c test_maidenhead.c test_ringbuffer.c test_util_time.c test_config_save.c test_config_includes.c test_event_tokens.c test_event_messages.c test_serial_config.c test_runtime_globals.c test_transport_wss.c

# Keep test artifacts out of the source directory. The top-level build already
# owns generated files, so running a test suite must not leave .o files beside
# the test sources.
TEST_BUILD_DIR ?= $(REPO_ROOT)/build/tests/librustyaxe


# Object files and test executables
TEST_OBJECTS = $(addprefix $(TEST_BUILD_DIR)/,$(TEST_SOURCES:.c=.o))
GLOBAL_OBJECT = $(TEST_BUILD_DIR)/test_globals.o

# Test executables, one per source file
TARGETS = $(addprefix $(TEST_BUILD_DIR)/,$(TEST_SOURCES:.c=))
TEST_NAMES = $(TEST_SOURCES:.c=)

all: $(TARGETS) $(TEST_BUILD_DIR)/test_log_queue $(TEST_BUILD_DIR)/test_transport_tcp

# Preserve the convenient `make test_dict` style without placing the
# executable in the source directory.
.PHONY: $(TEST_NAMES)
$(TEST_NAMES): %: $(TEST_BUILD_DIR)/%
	$<

# Each test links against the prebuilt shared library
$(TEST_BUILD_DIR)/test_%: $(TEST_BUILD_DIR)/test_%.o $(GLOBAL_OBJECT)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(if $(filter %/test_util_time,$@),$<,$^) $(LIBS)

# Confirm a host application's strong process-state symbols override the
# library's weak standalone fallback definitions.
$(TEST_BUILD_DIR)/test_runtime_globals: $(TEST_BUILD_DIR)/test_runtime_globals.o
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS)

# Threaded message delivery test uses pthreads.
$(TEST_BUILD_DIR)/test_event_messages: LIBS += -pthread

# maidenhead test needs libm for fabs()
$(TEST_BUILD_DIR)/test_maidenhead: LIBS += -lm
$(TEST_BUILD_DIR)/test_transport_wss: LIBS += -lcrypto

# End-to-end SOCKS5h with an authenticated/verified native TLS transport.
$(TEST_BUILD_DIR)/test_transport_tcp: $(TEST_BUILD_DIR)/test_transport_tcp.o
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) -lssl -lcrypto

$(TEST_BUILD_DIR):
	mkdir -p $@

$(TEST_BUILD_DIR)/%.o: %.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TEST_BUILD_DIR)/test_log_queue: log-queue.c ../log-queue.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -pthread -o $@ $^

test tests: all
	@bash ./run-check.sh "$(TEST_BUILD_DIR)" $(TARGETS) $(TEST_BUILD_DIR)/test_log_queue

clean:
	rm -rf $(TEST_BUILD_DIR)

.PHONY: all clean test tests
