if(CONFIG_PULSE_SDK)
	set(PULSE_SDK_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

	if(CONFIG_PULSE_SDK_TRANSPORT_COAPS)
		set(PULSE_SDK_TRANSPORT "coaps" CACHE STRING "" FORCE)
	else()
		set(PULSE_SDK_TRANSPORT "https" CACHE STRING "" FORCE)
	endif()

	include(${PULSE_SDK_ROOT}/pulse-sdk.cmake)
	pulse_sdk_collect(PULSE_SDK_SRCS PULSE_SDK_PUBLIC_INCS PULSE_SDK_PRIVATE_INCS)

	zephyr_library_named(pulse_sdk)
	zephyr_library_sources(${PULSE_SDK_SRCS})
	zephyr_include_directories(${PULSE_SDK_PUBLIC_INCS})
	zephyr_library_include_directories(${PULSE_SDK_PUBLIC_INCS}
		${PULSE_SDK_PRIVATE_INCS})
	zephyr_ld_options(-u pulse_metrics_cbor_encoder_link_anchor)

	if(CONFIG_PULSE_SDK_METRICS_USER_DEFINES)
		set(_pulse_sdk_metrics_defines_path
			${CONFIG_PULSE_SDK_METRICS_USER_DEFINES})

		if(NOT IS_ABSOLUTE "${_pulse_sdk_metrics_defines_path}")
			set(_pulse_sdk_metrics_defines_path
				${APPLICATION_SOURCE_DIR}/${_pulse_sdk_metrics_defines_path})
		endif()

		zephyr_library_compile_definitions(
			METRICS_USER_DEFINES="${_pulse_sdk_metrics_defines_path}")
	endif()
endif()
