set(GIT_VERSION "undefined")

execute_process(
	WORKING_DIRECTORY ${GIT_SOURCE_DIR}
	COMMAND ${GIT_EXECUTABLE} describe --tags --always --abbrev=4 --dirty
	OUTPUT_VARIABLE GIT_VERSION
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

message(STATUS "VERSION: ${GIT_VERSION}")

configure_file(${INPUT_FILE} ${OUTPUT_FILE})
