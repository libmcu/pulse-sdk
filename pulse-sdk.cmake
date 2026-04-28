include_guard(GLOBAL)

include(FetchContent)

set(PULSE_SDK_TRANSPORT "coaps" CACHE STRING
	"SDK transport to build (https or coaps)")
set_property(CACHE PULSE_SDK_TRANSPORT PROPERTY STRINGS https coaps)

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

function(_pulse_sdk_get_transport_name out_var)
	string(TOLOWER "${PULSE_SDK_TRANSPORT}" _transport)

	if(NOT _transport STREQUAL "https" AND NOT _transport STREQUAL "coaps")
		message(FATAL_ERROR
			"Unsupported PULSE_SDK_TRANSPORT='${PULSE_SDK_TRANSPORT}'. Use https or coaps.")
	endif()

	set(${out_var} ${_transport} PARENT_SCOPE)
endfunction()

function(_pulse_sdk_resolve_transport_source platform_dir out_var)
	_pulse_sdk_get_transport_name(_transport)
	set(_transport_path "${platform_dir}/pulse_transport_${_transport}.c")

	if(NOT EXISTS "${_transport_path}")
		message(FATAL_ERROR
			"Transport '${_transport}' is not available for platform path '${platform_dir}'.")
	endif()

	set(${out_var} ${_transport_path} PARENT_SCOPE)
endfunction()

