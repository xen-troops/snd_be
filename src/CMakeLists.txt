################################################################################
# Check packages
################################################################################

include(FindPkgConfig)

pkg_check_modules (CONFIG REQUIRED libconfig++)
if (!CONFIG_FOUND)
	message(FATAL_ERROR "No libconfig++ found")
endif()

if(WITH_ALSA)
	pkg_check_modules (ALSA REQUIRED alsa)
	if (!ALSA_FOUND)
		message(FATAL_ERROR "No alsa found")
	endif()
endif()

if(WITH_PULSE)
	pkg_check_modules (PULSE REQUIRED libpulse)
	if(!PULSE_FOUND)
		message(FATAL_ERROR "No pulse found")
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

target_link_libraries(${PROJECT_NAME}
	xenbe
	pthread
	${CONFIG_LIBRARIES}
)
