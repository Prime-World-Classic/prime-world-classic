# Require successful teardown as well as the exact asset-backed startup verdict.
if(DEFINED ROOT)
	execute_process(COMMAND "${CLIENT}" --bootstrap-session-startup-probe --root "${ROOT}"
		RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 50)
	if(NOT result STREQUAL "0" OR NOT output MATCHES "\nSession startup: PASS roots=yes effects=yes chain=yes\n")
		message(FATAL_ERROR "Startup failed: ${result}\n${output}\n${error}")
	endif()
	message(STATUS "Session startup and teardown passed")
	return()
endif()

# Reject missing/invalid explicit roots before asset fallback, graphics or replay setup.
foreach(mode missing invalid)
	set(arguments --bootstrap-session-startup-probe)
	if(mode STREQUAL "invalid")
		list(APPEND arguments --root "${INVALID_ROOT}")
	endif()
	execute_process(COMMAND "${CLIENT}" ${arguments}
		RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 4)
	if(NOT result STREQUAL "1" OR NOT error STREQUAL "Session startup: FAIL explicit game root required\n")
		message(FATAL_ERROR "Unexpected ${mode}-root outcome: ${result}\n${output}\n${error}")
	endif()
endforeach()
