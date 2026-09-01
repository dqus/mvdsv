foreach(required QC2CPP_COMPILER FIXTURE_SOURCE_DIR FIXTURE_BINARY_DIR)
	if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
		message(FATAL_ERROR "${required} is required")
	endif()
endforeach()
if(NOT EXISTS "${QC2CPP_COMPILER}")
	message(FATAL_ERROR "qc2cpp compiler does not exist: ${QC2CPP_COMPILER}")
endif()

file(REMOVE_RECURSE "${FIXTURE_BINARY_DIR}")
set(generated_dir "${FIXTURE_BINARY_DIR}/generated")
execute_process(
	COMMAND "${QC2CPP_COMPILER}" "${FIXTURE_SOURCE_DIR}/progs.src" -o "${generated_dir}"
	RESULT_VARIABLE transpile_result)
if(NOT transpile_result EQUAL 0)
	message(FATAL_ERROR "qc2cpp reentry fixture did not transpile")
endif()
execute_process(
	COMMAND "${CMAKE_COMMAND}" -S "${generated_dir}" -B "${FIXTURE_BINARY_DIR}/build"
	RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
	message(FATAL_ERROR "qc2cpp reentry fixture did not configure")
endif()
execute_process(
	COMMAND "${CMAKE_COMMAND}" --build "${FIXTURE_BINARY_DIR}/build" --target game
	RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
	message(FATAL_ERROR "qc2cpp reentry fixture did not build")
endif()
