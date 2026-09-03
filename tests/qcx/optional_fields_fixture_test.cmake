foreach(required QC2CPP_COMPILER FIXTURE_SOURCE_DIR FIXTURE_BINARY_DIR)
	if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
		message(FATAL_ERROR "${required} is required")
	endif()
endforeach()

file(REMOVE_RECURSE "${FIXTURE_BINARY_DIR}")
set(generated_dir "${FIXTURE_BINARY_DIR}/generated")
execute_process(
	COMMAND "${QC2CPP_COMPILER}" "${FIXTURE_SOURCE_DIR}/progs.src" -o "${generated_dir}"
	RESULT_VARIABLE transpile_result)
if(NOT transpile_result EQUAL 0)
	message(FATAL_ERROR "qc2cpp optional-fields fixture did not transpile")
endif()
set(configure_args -S "${generated_dir}" -B "${FIXTURE_BINARY_DIR}/build")
if(NATIVE_PLUGIN)
	list(APPEND configure_args -DQC2CPP_NATIVE_PLUGIN=ON)
endif()
if(DEFINED WASI_SDK_ROOT AND NOT "${WASI_SDK_ROOT}" STREQUAL "")
	list(APPEND configure_args
		-DCMAKE_TOOLCHAIN_FILE=${generated_dir}/cmake/WasmToolchain.cmake
		-DQC_WASI_SDK_ROOT=${WASI_SDK_ROOT})
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${configure_args}
	RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
	message(FATAL_ERROR "qc2cpp optional-fields fixture did not configure")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${FIXTURE_BINARY_DIR}/build" --target game
	RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
	message(FATAL_ERROR "qc2cpp optional-fields fixture did not build")
endif()
