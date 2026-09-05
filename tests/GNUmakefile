CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O0 -g
LIB_DIR = ..
INCLUDES = -I$(LIB_DIR) -I$(LIB_DIR)/.. -I../../build/radio
# Link against the prebuilt shared library instead of recompiling the whole lib
LIBS = -L../.. -lrustyaxe -Wl,-rpath,$(abspath ../..)

TEST_SOURCES = test_dict.c test_list.c test_maidenhead.c test_ringbuffer.c test_util_time.c

# Object files
TEST_OBJECTS = $(TEST_SOURCES:.c=.o)

# Test executables, one per source file
TARGETS = $(TEST_SOURCES:.c=)

all: $(TARGETS)

# Each test links against the prebuilt shared library
test_%: test_%.o
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

# maidenhead test needs libm for fabs()
test_maidenhead: LIBS += -lm

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

check: all
	@fail=0; for t in $(TARGETS); do ./$$t || fail=1; done; exit $$fail

clean:
	rm -f $(TEST_OBJECTS) $(TARGETS)

.PHONY: all clean check
