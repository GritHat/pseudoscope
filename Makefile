CC       ?= cc
CFLAGS   ?= -O2 -g
LDFLAGS  ?=
WARN      = -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes
STD       = -std=c11 -D_XOPEN_SOURCE=700
SANITIZE  = -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all

PREFIX   ?= /usr/local
BUILD    ?= build
PLIKE    ?= bin/pseudoscope

LIB       = $(wildcard src/util/*.c src/front/*.c src/sema/*.c src/back/*.c src/vm/*.c src/uml/*.c)
CLI       = $(wildcard src/cli/*.c src/tui/*.c)
UNIT      = $(wildcard tests/unit/*.c)

LIB_OBJ   = $(LIB:%.c=$(BUILD)/%.o)
CLI_OBJ   = $(CLI:%.c=$(BUILD)/%.o)
UNIT_OBJ  = $(UNIT:%.c=$(BUILD)/%.o)

.PHONY: all test unit e2e syntax targets sanitize fuzz install uninstall clean

all: $(PLIKE) bin/plike

# The name it had when it only translated.
bin/plike: $(PLIKE)
	@mkdir -p bin
	@ln -sf pseudoscope bin/plike

$(PLIKE): $(LIB_OBJ) $(CLI_OBJ)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ -lm

$(BUILD)/unit: $(LIB_OBJ) $(UNIT_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ -lm

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(STD) $(WARN) -Iinclude $(CFLAGS) -MMD -MP -c $< -o $@

test: unit e2e syntax

unit: $(BUILD)/unit
	$(BUILD)/unit tests/programs/*.plike tests/targets/*.plike examples/*/*.pscope

e2e: $(PLIKE)
	PLIKE=$(PLIKE) tests/run.sh

syntax: $(PLIKE)
	python3 tests/messages.py
	python3 tests/syntax.py $(PLIKE)

# Every program translated to every language whose tools are here, and run.
targets: $(PLIKE)
	python3 tests/targets.py $(PLIKE)

# Everything again, with the translator itself under sanitizers.
sanitize:
	$(MAKE) test BUILD=build-san PLIKE=build-san/pseudoscope CFLAGS="$(SANITIZE)"

# Mutated programs must never crash the translator.
fuzz:
	$(MAKE) build-san/pseudoscope BUILD=build-san PLIKE=build-san/pseudoscope CFLAGS="$(SANITIZE)"
	python3 tests/fuzz.py build-san/pseudoscope

install: $(PLIKE)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(PLIKE) $(DESTDIR)$(PREFIX)/bin/pseudoscope
	install -m 755 tools/pseudoscope-window $(DESTDIR)$(PREFIX)/bin/pseudoscope-window
	ln -sf pseudoscope $(DESTDIR)$(PREFIX)/bin/plike

uninstall:
	rm -f $(addprefix $(DESTDIR)$(PREFIX)/bin/,pseudoscope pseudoscope-window plike)

clean:
	rm -rf $(BUILD) build-san bin

-include $(LIB_OBJ:.o=.d) $(CLI_OBJ:.o=.d) $(UNIT_OBJ:.o=.d)
