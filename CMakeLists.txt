cmake_minimum_required(VERSION 3.0)

set(PROJECT_NAME "snd_be")

project(${PROJECT_NAME})

# ######################################################################################################################
# Options
# ######################################################################################################################

option(WITH_PULSE "build with PulseAudio backend" ON)
option(WITH_ALSA "build with Alsa backend" ON)
option(WITH_MOCKBELIB "build with mock backend lib" OFF)
option(WITH_DOC "build with documenation" OFF)

message(STATUS)
message(STATUS "${PROJECT_NAME} Configuration:")
message(STATUS "CMAKE_BUILD_TYPE              = ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_INSTALL_PREFIX          = ${CMAKE_INSTALL_PREFIX}")
message(STATUS)
message(STATUS "WITH_DOC                      = ${WITH_DOC}")
message(STATUS "WITH_PULSE                    = ${WITH_PULSE}")
message(STATUS "WITH_ALSA                     = ${WITH_ALSA}")
message(STATUS)
message(STATUS "WITH_MOCKBELIB                = ${WITH_MOCKBELIB}")
message(STATUS)
message(STATUS "XEN_INCLUDE_PATH              = ${XEN_INCLUDE_PATH}")
message(STATUS "XENBE_INCLUDE_PATH            = ${XENBE_INCLUDE_PATH}")
message(STATUS "IF_INCLUDE_PATH               = ${IF_INCLUDE_PATH}")

if(WITH_MOCKBELIB)
  message(STATUS "XENBEMOCK_INCLUDE_PATH        = ${XENBEMOCK_INCLUDE_PATH}")
endif()

message(STATUS)
message(STATUS "XEN_LIB_PATH                  = ${XEN_LIB_PATH}")
message(STATUS "XENBE_LIB_PATH                = ${XENBE_LIB_PATH}")

if(WITH_MOCKBELIB)
  message(STATUS "XENBEMOCK_LIB_PATH            = ${XENBEMOCK_LIB_PATH}")
endif()

message(STATUS)

if(NOT WITH_PULSE AND NOT WITH_ALSA)
  message(FATAL_ERROR "At least one backend should be specified: WITH_ALSA, WITH_PULSE")
endif()

# ######################################################################################################################
# Compiler flags
# ######################################################################################################################

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11 -Wall")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")

# ######################################################################################################################
# Definitions
# ######################################################################################################################
if(WITH_ALSA)
  add_definitions(-DWITH_ALSA)
endif()

if(WITH_PULSE)
  add_definitions(-DWITH_PULSE)
endif()

if(WITH_MOCKBELIB)
  add_definitions(-DWITH_MOCKBELIB)
endif()

# ######################################################################################################################
# Includes
# ######################################################################################################################

include_directories(${XEN_INCLUDE_PATH} ${XENBE_INCLUDE_PATH} ${XENBEMOCK_INCLUDE_PATH} ${CMAKE_CURRENT_BINARY_DIR} .)

# ######################################################################################################################
# Link directories
# ######################################################################################################################

link_directories(${XEN_LIB_PATH} ${XENBE_LIB_PATH} ${XENBEMOCK_LIB_PATH})

# ######################################################################################################################
# Sources
# ######################################################################################################################

add_subdirectory(src)

# ######################################################################################################################
# Versioning
# ######################################################################################################################

find_package(Git)

if(NOT GIT_FOUND)
  set(GIT_EXECUTABLE git)
endif()

add_custom_target(
  Version
  COMMAND
    ${CMAKE_COMMAND} -D GIT_EXECUTABLE=${GIT_EXECUTABLE} -D INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/Version.hpp.in -D
    OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/Version.hpp -D GIT_SOURCE_DIR=${CMAKE_SOURCE_DIR} -P
    ${CMAKE_SOURCE_DIR}/generateVersion.cmake)

add_dependencies(${PROJECT_NAME} Version)

# ######################################################################################################################
# Docs
# ######################################################################################################################

find_package(Doxygen)

if(WITH_DOC AND NOT DOXYGEN_FOUND)
  message(FATAL_ERROR "Doxygen not found. Required to generate docs.")
else()
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/doxygen.cfg ${CMAKE_CURRENT_BINARY_DIR}/doxygen.cfg @ONLY)

  add_custom_target(
    doc
    ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/doxygen.cfg
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM)
endif()
