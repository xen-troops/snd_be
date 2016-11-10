cmake_minimum_required(VERSION 3.0)

project(snd_be)

set(SOURCES
	src/AlsaPcm.cpp
	src/SndBackend.cpp
	src/CommandHandler.cpp
)

include_directories(
	src
	${XEN_INCLUDE_PATH}
	${XENBE_INCLUDE_PATH}
	${IF_INCLUDE_PATH}
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")

link_directories(${XEN_LIB_PATH} ${XENBE_LIB_PATH})

add_executable(snd_be ${SOURCES})

target_link_libraries(snd_be xenbe xenctrl xenstore xenevtchn xengnttab asound pthread)

find_package(Doxygen)
if(DOXYGEN_FOUND)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/doxygen.cfg ${CMAKE_CURRENT_BINARY_DIR}/doxygen.cfg @ONLY)
	add_custom_target(
		doc ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/doxygen.cfg
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		COMMENT "Generating API documentation with Doxygen" VERBATIM
	)
endif(DOXYGEN_FOUND)
