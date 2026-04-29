include_guard(GLOBAL)

include(FetchContent)

set(PULSE_TRANSPORT "coaps" CACHE STRING
	"SDK transport to build (https or coaps)")
set_property(CACHE PULSE_TRANSPORT PROPERTY STRINGS https coaps)

set(PULSE_ROOT ${CMAKE_CURRENT_LIST_DIR})

if(NOT DEFINED PULSE_FETCH_DEPS)
	if(CMAKE_BUILD_EARLY_EXPANSION)
		set(PULSE_FETCH_DEPS OFF)
	else()
		set(PULSE_FETCH_DEPS ON)
	endif()
endif()

function(_pulse_fetch_repo name git_url git_tag out_var)
	FetchContent_Declare(${name}
		GIT_REPOSITORY ${git_url}
		GIT_TAG ${git_tag}
		SOURCE_SUBDIR _no_cmake_subdirectory
	)
	FetchContent_MakeAvailable(${name})
	set(${out_var} ${${name}_SOURCE_DIR} PARENT_SCOPE)
endfunction()

function(_pulse_resolve_root preferred_var fallback_var local_dir repo_url repo_tag out_var)
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
	elseif(EXISTS "${PULSE_ROOT}/${local_dir}")
		set(_resolved "${PULSE_ROOT}/${local_dir}")
		set(_source bundled)
	elseif(PULSE_FETCH_DEPS)
		string(REPLACE "/" "_" _fetch_name ${local_dir})
		string(REPLACE "-" "_" _fetch_name ${_fetch_name})
		_pulse_fetch_repo(${_fetch_name} ${repo_url} ${repo_tag} _resolved)
		set(_source fetched)
	elseif(EXISTS "${PULSE_ROOT}/../${_dep_name}")
		set(_resolved "${PULSE_ROOT}/../${_dep_name}")
		set(_source sibling)
	endif()

	set(${out_var} ${_resolved} PARENT_SCOPE)
	set(${out_var}_SOURCE ${_source} PARENT_SCOPE)
endfunction()

function(_pulse_find_first_target out_var)
	foreach(_candidate ${ARGN})
		if(TARGET ${_candidate})
			set(${out_var} ${_candidate} PARENT_SCOPE)
			return()
		endif()
	endforeach()

	set(${out_var} "" PARENT_SCOPE)
endfunction()

function(_pulse_make_alias alias_name target_name)
	if(NOT TARGET ${alias_name})
		add_library(${alias_name} ALIAS ${target_name})
	endif()
endfunction()

function(_pulse_add_cmake_dep root dep_name out_target)
	if(NOT EXISTS "${root}/CMakeLists.txt")
		message(FATAL_ERROR
			"Dependency '${dep_name}' at '${root}' does not provide a CMakeLists.txt")
	endif()

	string(MAKE_C_IDENTIFIER "${root}" _root_id)
	set(_binary_dir "${CMAKE_BINARY_DIR}/pulse-deps/${dep_name}-${_root_id}")
	add_subdirectory("${root}" "${_binary_dir}")

	if(NOT TARGET ${dep_name})
		message(FATAL_ERROR
			"Dependency '${dep_name}' added from '${root}' did not create target '${dep_name}'")
	endif()

	set(${out_target} ${dep_name} PARENT_SCOPE)
endfunction()

function(_pulse_resolve_metrics_user_defines out_var)
	set(_metrics_defines "")

	if(DEFINED PULSE_METRICS_USER_DEFINES AND
			NOT PULSE_METRICS_USER_DEFINES STREQUAL "")
		set(_metrics_defines "${PULSE_METRICS_USER_DEFINES}")
	elseif(DEFINED METRICS_USER_DEFINES AND NOT METRICS_USER_DEFINES STREQUAL "")
		set(_metrics_defines "${METRICS_USER_DEFINES}")
	endif()

	set(${out_var} "${_metrics_defines}" PARENT_SCOPE)
endfunction()

