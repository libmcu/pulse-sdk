COMPONENT_NAME = PulseTransportCoapsEspIdf

LIBMCU_ROOT ?= $(firstword $(wildcard ../build/_deps/libmcu-src ../build/_deps/external_libmcu-src))

ifeq ($(LIBMCU_ROOT),)
$(error LIBMCU_ROOT not found in ../build/_deps; run CMake configure first)
endif

SRC_FILES = \
	../ports/esp-idf/pulse_transport_coaps.c \
	$(LIBMCU_ROOT)/modules/common/src/base64.c \

TEST_SRC_FILES = \
	src/pulse_transport_coaps_esp_idf_test.cpp \
	src/test_all.cpp \
	mocks/coap_mock.cpp \
	mocks/openssl_mock.cpp \
	mocks/psa_crypto_mock.cpp \

INCLUDE_DIRS = \
	$(CPPUTEST_HOME)/include \
	../include \
	$(LIBMCU_ROOT)/modules/metrics/include \
	$(LIBMCU_ROOT)/modules/common/include \
	$(LIBMCU_ROOT)/interfaces/kvstore/include \
	mocks \

MOCKS_SRC_DIRS =

CPPUTEST_CPPFLAGS = \
	-include $(abspath $(LIBMCU_ROOT)/modules/metrics/include/libmcu/metrics_overrides.h) \
	-DMETRICS_USER_DEFINES=\"$(abspath ../examples/metrics.def)\"

include runners/MakefileRunner
