COMPONENT_NAME = PulseReport

LIBMCU_ROOT = ../build/_deps/external_libmcu-src

SRC_FILES = \
	../src/pulse.c \
	$(LIBMCU_ROOT)/modules/metrics/src/metrics.c \
	$(LIBMCU_ROOT)/modules/metrics/src/metrics_overrides.c \

TEST_SRC_FILES = \
	src/pulse_report_test.cpp \
	src/test_all.cpp \
	stubs/metricfs_stub.c \
	stubs/assert_stub.c \

INCLUDE_DIRS = \
	$(CPPUTEST_HOME)/include \
	../include \
	stubs \
	$(LIBMCU_ROOT)/modules/metrics/include \
	$(LIBMCU_ROOT)/modules/common/include \
	$(LIBMCU_ROOT)/interfaces/kvstore/include \

MOCKS_SRC_DIRS =

CPPUTEST_CPPFLAGS = \
	-include $(LIBMCU_ROOT)/modules/metrics/include/libmcu/metrics_overrides.h \
	-DMETRICS_USER_DEFINES=\"../examples/metrics.def\" \
	-DLIBMCU_NOINIT= \
	-Wno-error=unused-macros

CPPUTEST_WARNINGFLAGS += -Wno-error=unused-macros

include runners/MakefileRunner
