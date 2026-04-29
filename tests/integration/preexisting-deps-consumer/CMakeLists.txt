cmake_minimum_required(VERSION 3.16)
project(pulse_preexisting_deps_consumer LANGUAGES C)

include(FetchContent)

set(METRICS_USER_DEFINES "${CMAKE_CURRENT_SOURCE_DIR}/metrics.def")
set(METRICS_USER_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

FetchContent_Declare(libmcu
	GIT_REPOSITORY ${LIBMCU_URL}
	GIT_TAG main)
FetchContent_MakeAvailable(libmcu)

FetchContent_Declare(cbor
	GIT_REPOSITORY ${CBOR_URL}
	GIT_TAG main)
FetchContent_MakeAvailable(cbor)

set(PULSE_TRANSPORT https CACHE STRING "")

add_subdirectory(${PULSE_ROOT} pulse)

add_executable(preexisting_deps_consumer
	main.c)

target_link_libraries(preexisting_deps_consumer PRIVATE
	pulse
	libmcu
	cbor)
