PULSE_SDK_ROOT ?= ../..
LIBMCU_ROOT   ?= $(PULSE_SDK_ROOT)/external/libmcu
CBOR_ROOT     ?= $(PULSE_SDK_ROOT)/external/cbor

include $(PULSE_SDK_ROOT)/pulse-sdk.mk

CC      ?= gcc
CPPFLAGS += $(addprefix -I,$(PULSE_SDK_INCS)) \
	    -I$(PULSE_SDK_ROOT)/examples \
	    -DMETRICS_USER_DEFINES=\"metrics.def\"
CFLAGS  += -std=c99 -Wall -Wextra
LDFLAGS ?=

SRCS := src/main.c src/syscalls.c $(PULSE_SDK_SRCS)
OBJS := $(patsubst %.c,build/%.o,$(SRCS))

.PHONY: all clean

all: build/example_pulse_baremetal

build/example_pulse_baremetal: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf build