function(pulse_sdk_collect out_srcs out_public_incs out_private_incs)
	set(_srcs
		${PULSE_SDK_ROOT}/src/pulse.c
		${PULSE_SDK_ROOT}/src/pulse_codec.c
		${PULSE_SDK_ROOT}/src/pulse_metrics_cbor_encoder.c
	)
	set(_public_incs
		${PULSE_SDK_ROOT}/include
	)
	set(_private_incs)
	set(_public_defs)
	set(_public_links)

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
	_pulse_sdk_get_transport_name(_transport)
	if(_transport STREQUAL "coaps")
		list(APPEND _public_defs PULSE_SDK_TRANSPORT_COAPS=1)
	else()
		list(APPEND _public_defs PULSE_SDK_TRANSPORT_COAPS=0)
	endif()

	# NOTE: metrics core sources, platform overrides, and transport are only
	# collected automatically when libmcu is resolved as bundled or fetched.
	# When PULSE_SDK_LIBMCU_ROOT or LIBMCU_ROOT points to an external root,
	# the caller is responsible for adding the following sources to the build:
	#   - <libmcu>/modules/metrics/src/metrics.c
	#   - <libmcu>/modules/metrics/src/metricfs.c
	#   - <libmcu>/modules/common/src/assert.c
	#   - <libmcu>/modules/common/src/base64.c (coaps transport only)
	#   - ports/<platform>/pulse_overrides.c
	#   - ports/<platform>/pulse_transport_<transport>.c
	#
	# When libmcu is an external library (preferred/fallback), the GNU linker will
	# not extract it from the archive because the weak symbol in libmcu satisfies
	# the reference before the archive is scanned (archive members are only
	# extracted for undefined references, not weak-defined ones). To ensure the
	# strong CBOR encoder is used, callers must also compile
	# pulse_metrics_cbor_encoder.c directly into the final executable (e.g. via
	# PORT_SRCS). The duplicate entry in the archive is harmless — it will not be
	# extracted once the symbol is already defined by the direct-compiled object.
	if(_PULSE_SDK_LIBMCU_RESOLVED_ROOT_SOURCE STREQUAL "bundled" OR
			_PULSE_SDK_LIBMCU_RESOLVED_ROOT_SOURCE STREQUAL "fetched")
		set(_platform "")
		if(DEFINED PULSE_SDK_PLATFORM_OVERRIDE AND
				NOT PULSE_SDK_PLATFORM_OVERRIDE STREQUAL "")
			string(TOLOWER "${PULSE_SDK_PLATFORM_OVERRIDE}" _platform)
		elseif(COMMAND idf_component_register)
			set(_platform "esp-idf")
		elseif(DEFINED ZEPHYR_BASE)
			set(_platform "zephyr")
		elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
			set(_platform "linux")
		else()
			set(_platform "baremetal")
		endif()

		list(APPEND _srcs
			${_PULSE_SDK_LIBMCU_RESOLVED_ROOT}/modules/common/src/assert.c
			${_PULSE_SDK_LIBMCU_RESOLVED_ROOT}/modules/metrics/src/metrics.c
			${_PULSE_SDK_LIBMCU_RESOLVED_ROOT}/modules/metrics/src/metricfs.c)

		if(_transport STREQUAL "coaps")
			list(APPEND _srcs
				${_PULSE_SDK_LIBMCU_RESOLVED_ROOT}/modules/common/src/base64.c)
		endif()

		if(_platform STREQUAL "esp-idf" AND
				EXISTS "${PULSE_SDK_ROOT}/ports/esp-idf/pulse_overrides.c")
			_pulse_sdk_resolve_transport_source(
				"${PULSE_SDK_ROOT}/ports/esp-idf" _transport_src)
			list(APPEND _srcs
				${PULSE_SDK_ROOT}/ports/esp-idf/pulse_overrides.c
				${_transport_src})
		elseif(_platform STREQUAL "zephyr" AND
				EXISTS "${PULSE_SDK_ROOT}/ports/zephyr/pulse_overrides.c")
			_pulse_sdk_resolve_transport_source(
				"${PULSE_SDK_ROOT}/ports/zephyr" _transport_src)
			list(APPEND _srcs
				${PULSE_SDK_ROOT}/ports/zephyr/pulse_overrides.c
				${_transport_src})
		elseif(_platform STREQUAL "linux" AND
				EXISTS "${PULSE_SDK_ROOT}/ports/linux/pulse_overrides.c")
			_pulse_sdk_resolve_transport_source(
				"${PULSE_SDK_ROOT}/ports/linux" _transport_src)
			list(APPEND _srcs ${PULSE_SDK_ROOT}/ports/linux/pulse_overrides.c)
			list(APPEND _srcs ${_transport_src})
		elseif(_platform STREQUAL "baremetal" AND
				EXISTS "${PULSE_SDK_ROOT}/ports/baremetal/pulse_overrides.c")
			_pulse_sdk_resolve_transport_source(
				"${PULSE_SDK_ROOT}/ports/baremetal" _transport_src)
			list(APPEND _srcs
				${PULSE_SDK_ROOT}/ports/baremetal/pulse_overrides.c
				${_transport_src})
		endif()
	endif()

	list(REMOVE_DUPLICATES _srcs)
	list(REMOVE_DUPLICATES _public_incs)
	list(REMOVE_DUPLICATES _private_incs)
	list(REMOVE_DUPLICATES _public_defs)

	set(_https_src "${PULSE_SDK_ROOT}/ports/linux/pulse_transport_https.c")
	set(_coaps_src "${PULSE_SDK_ROOT}/ports/linux/pulse_transport_coaps.c")
	list(FIND _srcs ${_https_src} _https_idx)
	list(FIND _srcs ${_coaps_src} _coaps_idx)

	if(NOT _https_idx EQUAL -1)
		find_package(CURL REQUIRED)
		list(APPEND _public_links CURL::libcurl)
	endif()

	if(NOT _coaps_idx EQUAL -1)
		find_package(PkgConfig REQUIRED)
		pkg_check_modules(PULSE_SDK_LIBCOAP REQUIRED IMPORTED_TARGET libcoap-3-openssl)
		pkg_check_modules(PULSE_SDK_LIBCRYPTO REQUIRED IMPORTED_TARGET libcrypto)
		pkg_check_modules(PULSE_SDK_LIBSSL REQUIRED IMPORTED_TARGET libssl)
		list(APPEND _public_links
			PkgConfig::PULSE_SDK_LIBCOAP
			PkgConfig::PULSE_SDK_LIBCRYPTO
			PkgConfig::PULSE_SDK_LIBSSL)
	endif()

	list(REMOVE_DUPLICATES _public_links)

	set(${out_srcs} ${_srcs} PARENT_SCOPE)
	set(${out_public_incs} ${_public_incs} PARENT_SCOPE)
	set(${out_private_incs} ${_private_incs} PARENT_SCOPE)
	if(ARGC GREATER 3 AND ARGV3)
		set(${ARGV3} ${_public_defs} PARENT_SCOPE)
	endif()
	if(ARGC GREATER 4 AND ARGV4)
		set(${ARGV4} ${_public_links} PARENT_SCOPE)
	endif()
endfunction()
