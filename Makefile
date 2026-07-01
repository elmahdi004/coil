# Coil build.
#   make            -- build the `coil` command-line driver
#   make test       -- build and run tests/*.c under the sanitizers
#   make clean

CC      ?= cc
CFLAGS  ?= -Wall -Wextra -std=c11 -O2 -I src
SANFLAGS := -Wall -Wextra -std=c11 -g -fsanitize=address,undefined -I src
LDLIBS  ?= -lm

SRCS  := $(wildcard src/*.c)
TESTS := $(patsubst tests/%.c,%,$(wildcard tests/*.c))

.PHONY: all test clean

all: coil

coil: main.c $(SRCS)
	$(CC) $(CFLAGS) main.c $(SRCS) $(LDLIBS) -o $@

# Each test links against the whole implementation and runs under ASan/UBSan.
test:
	@fail=0; for t in $(TESTS); do \
	    echo "== $$t"; \
	    $(CC) $(SANFLAGS) tests/$$t.c $(SRCS) $(LDLIBS) -o /tmp/coil_$$t 2>/dev/null \
	        && /tmp/coil_$$t || fail=1; \
	done; \
	[ $$fail -eq 0 ] && echo "all tests passed" || { echo "TESTS FAILED"; exit 1; }

clean:
	rm -f coil src/*.o
