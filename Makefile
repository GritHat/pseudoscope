CC       ?= cc
CFLAGS   ?= -O2 -g
WARN      = -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes
STD       = -std=c11 -D_POSIX_C_SOURCE=200809L
PREFIX   ?= /usr/local

BUILD    ?= build
LEGACY    = $(wildcard src/core/*.c) src/util/utils.c src/main.c
LIB       = $(wildcard src/util/*.c src/front/*.c src/sema/*.c src/back/*.c)
LIB      := $(filter-out src/util/utils.c,$(LIB))
UNIT      = $(wildcard tests/unit/*.c)

LIB_OBJ   = $(LIB:%.c=$(BUILD)/%.o)
UNIT_OBJ  = $(UNIT:%.c=$(BUILD)/%.o)

.PHONY: all test unit clean

all: bin/plike

# The original sources, until the new pipeline replaces them.
bin/plike: $(LEGACY)
	@mkdir -p $(@D)
	$(CC) -std=gnu2x -Iinclude -w $(CFLAGS) $(LEGACY) -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(STD) $(WARN) -Iinclude $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/unit: $(LIB_OBJ) $(UNIT_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

unit: $(BUILD)/unit
	$(BUILD)/unit

test: unit

clean:
	rm -rf $(BUILD) bin

-include $(LIB_OBJ:.o=.d) $(UNIT_OBJ:.o=.d)
