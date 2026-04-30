cmake_minimum_required(VERSION 3.16)
project(pulse-example-linux LANGUAGES C)

set(PULSE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(METRICS_USER_DEFINES "${PULSE_ROOT}/examples/metrics.def")

add_subdirectory(${PULSE_ROOT} pulse)

add_executable(example_pulse_linux
	main.c)

target_link_libraries(example_pulse_linux PRIVATE pulse)