function(_pulse_resolve_cmake_dependency dep_name preferred_var fallback_var
		local_dir repo_url repo_tag alias_name out_target out_managed out_source)
	_pulse_find_first_target(_existing_target ${alias_name} ${dep_name})
	if(_existing_target)
		if(NOT _existing_target STREQUAL ${alias_name})
			_pulse_make_alias(${alias_name} ${_existing_target})
		endif()
		set(${out_target} ${alias_name} PARENT_SCOPE)
		set(${out_managed} FALSE PARENT_SCOPE)
		set(${out_source} preexisting-target PARENT_SCOPE)
		return()
	endif()

	set(_resolved_root "")
	set(_resolved_source "")

	if(DEFINED ${preferred_var} AND EXISTS "${${preferred_var}}")
		set(_resolved_root "${${preferred_var}}")
		set(_resolved_source preferred)
	elseif(DEFINED ${fallback_var} AND EXISTS "${${fallback_var}}")
		set(_resolved_root "${${fallback_var}}")
		set(_resolved_source fallback)
	elseif(EXISTS "${PULSE_ROOT}/${local_dir}")
		set(_resolved_root "${PULSE_ROOT}/${local_dir}")
		set(_resolved_source bundled)
	elseif(PULSE_FETCH_DEPS)
		FetchContent_Declare(${dep_name}
			GIT_REPOSITORY ${repo_url}
			GIT_TAG ${repo_tag}
		)
		FetchContent_MakeAvailable(${dep_name})
		if(NOT TARGET ${dep_name})
			message(FATAL_ERROR
				"Fetched dependency '${dep_name}' did not create target '${dep_name}'")
		endif()
		_pulse_make_alias(${alias_name} ${dep_name})
		set(${out_target} ${alias_name} PARENT_SCOPE)
		set(${out_managed} TRUE PARENT_SCOPE)
		set(${out_source} fetched PARENT_SCOPE)
		return()
	elseif(EXISTS "${PULSE_ROOT}/../${dep_name}")
		set(_resolved_root "${PULSE_ROOT}/../${dep_name}")
		set(_resolved_source sibling)
	endif()

	if(NOT _resolved_root)
		message(FATAL_ERROR
			"${dep_name} root not found. Set ${preferred_var} or ${fallback_var}, place dependency under ${local_dir}, or enable PULSE_FETCH_DEPS.")
	endif()

	_pulse_add_cmake_dep("${_resolved_root}" ${dep_name} _resolved_target)
	_pulse_make_alias(${alias_name} ${_resolved_target})

	set(${out_target} ${alias_name} PARENT_SCOPE)
	set(${out_managed} TRUE PARENT_SCOPE)
	set(${out_source} ${_resolved_source} PARENT_SCOPE)
endfunction()

function(_pulse_get_transport_name out_var)
	string(TOLOWER "${PULSE_TRANSPORT}" _transport)

	if(NOT _transport STREQUAL "https" AND NOT _transport STREQUAL "coaps")
		message(FATAL_ERROR
			"Unsupported PULSE_TRANSPORT='${PULSE_TRANSPORT}'. Use https or coaps.")
	endif()

	set(${out_var} ${_transport} PARENT_SCOPE)
endfunction()

function(_pulse_resolve_transport_source platform_dir out_var)
	_pulse_get_transport_name(_transport)
	set(_transport_path "${platform_dir}/pulse_transport_${_transport}.c")

	if(NOT EXISTS "${_transport_path}")
		message(FATAL_ERROR
			"Transport '${_transport}' is not available for platform path '${platform_dir}'.")
	endif()

	set(${out_var} ${_transport_path} PARENT_SCOPE)
endfunction()

function(_pulse_get_platform_name out_var)
	set(_platform "")
	if(DEFINED PULSE_PLATFORM_OVERRIDE AND
			NOT PULSE_PLATFORM_OVERRIDE STREQUAL "")
		string(TOLOWER "${PULSE_PLATFORM_OVERRIDE}" _platform)
	elseif(COMMAND idf_component_register)
		set(_platform "esp-idf")
	elseif(DEFINED ZEPHYR_BASE)
		set(_platform "zephyr")
	elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		set(_platform "linux")
	else()
		set(_platform "baremetal")
	endif()

	set(${out_var} ${_platform} PARENT_SCOPE)
endfunction()

function(_pulse_append_transport_compile_definition out_var)
	set(_defs ${ARGN})
	_pulse_get_transport_name(_transport)
	if(_transport STREQUAL "coaps")
		list(APPEND _defs PULSE_TRANSPORT_COAPS=1)
	else()
		list(APPEND _defs PULSE_TRANSPORT_COAPS=0)
	endif()

	set(${out_var} ${_defs} PARENT_SCOPE)
endfunction()

