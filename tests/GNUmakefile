CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O0 -g
LIB_DIR = ../..
INCLUDES = -I$(LIB_DIR) -I$(LIB_DIR)/include

# Find all source files in librustyaxe directory
SOURCES = $(wildcard $(LIB_DIR)/*.c)
TEST_SOURCES = test_dict.c

# Object files
OBJECTS = $(SOURCES:.c=.o)
TEST_OBJECTS = $(TEST_SOURCES:.c=.o)

# Target executable
TARGET = test_dict

all: $(TARGET)

$(TARGET): $(OBJECTS) $(TEST_OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TEST_OBJECTS) $(TARGET)

.PHONY: all clean
