################################################################################
# Check packages
################################################################################

include(FindPkgConfig)

if(WITH_ALSA)
	pkg_check_modules (ALSA REQUIRED alsa)
	if (NOT ALSA_FOUND)
		message(FATAL_ERROR "No alsa found")
	endif()
endif()

if(WITH_PULSE)
	pkg_check_modules (PULSE REQUIRED libpulse)
	if(NOT PULSE_FOUND)
		message(FATAL_ERROR "No pulse found")
	endif()
endif()

if(WITH_PIPEWIRE)
	pkg_check_modules (PIPEWIRE REQUIRED libpipewire-0.3)
	if (NOT PIPEWIRE_FOUND)
		message(FATAL_ERROR "No pipewire found")
	endif()
endif()


################################################################################
# Includes
################################################################################

################################################################################
# Sources
################################################################################

set(SOURCES
	CommandHandler.cpp
	SndBackend.cpp
)

if(WITH_ALSA)
	list(APPEND SOURCES
		AlsaPcm.cpp
	)
endif()

if(WITH_PULSE)
	list(APPEND SOURCES
		PulsePcm.cpp
	)
endif()

if(WITH_PIPEWIRE)
	list(APPEND SOURCES
		PipeWirePcm.cpp
	)
endif()

if(WITH_MOCKBELIB)
	list(APPEND SOURCES
		MockBackend.cpp
	)
endif()

################################################################################
# Targets
################################################################################

add_executable(${PROJECT_NAME} ${SOURCES})

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

################################################################################
# Libraries
################################################################################

if(WITH_ALSA)
	target_link_libraries(${PROJECT_NAME} ${ALSA_LIBRARIES})
endif()

if(WITH_PULSE)
	target_link_libraries(${PROJECT_NAME} ${PULSE_LIBRARIES})
endif()

if(WITH_PIPEWIRE)
	target_link_libraries(${PROJECT_NAME} ${PIPEWIRE_LIBRARIES})
endif()

set(XENBE_LIB xenbe)

if(WITH_MOCKBELIB)
	set(XENBE_LIB xenbemock)
endif()

target_link_libraries(${PROJECT_NAME}
	${XENBE_LIB}
	pthread
)
