CC       ?= cc
CFLAGS   ?= -O2 -g
LDFLAGS  ?=
WARN      = -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes
STD       = -std=c11 -D_POSIX_C_SOURCE=200809L
SANITIZE  = -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all

PREFIX   ?= /usr/local
BUILD    ?= build
PLIKE    ?= bin/plike

LIB       = $(wildcard src/util/*.c src/front/*.c src/sema/*.c src/back/*.c)
CLI       = $(wildcard src/cli/*.c)
UNIT      = $(wildcard tests/unit/*.c)

LIB_OBJ   = $(LIB:%.c=$(BUILD)/%.o)
CLI_OBJ   = $(CLI:%.c=$(BUILD)/%.o)
UNIT_OBJ  = $(UNIT:%.c=$(BUILD)/%.o)

.PHONY: all test unit e2e sanitize fuzz install uninstall clean

all: $(PLIKE)

$(PLIKE): $(LIB_OBJ) $(CLI_OBJ)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

$(BUILD)/unit: $(LIB_OBJ) $(UNIT_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(STD) $(WARN) -Iinclude $(CFLAGS) -MMD -MP -c $< -o $@

test: unit e2e

unit: $(BUILD)/unit
	$(BUILD)/unit

e2e: $(PLIKE)
	PLIKE=$(PLIKE) tests/run.sh

# Everything again, with the translator itself under sanitizers.
sanitize:
	$(MAKE) test BUILD=build-san PLIKE=build-san/plike CFLAGS="$(SANITIZE)"

# Mutated programs must never crash the translator.
fuzz:
	$(MAKE) build-san/plike BUILD=build-san PLIKE=build-san/plike CFLAGS="$(SANITIZE)"
	python3 tests/fuzz.py build-san/plike

install: $(PLIKE)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(PLIKE) $(DESTDIR)$(PREFIX)/bin/plike

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/plike

clean:
	rm -rf $(BUILD) build-san bin

-include $(LIB_OBJ:.o=.d) $(CLI_OBJ:.o=.d) $(UNIT_OBJ:.o=.d)
