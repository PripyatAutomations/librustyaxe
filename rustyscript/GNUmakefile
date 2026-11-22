# RustyScript standalone syntax foundation. SPDX-License-Identifier: MIT
CC ?= cc
CFLAGS += -std=gnu23 -Wall -Wextra -Werror -pedantic
.PHONY: all test clean
all: build/rrscript.o
build:
	mkdir -p build
build/rrscript.o: rrscript.c rrscript.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c rrscript.c -o $@
build/test_syntax: rrscript.c rrscript.h tests/test_syntax.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) rrscript.c tests/test_syntax.c -o $@
test: build/test_syntax
	./build/test_syntax
clean:
	rm -rf build
