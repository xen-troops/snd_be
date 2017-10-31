set(GIT_VERSION "undefined")

execute_process(
	COMMAND
	${GIT_EXECUTABLE} describe --tags --always --abbrev=4 --dirty
    	OUTPUT_VARIABLE GIT_VERSION
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

configure_file(${INPUT_FILE} ${OUTPUT_FILE})
