# RustyScript runtime foundation. SPDX-License-Identifier: MIT
CC ?= cc
CFLAGS += -std=gnu23 -Wall -Wextra -Werror -pedantic
.PHONY: all test clean
all: build/rrscript.o build/runtime.o
build:
	mkdir -p build
build/rrscript.o: rrscript.c rrscript.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c rrscript.c -o $@
build/runtime.o: runtime.c runtime.h rrscript.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c runtime.c -o $@
build/test_syntax: rrscript.c rrscript.h tests/test_syntax.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) rrscript.c tests/test_syntax.c -o $@
build/test_runtime: runtime.c runtime.h rrscript.c rrscript.h tests/test_runtime.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) runtime.c rrscript.c tests/test_runtime.c -o $@
test: build/test_syntax build/test_runtime
	./build/test_syntax
	./build/test_runtime
clean:
	rm -rf build
