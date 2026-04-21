include_guard(GLOBAL)

include(FetchContent)

set(PULSE_SDK_ROOT ${CMAKE_CURRENT_LIST_DIR})

if(NOT DEFINED PULSE_SDK_FETCH_DEPS)
	if(CMAKE_BUILD_EARLY_EXPANSION)
		set(PULSE_SDK_FETCH_DEPS OFF)
	else()
		set(PULSE_SDK_FETCH_DEPS ON)
	endif()
endif()

function(_pulse_sdk_fetch_repo name git_url git_tag out_var)
	FetchContent_Declare(${name}
		GIT_REPOSITORY ${git_url}
		GIT_TAG ${git_tag}
		SOURCE_SUBDIR _no_cmake_subdirectory
	)
	FetchContent_MakeAvailable(${name})
	set(${out_var} ${${name}_SOURCE_DIR} PARENT_SCOPE)
endfunction()

function(_pulse_sdk_resolve_root preferred_var fallback_var local_dir repo_url repo_tag out_var)
	set(_resolved "")
	set(_source "")
	get_filename_component(_dep_name ${local_dir} NAME)

	if(DEFINED ${preferred_var} AND EXISTS "${${preferred_var}}")
		set(_resolved "${${preferred_var}}")
		set(_source preferred)
	elseif(DEFINED ${fallback_var} AND EXISTS "${${fallback_var}}")
		set(_resolved "${${fallback_var}}")
		set(_source fallback)
	elseif(COMMAND idf_component_register AND DEFINED PROJECT_DIR AND
			EXISTS "${PROJECT_DIR}/components/${_dep_name}")
		set(_resolved "${PROJECT_DIR}/components/${_dep_name}")
		set(_source project-components)
	elseif(EXISTS "${PULSE_SDK_ROOT}/${local_dir}")
		set(_resolved "${PULSE_SDK_ROOT}/${local_dir}")
		set(_source bundled)
	elseif(PULSE_SDK_FETCH_DEPS)
		string(REPLACE "/" "_" _fetch_name ${local_dir})
		string(REPLACE "-" "_" _fetch_name ${_fetch_name})
		_pulse_sdk_fetch_repo(${_fetch_name} ${repo_url} ${repo_tag} _resolved)
		set(_source fetched)
	elseif(EXISTS "${PULSE_SDK_ROOT}/../${_dep_name}")
		set(_resolved "${PULSE_SDK_ROOT}/../${_dep_name}")
		set(_source sibling)
	endif()

	set(${out_var} ${_resolved} PARENT_SCOPE)
	set(${out_var}_SOURCE ${_source} PARENT_SCOPE)
endfunction()

