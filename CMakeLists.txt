cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/pulse-sdk.cmake)

if(COMMAND idf_component_register)
	pulse_sdk_collect(PULSE_SDK_SRCS PULSE_SDK_PUBLIC_INCS PULSE_SDK_PRIVATE_INCS)

	idf_component_register(
		SRCS ${PULSE_SDK_SRCS}
		INCLUDE_DIRS ${PULSE_SDK_PUBLIC_INCS}
		PRIV_INCLUDE_DIRS ${PULSE_SDK_PRIVATE_INCS}
		REQUIRES esp_http_client mbedtls
		PRIV_REQUIRES esp_timer
	)

	target_compile_features(${COMPONENT_LIB} PRIVATE c_std_99)

	if(NOT DEFINED METRICS_USER_DEFINES AND DEFINED PROJECT_DIR AND
			EXISTS "${PROJECT_DIR}/main/metrics.def")
		target_compile_definitions(${COMPONENT_LIB}
			PRIVATE METRICS_USER_DEFINES="${PROJECT_DIR}/main/metrics.def"
		)
	endif()

	return()
endif()

project(pulse-sdk LANGUAGES C)

pulse_sdk_collect(PULSE_SDK_SRCS PULSE_SDK_PUBLIC_INCS PULSE_SDK_PRIVATE_INCS)

add_library(${PROJECT_NAME} STATIC ${PULSE_SDK_SRCS})
add_library(pulse::sdk ALIAS ${PROJECT_NAME})

target_compile_features(${PROJECT_NAME} PRIVATE c_std_99)
target_include_directories(${PROJECT_NAME}
	PUBLIC ${PULSE_SDK_PUBLIC_INCS}
	PRIVATE ${PULSE_SDK_PRIVATE_INCS}
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	target_compile_definitions(${PROJECT_NAME}
		PRIVATE METRICS_USER_DEFINES="${CMAKE_CURRENT_SOURCE_DIR}/examples/metrics.def"
	)

	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		set(_PULSE_EXAMPLE_NAME example_pulse_linux)
	else()
		set(_PULSE_EXAMPLE_NAME example_pulse_baremetal)
	endif()

	add_executable(${_PULSE_EXAMPLE_NAME} examples/linux/main.c)
	target_include_directories(${_PULSE_EXAMPLE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples)
	target_link_libraries(${_PULSE_EXAMPLE_NAME} PRIVATE ${PROJECT_NAME})
endif()

if(BUILD_TESTING)
	include(CTest)
	enable_language(CXX)
	set(CPPUTEST_BUILD_TESTING OFF CACHE BOOL "" FORCE)
	set(CPPUTEST_EXAMPLES OFF CACHE BOOL "" FORCE)
	add_subdirectory(tests/cpputest EXCLUDE_FROM_ALL)

	# Platform-specific and storage files are excluded so tests supply their own stubs.
	set(_test_srcs ${PULSE_SDK_SRCS})
	list(FILTER _test_srcs EXCLUDE REGEX "pulse_overrides\\.c$")
	list(FILTER _test_srcs EXCLUDE REGEX "pulse_transport_https\\.c$")
	list(FILTER _test_srcs EXCLUDE REGEX "metricfs\\.c$")
	add_library(pulse-sdk-testable STATIC ${_test_srcs})
	target_compile_features(pulse-sdk-testable PRIVATE c_std_99)
	target_include_directories(pulse-sdk-testable
		PUBLIC ${PULSE_SDK_PUBLIC_INCS}
		PRIVATE ${PULSE_SDK_PRIVATE_INCS})
	target_compile_definitions(pulse-sdk-testable
		PRIVATE METRICS_USER_DEFINES="${CMAKE_CURRENT_SOURCE_DIR}/examples/metrics.def")

	add_executable(test_pulse_report_cpp
		tests/src/test_all.cpp
		tests/src/pulse_report_test.cpp
		tests/stubs/metricfs_stub.c)
	target_compile_definitions(test_pulse_report_cpp
		PRIVATE METRICS_USER_DEFINES="${CMAKE_CURRENT_SOURCE_DIR}/examples/metrics.def"
	)
	target_include_directories(test_pulse_report_cpp PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/tests/stubs)
	target_link_libraries(test_pulse_report_cpp PRIVATE
		pulse-sdk-testable
		CppUTest::CppUTest
		CppUTest::CppUTestExt)
	add_test(NAME test_pulse_report_cpp COMMAND test_pulse_report_cpp)

	add_executable(test_pulse_transport_https_esp_idf
		tests/src/test_all.cpp
		tests/src/pulse_transport_https_esp_idf_test.cpp
		tests/mocks/esp_http_client_mock.cpp
		tests/mocks/esp_timer_mock.cpp
		ports/esp-idf/pulse_transport_https.c)
	target_compile_definitions(test_pulse_transport_https_esp_idf
		PRIVATE
		METRICS_USER_DEFINES="${CMAKE_CURRENT_SOURCE_DIR}/examples/metrics.def"
		UNIT_TEST
	)
	target_include_directories(test_pulse_transport_https_esp_idf PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/tests/mocks)
	target_link_libraries(test_pulse_transport_https_esp_idf PRIVATE
		pulse::sdk
		CppUTest::CppUTest
		CppUTest::CppUTestExt)
	add_test(NAME test_pulse_transport_https_esp_idf
		COMMAND test_pulse_transport_https_esp_idf)
endif()