function(_pulse_append_linux_transport_links srcs_var links_var)
	set(_links ${${links_var}})
	set(_srcs ${${srcs_var}})

	set(_https_src "${PULSE_ROOT}/ports/linux/pulse_transport_https.c")
	set(_coaps_src "${PULSE_ROOT}/ports/linux/pulse_transport_coaps.c")
	list(FIND _srcs ${_https_src} _https_idx)
	list(FIND _srcs ${_coaps_src} _coaps_idx)

	if(NOT _https_idx EQUAL -1)
		find_package(CURL REQUIRED)
		list(APPEND _links CURL::libcurl)
	endif()

	if(NOT _coaps_idx EQUAL -1)
		find_package(PkgConfig REQUIRED)
		pkg_check_modules(PULSE_LIBCOAP REQUIRED IMPORTED_TARGET libcoap-3-openssl)
		pkg_check_modules(PULSE_LIBCRYPTO REQUIRED IMPORTED_TARGET libcrypto)
		pkg_check_modules(PULSE_LIBSSL REQUIRED IMPORTED_TARGET libssl)
		list(APPEND _links
			PkgConfig::PULSE_LIBCOAP
			PkgConfig::PULSE_LIBCRYPTO
			PkgConfig::PULSE_LIBSSL)
	endif()

	list(REMOVE_DUPLICATES _links)
	set(${links_var} ${_links} PARENT_SCOPE)
endfunction()

function(_pulse_is_idf_component_available dep_name out_var)
	set(_available FALSE)

	if(COMMAND idf_component_register AND DEFINED PROJECT_DIR)
		if(TARGET __idf_${dep_name} OR
				EXISTS "${PROJECT_DIR}/components/${dep_name}/CMakeLists.txt")
			set(_available TRUE)
		endif()
	endif()

	set(${out_var} ${_available} PARENT_SCOPE)
endfunction()

function(pulse_collect_cmake out_srcs out_public_incs out_private_incs)
	set(_srcs
		${PULSE_ROOT}/src/pulse.c
		${PULSE_ROOT}/src/pulse_codec.c
		${PULSE_ROOT}/src/pulse_metrics_cbor_encoder.c
	)
	set(_public_incs
		${PULSE_ROOT}/include
	)
	set(_private_incs)
	set(_public_defs)
	set(_public_links)
	set(_private_links)

	_pulse_resolve_cmake_dependency(libmcu PULSE_LIBMCU_ROOT LIBMCU_ROOT
		external/libmcu https://github.com/libmcu/libmcu.git main
		pulse::libmcu _pulse_libmcu_target
		_pulse_libmcu_managed _pulse_libmcu_source)
	_pulse_resolve_cmake_dependency(cbor PULSE_CBOR_ROOT CBOR_ROOT
		external/cbor https://github.com/libmcu/cbor.git main
		pulse::cbor _pulse_cbor_target
		_pulse_cbor_managed _pulse_cbor_source)

	_pulse_resolve_metrics_user_defines(_metrics_user_defines)
	if(_pulse_libmcu_managed AND _metrics_user_defines)
		target_compile_definitions(libmcu PUBLIC
			METRICS_USER_DEFINES="${_metrics_user_defines}")
	endif()

	list(APPEND _public_links ${_pulse_libmcu_target})
	list(APPEND _private_links ${_pulse_cbor_target})

	_pulse_append_transport_compile_definition(_public_defs ${_public_defs})
	_pulse_get_platform_name(_platform)

	if(_platform STREQUAL "linux" AND
			EXISTS "${PULSE_ROOT}/ports/linux/pulse_overrides.c")
		_pulse_resolve_transport_source(
			"${PULSE_ROOT}/ports/linux" _transport_src)
		list(APPEND _srcs
			${PULSE_ROOT}/ports/linux/pulse_overrides.c
			${_transport_src})
	elseif(_platform STREQUAL "baremetal" AND
			EXISTS "${PULSE_ROOT}/ports/baremetal/pulse_overrides.c")
		_pulse_resolve_transport_source(
			"${PULSE_ROOT}/ports/baremetal" _transport_src)
		list(APPEND _srcs
			${PULSE_ROOT}/ports/baremetal/pulse_overrides.c
			${_transport_src})
	endif()

	list(REMOVE_DUPLICATES _srcs)
	list(REMOVE_DUPLICATES _public_incs)
	list(REMOVE_DUPLICATES _private_incs)
	list(REMOVE_DUPLICATES _public_defs)

	_pulse_append_linux_transport_links(_srcs _public_links)
	list(REMOVE_DUPLICATES _private_links)

	set(${out_srcs} ${_srcs} PARENT_SCOPE)
	set(${out_public_incs} ${_public_incs} PARENT_SCOPE)
	set(${out_private_incs} ${_private_incs} PARENT_SCOPE)
	if(ARGC GREATER 3 AND ARGV3)
		set(${ARGV3} ${_public_defs} PARENT_SCOPE)
	endif()
	if(ARGC GREATER 4 AND ARGV4)
		set(${ARGV4} ${_public_links} PARENT_SCOPE)
	endif()
	if(ARGC GREATER 5 AND ARGV5)
		set(${ARGV5} ${_private_links} PARENT_SCOPE)
	endif()
