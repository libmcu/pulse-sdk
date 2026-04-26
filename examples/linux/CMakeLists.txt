cmake_minimum_required(VERSION 3.16)
project(pulse-sdk-example-linux LANGUAGES C)

set(PULSE_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

find_package(CURL REQUIRED)

include(${PULSE_SDK_ROOT}/pulse-sdk.cmake)

pulse_sdk_collect(PULSE_SDK_SRCS PULSE_SDK_PUBLIC_INCS PULSE_SDK_PRIVATE_INCS)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
	list(FILTER PULSE_SDK_SRCS EXCLUDE REGEX "ports/baremetal/pulse_overrides\\.c$")
	list(FILTER PULSE_SDK_SRCS EXCLUDE REGEX "ports/baremetal/pulse_transport_https\\.c$")
	list(APPEND PULSE_SDK_SRCS
		${PULSE_SDK_ROOT}/ports/linux/pulse_overrides.c
		${PULSE_SDK_ROOT}/ports/linux/pulse_transport_https.c)
endif()

list(REMOVE_DUPLICATES PULSE_SDK_SRCS)

add_library(pulse-sdk STATIC ${PULSE_SDK_SRCS})

target_compile_features(pulse-sdk PRIVATE c_std_99)

target_include_directories(pulse-sdk
	PUBLIC ${PULSE_SDK_PUBLIC_INCS}
	PRIVATE ${PULSE_SDK_PRIVATE_INCS})

target_link_libraries(pulse-sdk PUBLIC CURL::libcurl)

target_compile_definitions(pulse-sdk
	PUBLIC METRICS_USER_DEFINES="${PULSE_SDK_ROOT}/examples/metrics.def")

add_executable(example_pulse_linux
	main.c)

target_link_libraries(example_pulse_linux PRIVATE pulse-sdk)
