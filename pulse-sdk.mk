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

PULSE_SDK_CORE_SRCS := \
	$(PULSE_SDK_ROOT)/src/pulse.c \
	$(PULSE_SDK_ROOT)/src/pulse_codec.c \
	$(PULSE_SDK_ROOT)/src/pulse_metrics_cbor_encoder.c \
	$(PULSE_SDK_ROOT)/ports/baremetal/pulse_overrides.c \
	$(PULSE_SDK_ROOT)/ports/baremetal/pulse_transport_https.c

PULSE_SDK_CBOR_SRCS := $(CBOR_SRCS)

PULSE_SDK_LDFLAGS ?= -Wl,-u,pulse_metrics_cbor_encoder_link_anchor

# NOTE: pulse-sdk's own baremetal overrides and default HTTPS transport are
# always exported through PULSE_SDK_CORE_SRCS. Additional libmcu runtime
# sources are collected automatically only when LIBMCU_ROOT resolves to the
# bundled external/libmcu path. When an external LIBMCU_ROOT is supplied, the
# caller must manually add the following libmcu sources to the build:
#   $(LIBMCU_ROOT)/modules/metrics/src/metrics.c
#   $(LIBMCU_ROOT)/modules/metrics/src/metricfs.c
#   $(LIBMCU_ROOT)/modules/common/src/assert.c
ifeq ($(realpath $(LIBMCU_ROOT)),$(realpath $(PULSE_SDK_ROOT)/external/libmcu))
	PULSE_SDK_CORE_SRCS += $(LIBMCU_ROOT)/modules/common/src/assert.c
	PULSE_SDK_CORE_SRCS += $(LIBMCU_ROOT)/modules/metrics/src/metrics.c
	PULSE_SDK_CORE_SRCS += $(LIBMCU_ROOT)/modules/metrics/src/metricfs.c
endif

PULSE_SDK_SRCS ?= $(PULSE_SDK_CORE_SRCS) $(PULSE_SDK_CBOR_SRCS)

PULSE_SDK_INCS := \
	$(PULSE_SDK_ROOT)/include \
	$(LIBMCU_INTERFACES_INCS) \
	$(LIBMCU_ROOT)/modules/common/include \
	$(LIBMCU_ROOT)/modules/metrics/include \
	$(CBOR_INCS)