endfunction()

function(pulse_collect out_srcs out_public_incs out_private_incs)
	set(_srcs
		${PULSE_ROOT}/src/pulse.c
		${PULSE_ROOT}/src/pulse_codec.c
		${PULSE_ROOT}/src/pulse_metrics_cbor_encoder.c
	)
	set(_public_incs
		${PULSE_ROOT}/include
	)
	set(_private_incs)
	set(_public_defs)
	set(_public_links)
	set(_reuse_libmcu FALSE)
	set(_reuse_cbor FALSE)

	_pulse_get_transport_name(_transport)
	_pulse_append_transport_compile_definition(_public_defs ${_public_defs})
	_pulse_get_platform_name(_platform)

	if(_platform STREQUAL "esp-idf")
		_pulse_is_idf_component_available(libmcu _reuse_libmcu)
		_pulse_is_idf_component_available(cbor _reuse_cbor)
	elseif(_platform STREQUAL "zephyr")
		if(DEFINED CONFIG_LIBMCU AND CONFIG_LIBMCU)
			set(_reuse_libmcu TRUE)
		endif()
		if(DEFINED CONFIG_LIBMCU_CBOR AND CONFIG_LIBMCU_CBOR)
			set(_reuse_cbor TRUE)
		endif()
	endif()

	if(NOT _reuse_libmcu)
		_pulse_resolve_root(PULSE_LIBMCU_ROOT LIBMCU_ROOT
			external/libmcu https://github.com/libmcu/libmcu.git main
			_PULSE_LIBMCU_RESOLVED_ROOT)

		if(NOT _PULSE_LIBMCU_RESOLVED_ROOT)
			if(CMAKE_BUILD_EARLY_EXPANSION)
				return()
			endif()
			message(FATAL_ERROR "libmcu root not found. Set PULSE_LIBMCU_ROOT or LIBMCU_ROOT, place dependency under external/libmcu, or enable PULSE_FETCH_DEPS.")
		endif()

		if(DEFINED CONFIG_PULSE_LIBMCU AND CONFIG_PULSE_LIBMCU)
			include(${_PULSE_LIBMCU_RESOLVED_ROOT}/project/interfaces.cmake)
			include(${_PULSE_LIBMCU_RESOLVED_ROOT}/project/modules.cmake)
			list(APPEND _srcs ${LIBMCU_MODULES_SRCS})
			list(APPEND _public_incs
				${LIBMCU_INTERFACES_INCS}
				${LIBMCU_MODULES_INCS})
		else()
			set(LIBMCU_INTERFACES uart kvstore)
			include(${_PULSE_LIBMCU_RESOLVED_ROOT}/project/interfaces.cmake)
			list(APPEND _public_incs
				${LIBMCU_INTERFACES_INCS}
				${_PULSE_LIBMCU_RESOLVED_ROOT}/modules/common/include
				${_PULSE_LIBMCU_RESOLVED_ROOT}/modules/metrics/include)
		endif()
	endif()

	if(NOT _reuse_cbor)
		_pulse_resolve_root(PULSE_CBOR_ROOT CBOR_ROOT
			external/cbor https://github.com/libmcu/cbor.git main
			_PULSE_CBOR_RESOLVED_ROOT)

		if(NOT _PULSE_CBOR_RESOLVED_ROOT)
			if(CMAKE_BUILD_EARLY_EXPANSION)
				return()
			endif()
			message(FATAL_ERROR "cbor root not found. Set PULSE_CBOR_ROOT or CBOR_ROOT, place dependency under external/cbor, or enable PULSE_FETCH_DEPS.")
		endif()

		include(${_PULSE_CBOR_RESOLVED_ROOT}/cbor.cmake)
		list(APPEND _srcs ${CBOR_SRCS})
		if(DEFINED CONFIG_PULSE_CBOR AND CONFIG_PULSE_CBOR)
			list(APPEND _public_incs ${CBOR_INCS})
		else()
			list(APPEND _private_incs ${CBOR_INCS})
		endif()
	endif()

	# NOTE: pulse always compiles its own transport/override sources so the
	# SDK remains self-contained. When an external libmcu is used, the caller only
	# needs to make sure libmcu runtime implementation is linked somehow: either
	# through the application's existing libmcu target/library, or by compiling the
	# libmcu sources below directly.
	# When PULSE_LIBMCU_ROOT or LIBMCU_ROOT points to an external root,
	# the caller is responsible for providing the following libmcu implementation:
	#   - <libmcu>/modules/metrics/src/metrics.c
	#   - <libmcu>/modules/metrics/src/metricfs.c
	#   - <libmcu>/modules/common/src/assert.c
	#   - <libmcu>/modules/common/src/base64.c (coaps transport only)
	# For archive-based platforms, the build system must retain
	# pulse_metrics_cbor_encoder.c so the strong CBOR encoder is linked even when
	# libmcu arrives through a separate static archive.
	if(NOT _reuse_libmcu AND
			NOT (DEFINED CONFIG_PULSE_LIBMCU AND CONFIG_PULSE_LIBMCU) AND
			(_PULSE_LIBMCU_RESOLVED_ROOT_SOURCE STREQUAL "bundled" OR
			_PULSE_LIBMCU_RESOLVED_ROOT_SOURCE STREQUAL "fetched"))
		list(APPEND _srcs
			${_PULSE_LIBMCU_RESOLVED_ROOT}/modules/common/src/assert.c
			${_PULSE_LIBMCU_RESOLVED_ROOT}/modules/metrics/src/metrics.c
			${_PULSE_LIBMCU_RESOLVED_ROOT}/modules/metrics/src/metricfs.c)

		if(_transport STREQUAL "coaps")
			list(APPEND _srcs
				${_PULSE_LIBMCU_RESOLVED_ROOT}/modules/common/src/base64.c)
		endif()
	endif()

	if(_platform STREQUAL "esp-idf" AND
			EXISTS "${PULSE_ROOT}/ports/esp-idf/pulse_overrides.c")
		_pulse_resolve_transport_source(
			"${PULSE_ROOT}/ports/esp-idf" _transport_src)
		list(APPEND _srcs
			${PULSE_ROOT}/ports/esp-idf/pulse_overrides.c
			${_transport_src})
	elseif(_platform STREQUAL "zephyr" AND
			EXISTS "${PULSE_ROOT}/ports/zephyr/pulse_overrides.c")
		_pulse_resolve_transport_source(
			"${PULSE_ROOT}/ports/zephyr" _transport_src)
		list(APPEND _srcs
			${PULSE_ROOT}/ports/zephyr/pulse_overrides.c
			${_transport_src})
	elseif(_platform STREQUAL "linux" AND
			EXISTS "${PULSE_ROOT}/ports/linux/pulse_overrides.c")
		_pulse_resolve_transport_source(
			"${PULSE_ROOT}/ports/linux" _transport_src)
		list(APPEND _srcs ${PULSE_ROOT}/ports/linux/pulse_overrides.c)
		list(APPEND _srcs ${_transport_src})
	elseif(_platform STREQUAL "baremetal" AND
			EXISTS "${PULSE_ROOT}/ports/baremetal/pulse_overrides.c")
		_pulse_resolve_transport_source(
			"${PULSE_ROOT}/ports/baremetal" _transport_src)
		list(APPEND _srcs
			${PULSE_ROOT}/ports/baremetal/pulse_overrides.c
			${_transport_src})
	endif()

	list(REMOVE_DUPLICATES _srcs)
	list(REMOVE_DUPLICATES _public_incs)
	list(REMOVE_DUPLICATES _private_incs)
	list(REMOVE_DUPLICATES _public_defs)

	_pulse_append_linux_transport_links(_srcs _public_links)

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
