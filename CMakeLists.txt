cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/pulse.cmake)

if(COMMAND idf_component_register)
	if(CONFIG_PULSE_TRANSPORT_HTTPS)
		set(PULSE_TRANSPORT "https" CACHE STRING "" FORCE)
	else()
		set(PULSE_TRANSPORT "coaps" CACHE STRING "" FORCE)
	endif()

	pulse_collect(PULSE_SRCS PULSE_PUBLIC_INCS PULSE_PRIVATE_INCS PULSE_PUBLIC_DEFS PULSE_PUBLIC_LINKS)

	# idf_component_register REQUIRES/PRIV_REQUIRES are resolved during the
	# early-expansion pass, before Kconfig values are available.  List all
	# possible transport deps unconditionally so both transports link
	# correctly regardless of which pass evaluates this file.
	set(_idf_requires coap mbedtls)
	set(_idf_priv_requires esp_http_client esp_timer)
	_pulse_is_idf_component_available(libmcu _pulse_has_idf_libmcu)
	_pulse_is_idf_component_available(cbor _pulse_has_idf_cbor)
	if(_pulse_has_idf_libmcu)
		list(APPEND _idf_requires libmcu)
	endif()
	if(_pulse_has_idf_cbor)
		list(APPEND _idf_priv_requires cbor)
	endif()

	idf_component_register(
		SRCS ${PULSE_SRCS}
		INCLUDE_DIRS ${PULSE_PUBLIC_INCS}
		PRIV_INCLUDE_DIRS ${PULSE_PRIVATE_INCS}
		REQUIRES ${_idf_requires}
		PRIV_REQUIRES ${_idf_priv_requires}
		KCONFIG "${CMAKE_CURRENT_LIST_DIR}/ports/esp-idf/Kconfig"
	)

	target_compile_features(${COMPONENT_LIB} PRIVATE c_std_99)
	target_compile_definitions(${COMPONENT_LIB} PUBLIC ${PULSE_PUBLIC_DEFS})
	target_link_libraries(${COMPONENT_LIB} PUBLIC ${PULSE_PUBLIC_LINKS})

	if(NOT DEFINED METRICS_USER_DEFINES AND DEFINED PROJECT_DIR AND
			EXISTS "${PROJECT_DIR}/main/metrics.def")
		target_compile_definitions(${COMPONENT_LIB}
			PRIVATE METRICS_USER_DEFINES="${PROJECT_DIR}/main/metrics.def"
		)
	endif()

	return()
endif()

project(pulse LANGUAGES C)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND
		NOT DEFINED PULSE_METRICS_USER_DEFINES AND
		NOT DEFINED METRICS_USER_DEFINES)
	set(METRICS_USER_DEFINES "${CMAKE_CURRENT_SOURCE_DIR}/examples/metrics.def")
endif()

pulse_collect_cmake(
	PULSE_SRCS
	PULSE_PUBLIC_INCS
	PULSE_PRIVATE_INCS
	PULSE_PUBLIC_DEFS
	PULSE_PUBLIC_LINKS
	PULSE_PRIVATE_LINKS
)

set(_pulse_encoder_src "${PULSE_ROOT}/src/pulse_metrics_cbor_encoder.c")
list(REMOVE_ITEM PULSE_SRCS "${_pulse_encoder_src}")

add_library(pulse_cbor_encoder_obj OBJECT "${_pulse_encoder_src}")
target_compile_features(pulse_cbor_encoder_obj PRIVATE c_std_99)
target_link_libraries(pulse_cbor_encoder_obj PRIVATE
	pulse::libmcu
	pulse::cbor
)

add_library(pulse_cbor_encoder INTERFACE)
target_sources(pulse_cbor_encoder INTERFACE
	$<TARGET_OBJECTS:pulse_cbor_encoder_obj>
)

add_library(${PROJECT_NAME} STATIC ${PULSE_SRCS})

target_compile_features(${PROJECT_NAME} PRIVATE c_std_99)
target_include_directories(${PROJECT_NAME}
	PUBLIC ${PULSE_PUBLIC_INCS}
	PRIVATE ${PULSE_PRIVATE_INCS}
)
target_compile_definitions(${PROJECT_NAME} PUBLIC ${PULSE_PUBLIC_DEFS})

