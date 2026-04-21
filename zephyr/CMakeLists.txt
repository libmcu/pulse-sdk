if(CONFIG_PULSE_SDK)
	set(PULSE_SDK_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
	include(${PULSE_SDK_ROOT}/pulse-sdk.cmake)
	pulse_sdk_collect(PULSE_SDK_SRCS PULSE_SDK_PUBLIC_INCS PULSE_SDK_PRIVATE_INCS)

	zephyr_library_named(pulse_sdk)
	zephyr_library_sources(${PULSE_SDK_SRCS})
	zephyr_include_directories(${PULSE_SDK_PUBLIC_INCS})
	zephyr_library_include_directories(${PULSE_SDK_PUBLIC_INCS}
		${PULSE_SDK_PRIVATE_INCS})

	if(CONFIG_PULSE_SDK_METRICS_USER_DEFINES)
		zephyr_library_compile_definitions(
			METRICS_USER_DEFINES="${CONFIG_PULSE_SDK_METRICS_USER_DEFINES}")
	endif()
endif()
