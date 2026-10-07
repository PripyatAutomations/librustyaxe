CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L -O0 -g
LIB_DIR = ..
INCLUDES = -I$(LIB_DIR) -I$(LIB_DIR)/.. -I../../build/radio
# Link against the prebuilt shared library instead of recompiling the whole lib
LIBS = -L../.. -lrustyaxe -Wl,-rpath,$(abspath ../..)

TEST_SOURCES = test_mirc.c test_json.c test_dict.c test_list.c test_maidenhead.c test_ringbuffer.c test_util_time.c test_config_save.c test_event_tokens.c test_serial_config.c test_completion_labels.c

# Keep test artifacts out of the source directory. The top-level build already
# owns generated files, so running a test suite must not leave .o files beside
# the test sources.
TEST_BUILD_DIR ?= ../../build/tests/librustyaxe


# Object files and test executables
TEST_OBJECTS = $(addprefix $(TEST_BUILD_DIR)/,$(TEST_SOURCES:.c=.o))
GLOBAL_OBJECT = $(TEST_BUILD_DIR)/test_globals.o

# Test executables, one per source file
TARGETS = $(addprefix $(TEST_BUILD_DIR)/,$(TEST_SOURCES:.c=))
TEST_NAMES = $(TEST_SOURCES:.c=)

all: $(TARGETS)

# Preserve the convenient `make test_dict` style without placing the
# executable in the source directory.
.PHONY: $(TEST_NAMES)
$(TEST_NAMES): %: $(TEST_BUILD_DIR)/%
	$<

# Each test links against the prebuilt shared library
$(TEST_BUILD_DIR)/test_%: $(TEST_BUILD_DIR)/test_%.o $(GLOBAL_OBJECT)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(if $(filter %/test_util_time,$@),$<,$^) $(LIBS)

# maidenhead test needs libm for fabs()
$(TEST_BUILD_DIR)/test_maidenhead: LIBS += -lm

$(TEST_BUILD_DIR):
	mkdir -p $@

$(TEST_BUILD_DIR)/%.o: %.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

check: all
	@fail=0; for t in $(TARGETS); do ./$$t || fail=1; done; \
	$(MAKE) clean; \
	exit $$fail

clean:
	rm -rf $(TEST_BUILD_DIR)

.PHONY: all clean check