function(pulse_sdk_collect out_srcs out_public_incs out_private_incs)
	set(_srcs
		${PULSE_SDK_ROOT}/src/pulse.c
	)
	set(_public_incs
		${PULSE_SDK_ROOT}/include
	)
	set(_private_incs)

	_pulse_sdk_resolve_root(PULSE_SDK_LIBMCU_ROOT LIBMCU_ROOT
		external/libmcu https://github.com/libmcu/libmcu.git main
		_PULSE_SDK_LIBMCU_RESOLVED_ROOT)
	_pulse_sdk_resolve_root(PULSE_SDK_CBOR_ROOT CBOR_ROOT
		external/cbor https://github.com/libmcu/cbor.git main
		_PULSE_SDK_CBOR_RESOLVED_ROOT)

	if(NOT _PULSE_SDK_LIBMCU_RESOLVED_ROOT)
		if(CMAKE_BUILD_EARLY_EXPANSION)
			return()
		endif()
		message(FATAL_ERROR "libmcu root not found. Set PULSE_SDK_LIBMCU_ROOT or LIBMCU_ROOT, place dependency under external/libmcu, or enable PULSE_SDK_FETCH_DEPS.")
	endif()

	if(NOT _PULSE_SDK_CBOR_RESOLVED_ROOT)
		if(CMAKE_BUILD_EARLY_EXPANSION)
			return()
		endif()
		message(FATAL_ERROR "cbor root not found. Set PULSE_SDK_CBOR_ROOT or CBOR_ROOT, place dependency under external/cbor, or enable PULSE_SDK_FETCH_DEPS.")
	endif()

	set(LIBMCU_INTERFACES uart kvstore)
	include(${_PULSE_SDK_LIBMCU_RESOLVED_ROOT}/project/interfaces.cmake)
	include(${_PULSE_SDK_CBOR_RESOLVED_ROOT}/cbor.cmake)

	list(APPEND _srcs ${CBOR_SRCS})
	list(APPEND _public_incs
		${LIBMCU_INTERFACES_INCS}
		${_PULSE_SDK_LIBMCU_RESOLVED_ROOT}/modules/common/include
		${_PULSE_SDK_LIBMCU_RESOLVED_ROOT}/modules/metrics/include
		${CBOR_INCS})

	# NOTE: metrics core sources, platform overrides, and transport are only
	# collected automatically when libmcu is resolved as bundled or fetched.
	# When PULSE_SDK_LIBMCU_ROOT or LIBMCU_ROOT points to an external root,
	# the caller is responsible for adding the following sources to the build:
	#   - <libmcu>/modules/metrics/src/metrics.c
	#   - <libmcu>/modules/metrics/src/metrics_overrides.c
	#   - <libmcu>/modules/metrics/src/metrics_reporter.c
	#   - <libmcu>/modules/common/src/assert.c
	#   - <libmcu>/ports/metrics/cbor_encoder.c (if available)
	#   - ports/<platform>/pulse_overrides.c
	#   - ports/<platform>/pulse_transport_https.c
	#   - ports/pulse_metricfs_stub.c
	if(_PULSE_SDK_LIBMCU_RESOLVED_ROOT_SOURCE STREQUAL "bundled" OR
			_PULSE_SDK_LIBMCU_RESOLVED_ROOT_SOURCE STREQUAL "fetched")
		list(APPEND _srcs
			${PULSE_SDK_ROOT}/ports/pulse_metricfs_stub.c
			${_PULSE_SDK_LIBMCU_RESOLVED_ROOT}/modules/common/src/assert.c
			${_PULSE_SDK_LIBMCU_RESOLVED_ROOT}/modules/metrics/src/metrics.c
			${_PULSE_SDK_LIBMCU_RESOLVED_ROOT}/modules/metrics/src/metrics_overrides.c
			${_PULSE_SDK_LIBMCU_RESOLVED_ROOT}/modules/metrics/src/metrics_reporter.c)

		if(EXISTS "${_PULSE_SDK_LIBMCU_RESOLVED_ROOT}/ports/metrics/cbor_encoder.c")
			list(APPEND _srcs ${_PULSE_SDK_LIBMCU_RESOLVED_ROOT}/ports/metrics/cbor_encoder.c)
		endif()

		if(COMMAND idf_component_register AND
				EXISTS "${PULSE_SDK_ROOT}/ports/esp-idf/pulse_overrides.c")
			list(APPEND _srcs
				${PULSE_SDK_ROOT}/ports/esp-idf/pulse_overrides.c
				${PULSE_SDK_ROOT}/ports/esp-idf/pulse_transport_https.c)
		elseif(DEFINED ZEPHYR_BASE AND
				EXISTS "${PULSE_SDK_ROOT}/ports/zephyr/pulse_overrides.c")
			list(APPEND _srcs
				${PULSE_SDK_ROOT}/ports/zephyr/pulse_overrides.c
				${PULSE_SDK_ROOT}/ports/zephyr/pulse_transport_https.c)
		elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
				EXISTS "${PULSE_SDK_ROOT}/ports/linux/pulse_overrides.c")
			list(APPEND _srcs
				${PULSE_SDK_ROOT}/ports/linux/pulse_overrides.c
				${PULSE_SDK_ROOT}/ports/linux/pulse_transport_https.c)
		elseif(NOT DEFINED ZEPHYR_BASE AND NOT COMMAND idf_component_register AND
				EXISTS "${PULSE_SDK_ROOT}/ports/baremetal/pulse_overrides.c")
			list(APPEND _srcs
				${PULSE_SDK_ROOT}/ports/baremetal/pulse_overrides.c
				${PULSE_SDK_ROOT}/ports/baremetal/pulse_transport_https.c)
		endif()
	endif()

	list(REMOVE_DUPLICATES _srcs)
	list(REMOVE_DUPLICATES _public_incs)
	list(REMOVE_DUPLICATES _private_incs)

	set(${out_srcs} ${_srcs} PARENT_SCOPE)
	set(${out_public_incs} ${_public_incs} PARENT_SCOPE)
	set(${out_private_incs} ${_private_incs} PARENT_SCOPE)
endfunction()
