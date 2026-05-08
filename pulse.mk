# SPDX-License-Identifier: MIT

PULSE_ROOT ?= $(dir $(lastword $(MAKEFILE_LIST)))

ifneq ($(PULSE_LIBMCU_ROOT),)
LIBMCU_ROOT := $(PULSE_LIBMCU_ROOT)
endif
ifneq ($(PULSE_CBOR_ROOT),)
CBOR_ROOT := $(PULSE_CBOR_ROOT)
endif

ifeq ($(LIBMCU_ROOT),)
ifneq ($(wildcard $(PULSE_ROOT)/external/libmcu/project/interfaces.mk),)
LIBMCU_ROOT := $(PULSE_ROOT)/external/libmcu
endif
endif

ifeq ($(CBOR_ROOT),)
ifneq ($(wildcard $(PULSE_ROOT)/external/cbor/cbor.mk),)
CBOR_ROOT := $(PULSE_ROOT)/external/cbor
endif
endif

ifndef LIBMCU_ROOT
$(error LIBMCU_ROOT not set and no fallback external/libmcu found)
endif

ifndef CBOR_ROOT
$(error CBOR_ROOT not set and no fallback external/cbor found)
endif

LIBMCU_INTERFACES := uart kvstore

LIBMCU_MODULES ?= metrics
ifeq ($(filter metrics,$(LIBMCU_MODULES)),)
LIBMCU_MODULES += metrics
endif
ifeq ($(filter ratelim,$(LIBMCU_MODULES)),)
LIBMCU_MODULES += ratelim
endif

include $(LIBMCU_ROOT)/project/modules.mk
include $(LIBMCU_ROOT)/project/interfaces.mk
include $(CBOR_ROOT)/cbor.mk

PULSE_CORE_SRCS := \
	$(PULSE_ROOT)/src/pulse.c \
	$(PULSE_ROOT)/src/pulse_codec.c \
	$(PULSE_ROOT)/ports/baremetal/pulse_overrides.c \
	$(PULSE_ROOT)/ports/baremetal/pulse_transport_https.c

PULSE_CBOR_SRCS := $(CBOR_SRCS)

# NOTE: pulse's own baremetal overrides and default HTTPS transport are
# always exported through PULSE_CORE_SRCS. Additional libmcu runtime
# sources are collected automatically only when LIBMCU_ROOT resolves to the
# bundled external/libmcu path. When an external LIBMCU_ROOT is supplied, the
# caller must make sure the selected libmcu module sources are linked.
ifeq ($(realpath $(LIBMCU_ROOT)),$(realpath $(PULSE_ROOT)/external/libmcu))
	PULSE_CORE_SRCS += $(LIBMCU_MODULES_SRCS)
endif

PULSE_SRCS ?= $(PULSE_CORE_SRCS) $(PULSE_CBOR_SRCS)

PULSE_INCS := \
	$(PULSE_ROOT)/include \
	$(LIBMCU_INTERFACES_INCS) \
	$(LIBMCU_MODULES_INCS) \
	$(CBOR_INCS)