if(DEFINED PULSE_METRICS_USER_DEFINES AND
		NOT PULSE_METRICS_USER_DEFINES STREQUAL "")
	target_compile_definitions(${PROJECT_NAME}
		PUBLIC METRICS_USER_DEFINES="${PULSE_METRICS_USER_DEFINES}"
	)
	set(_pulse_metrics_user_defines "${PULSE_METRICS_USER_DEFINES}")
elseif(DEFINED METRICS_USER_DEFINES AND NOT METRICS_USER_DEFINES STREQUAL "")
	target_compile_definitions(${PROJECT_NAME}
		PUBLIC METRICS_USER_DEFINES="${METRICS_USER_DEFINES}"
	)
	set(_pulse_metrics_user_defines "${METRICS_USER_DEFINES}")
endif()

if(DEFINED _pulse_metrics_user_defines AND
		NOT _pulse_metrics_user_defines STREQUAL "")
	target_compile_definitions(pulse_cbor_encoder_obj PRIVATE
		METRICS_USER_DEFINES="${_pulse_metrics_user_defines}"
	)
endif()

target_link_libraries(${PROJECT_NAME}
	PUBLIC ${PULSE_PUBLIC_LINKS}
	PUBLIC pulse_cbor_encoder
	PRIVATE ${PULSE_PRIVATE_LINKS}
)

if(BUILD_TESTING)
	include(CTest)
	enable_language(CXX)
	set(CPPUTEST_BUILD_TESTING OFF CACHE BOOL "" FORCE)
	set(CPPUTEST_EXAMPLES OFF CACHE BOOL "" FORCE)
	add_subdirectory(tests/cpputest EXCLUDE_FROM_ALL)

	# Platform-specific and storage files are excluded so tests supply their own stubs.
	set(_test_srcs ${PULSE_SRCS})
	list(FILTER _test_srcs EXCLUDE REGEX "pulse_overrides\\.c$")
	list(FILTER _test_srcs EXCLUDE REGEX "pulse_transport_https\\.c$")
	list(FILTER _test_srcs EXCLUDE REGEX "pulse_transport_coaps\\.c$")
	list(FILTER _test_srcs EXCLUDE REGEX "/metricfs\\.c$")
	add_library(pulse-testable STATIC ${_test_srcs})
	target_compile_features(pulse-testable PRIVATE c_std_99)
	target_include_directories(pulse-testable
		PUBLIC ${PULSE_PUBLIC_INCS}
		PRIVATE ${PULSE_PRIVATE_INCS})
	target_compile_definitions(pulse-testable PRIVATE
		METRICS_USER_DEFINES="${_pulse_metrics_user_defines}")
	target_link_libraries(pulse-testable
		PUBLIC pulse::libmcu
		PUBLIC pulse_cbor_encoder
		PRIVATE pulse::cbor)

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
		pulse-testable
		CppUTest::CppUTest
		CppUTest::CppUTestExt)
	add_test(NAME test_pulse_report_cpp COMMAND test_pulse_report_cpp)

	add_test(NAME test_cmake_consumer_default
		COMMAND ${CMAKE_COMMAND}
			-DTEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/default-consumer
			-DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/tests/integration/default-consumer
			-DTEST_ARTIFACT=${CMAKE_CURRENT_BINARY_DIR}/tests/integration/default-consumer/default_consumer
			-DTEST_NM_SYMBOL=metrics_encode_header
			-DPULSE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
			-DCONFIGURE_ARGS=-DPULSE_FETCH_DEPS=ON
			-P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_fixture.cmake)

	add_test(NAME test_cmake_consumer_preexisting_deps
		COMMAND ${CMAKE_COMMAND}
			-DTEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/preexisting-deps-consumer
			-DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/tests/integration/preexisting-deps-consumer
			-DTEST_ARTIFACT=${CMAKE_CURRENT_BINARY_DIR}/tests/integration/preexisting-deps-consumer/preexisting_deps_consumer
			-DTEST_NM_SYMBOL=metrics_encode_header
			-DPULSE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
			-DLIBMCU_URL=https://github.com/libmcu/libmcu.git
			-DCBOR_URL=https://github.com/libmcu/cbor.git
			-P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_fixture.cmake)
endif()
