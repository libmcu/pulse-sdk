if(CONFIG_PULSE)
	set(PULSE_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

	if(CONFIG_PULSE_TRANSPORT_COAPS)
		set(PULSE_TRANSPORT "coaps" CACHE STRING "" FORCE)
	else()
		set(PULSE_TRANSPORT "https" CACHE STRING "" FORCE)
	endif()

	include(${PULSE_ROOT}/pulse.cmake)
	pulse_collect(PULSE_SRCS PULSE_PUBLIC_INCS PULSE_PRIVATE_INCS)

	zephyr_library_named(pulse)
	zephyr_library_sources(${PULSE_SRCS})
	zephyr_include_directories(${PULSE_PUBLIC_INCS})
	zephyr_library_include_directories(${PULSE_PUBLIC_INCS}
		${PULSE_PRIVATE_INCS})

	if(CONFIG_PULSE_METRICS_USER_DEFINES)
		set(_pulse_metrics_defines_path
			${CONFIG_PULSE_METRICS_USER_DEFINES})

		if(NOT IS_ABSOLUTE "${_pulse_metrics_defines_path}")
			set(_pulse_metrics_defines_path
				${APPLICATION_SOURCE_DIR}/${_pulse_metrics_defines_path})
		endif()

		zephyr_library_compile_definitions(
			METRICS_USER_DEFINES="${_pulse_metrics_defines_path}")
	endif()
endif()
