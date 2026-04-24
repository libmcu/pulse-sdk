# SPDX-License-Identifier: MIT

PULSE_SDK_ROOT ?= $(dir $(lastword $(MAKEFILE_LIST)))

ifneq ($(PULSE_SDK_LIBMCU_ROOT),)
LIBMCU_ROOT := $(PULSE_SDK_LIBMCU_ROOT)
endif
ifneq ($(PULSE_SDK_CBOR_ROOT),)
CBOR_ROOT := $(PULSE_SDK_CBOR_ROOT)
endif

ifeq ($(LIBMCU_ROOT),)
ifneq ($(wildcard $(PULSE_SDK_ROOT)/external/libmcu/project/interfaces.mk),)
LIBMCU_ROOT := $(PULSE_SDK_ROOT)/external/libmcu
endif
endif

ifeq ($(CBOR_ROOT),)
ifneq ($(wildcard $(PULSE_SDK_ROOT)/external/cbor/cbor.mk),)
CBOR_ROOT := $(PULSE_SDK_ROOT)/external/cbor
endif
endif

ifndef LIBMCU_ROOT
$(error LIBMCU_ROOT not set and no fallback external/libmcu found)
endif

ifndef CBOR_ROOT
$(error CBOR_ROOT not set and no fallback external/cbor found)
endif

LIBMCU_INTERFACES := uart kvstore

include $(LIBMCU_ROOT)/project/interfaces.mk
include $(CBOR_ROOT)/cbor.mk

PULSE_SDK_SRCS := \
	$(PULSE_SDK_ROOT)/src/pulse.c \
	$(CBOR_SRCS)

# NOTE: metrics core sources, platform overrides, and transport are only
# collected automatically when LIBMCU_ROOT resolves to the bundled
# external/libmcu path. When an external LIBMCU_ROOT is supplied, the caller
# must manually add the following to the build:
#   $(LIBMCU_ROOT)/modules/metrics/src/metrics.c
#   $(LIBMCU_ROOT)/modules/metrics/src/metrics_overrides.c
#   $(LIBMCU_ROOT)/modules/common/src/assert.c
#   $(LIBMCU_ROOT)/ports/metrics/cbor_encoder.c  (if present)
#   $(PULSE_SDK_ROOT)/ports/baremetal/pulse_overrides.c
#   $(PULSE_SDK_ROOT)/ports/baremetal/pulse_transport_https.c
#   $(PULSE_SDK_ROOT)/ports/pulse_metricfs_stub.c
ifeq ($(realpath $(LIBMCU_ROOT)),$(realpath $(PULSE_SDK_ROOT)/external/libmcu))
	PULSE_SDK_SRCS += $(PULSE_SDK_ROOT)/ports/pulse_metricfs_stub.c
	PULSE_SDK_SRCS += $(LIBMCU_ROOT)/modules/common/src/assert.c
	PULSE_SDK_SRCS += $(LIBMCU_ROOT)/modules/metrics/src/metrics.c
	PULSE_SDK_SRCS += $(LIBMCU_ROOT)/modules/metrics/src/metrics_overrides.c
	PULSE_SDK_SRCS += $(PULSE_SDK_ROOT)/ports/baremetal/pulse_overrides.c
	PULSE_SDK_SRCS += $(PULSE_SDK_ROOT)/ports/baremetal/pulse_transport_https.c
	PULSE_SDK_SRCS += $(LIBMCU_ROOT)/ports/metrics/cbor_encoder.c
endif

PULSE_SDK_INCS := \
	$(PULSE_SDK_ROOT)/include \
	$(LIBMCU_INTERFACES_INCS) \
	$(LIBMCU_ROOT)/modules/common/include \
	$(LIBMCU_ROOT)/modules/metrics/include \
	$(CBOR_INCS)
