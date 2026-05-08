COMPONENT_NAME = PulseReport

LIBMCU_ROOT ?= $(firstword $(wildcard ../build/_deps/libmcu-src ../build/_deps/external_libmcu-src))

ifeq ($(LIBMCU_ROOT),)
$(error LIBMCU_ROOT not found in ../build/_deps; run CMake configure first)
endif

CBOR_ROOT ?= $(firstword $(wildcard ../build/_deps/cbor-src ../build/_deps/external_cbor-src))

ifeq ($(CBOR_ROOT),)
$(error CBOR_ROOT not found in ../build/_deps; run CMake configure first)
endif

PULSE_REPORT_RATELIM_DISABLED ?= 0

LIBMCU_MODULES := metrics
ifeq ($(PULSE_REPORT_RATELIM_DISABLED),0)
LIBMCU_MODULES += ratelim
endif

include $(LIBMCU_ROOT)/project/modules.mk
include $(CBOR_ROOT)/cbor.mk

SRC_FILES = \
	../src/pulse.c \
	../src/pulse_codec.c \
	$(CBOR_SRCS) \
	$(filter-out %/metricfs.c %/assert.c,$(LIBMCU_MODULES_SRCS)) \

TEST_SRC_FILES = \
	src/pulse_codec_test.cpp \
	src/pulse_report_test.cpp \
	src/test_all.cpp \
	stubs/metricfs_stub.c \
	stubs/assert_stub.c \

INCLUDE_DIRS = \
	$(CPPUTEST_HOME)/include \
	../include \
	stubs \
	$(LIBMCU_MODULES_INCS) \
	$(LIBMCU_ROOT)/interfaces/kvstore/include \
	$(CBOR_INCS) \

MOCKS_SRC_DIRS =

CPPUTEST_CPPFLAGS = \
	-include $(LIBMCU_ROOT)/modules/metrics/include/libmcu/metrics_overrides.h \
	-DMETRICS_USER_DEFINES=\"../examples/metrics.def\" \
	-DLIBMCU_NOINIT= \
	-Wno-error=unused-macros

ifneq ($(PULSE_REPORT_RATELIM_DISABLED),0)
CPPUTEST_CPPFLAGS += \
	-DPULSE_RATELIM_CAPACITY=0u \
	-DPULSE_RATELIM_LEAK_RATE=0u
endif

CPPUTEST_WARNINGFLAGS += -Wno-error=unused-macros

include runners/MakefileRunner
