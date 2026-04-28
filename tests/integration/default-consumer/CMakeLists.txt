cmake_minimum_required(VERSION 3.16)
project(pulse_sdk_default_consumer LANGUAGES C)

set(METRICS_USER_DEFINES "${CMAKE_CURRENT_SOURCE_DIR}/metrics.def")
set(PULSE_SDK_TRANSPORT https CACHE STRING "")

add_subdirectory(${PULSE_SDK_ROOT} pulse-sdk)

add_executable(default_consumer
	main.c)

target_link_libraries(default_consumer PRIVATE
	pulse-sdk
	pulse::dep::libmcu)
