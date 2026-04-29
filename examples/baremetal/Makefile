PULSE_ROOT ?= ../..
LIBMCU_ROOT   ?= $(PULSE_ROOT)/external/libmcu
CBOR_ROOT     ?= $(PULSE_ROOT)/external/cbor

include $(PULSE_ROOT)/pulse.mk

CC      ?= gcc
CPPFLAGS += $(addprefix -I,$(PULSE_INCS)) \
	    -I$(PULSE_ROOT)/examples \
	    -DMETRICS_USER_DEFINES=\"metrics.def\"
CFLAGS  += -std=c99 -Wall -Wextra
LDFLAGS ?=

SRCS := src/main.c src/syscalls.c $(PULSE_SRCS)
OBJS := $(patsubst %.c,build/%.o,$(SRCS))

.PHONY: all clean

all: build/example_pulse_baremetal

build/example_pulse_baremetal: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf build
