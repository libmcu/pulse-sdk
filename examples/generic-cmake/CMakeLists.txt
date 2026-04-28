cmake_minimum_required(VERSION 3.16)
project(pulse-sdk-example-generic-cmake LANGUAGES C)

set(PULSE_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(METRICS_USER_DEFINES "${PULSE_SDK_ROOT}/examples/metrics.def")

add_subdirectory(${PULSE_SDK_ROOT} pulse-sdk)

add_executable(example_pulse
	src/main.c)

target_link_libraries(example_pulse PRIVATE pulse-sdk)
