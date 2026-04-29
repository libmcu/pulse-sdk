if(NOT DEFINED TEST_SOURCE_DIR)
	message(FATAL_ERROR "TEST_SOURCE_DIR is required")
endif()

if(NOT DEFINED TEST_BINARY_DIR)
	message(FATAL_ERROR "TEST_BINARY_DIR is required")
endif()

if(NOT DEFINED PULSE_ROOT)
	message(FATAL_ERROR "PULSE_ROOT is required")
endif()

set(_configure_args)
if(DEFINED CONFIGURE_ARGS AND NOT CONFIGURE_ARGS STREQUAL "")
	separate_arguments(_configure_args NATIVE_COMMAND "${CONFIGURE_ARGS}")
endif()

if(DEFINED LIBMCU_URL AND NOT LIBMCU_URL STREQUAL "")
	list(APPEND _configure_args -DLIBMCU_URL=${LIBMCU_URL})
endif()

if(DEFINED CBOR_URL AND NOT CBOR_URL STREQUAL "")
	list(APPEND _configure_args -DCBOR_URL=${CBOR_URL})
endif()

execute_process(
	COMMAND ${CMAKE_COMMAND}
		-S ${TEST_SOURCE_DIR}
		-B ${TEST_BINARY_DIR}
		-DPULSE_ROOT=${PULSE_ROOT}
		${_configure_args}
	RESULT_VARIABLE _configure_result)

if(NOT _configure_result EQUAL 0)
	message(FATAL_ERROR "Fixture configure failed: ${_configure_result}")
endif()

execute_process(
	COMMAND ${CMAKE_COMMAND} --build ${TEST_BINARY_DIR}
	RESULT_VARIABLE _build_result)

if(NOT _build_result EQUAL 0)
	message(FATAL_ERROR "Fixture build failed: ${_build_result}")
endif()

if(DEFINED TEST_ARTIFACT AND NOT TEST_ARTIFACT STREQUAL "" AND
		DEFINED TEST_NM_SYMBOL AND NOT TEST_NM_SYMBOL STREQUAL "")
	find_program(_nm_command nm REQUIRED)
	execute_process(
		COMMAND ${_nm_command} ${TEST_ARTIFACT}
		RESULT_VARIABLE _nm_result
		OUTPUT_VARIABLE _nm_output
		ERROR_VARIABLE _nm_error)

	if(NOT _nm_result EQUAL 0)
		message(FATAL_ERROR "Fixture nm check failed: ${_nm_result}\n${_nm_error}")
	endif()

	string(REGEX MATCH "(^|\n)[^\n]*[ \t]T[ \t]_?${TEST_NM_SYMBOL}(\n|$)"
		_nm_strong_match "${_nm_output}")
	if(NOT _nm_strong_match)
		message(FATAL_ERROR
			"Expected strong symbol '${TEST_NM_SYMBOL}' in ${TEST_ARTIFACT}.\n${_nm_output}")
	endif()

	string(REGEX MATCH "(^|\n)[^\n]*[ \t][Ww][ \t]_?${TEST_NM_SYMBOL}(\n|$)"
		_nm_weak_match "${_nm_output}")
	if(_nm_weak_match)
		message(FATAL_ERROR
			"Unexpected weak symbol '${TEST_NM_SYMBOL}' in ${TEST_ARTIFACT}.\n${_nm_output}")
	endif()
endif()
