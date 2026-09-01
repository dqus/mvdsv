add_library(qc2cpp_fixture_plugin MODULE fixture_plugin.c)
set_target_properties(qc2cpp_fixture_plugin PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF
	PREFIX ""
	OUTPUT_NAME "game")
if(APPLE)
	set_target_properties(qc2cpp_fixture_plugin PROPERTIES SUFFIX ".dylib")
endif()
target_link_libraries(qc2cpp_fixture_plugin PRIVATE Qc2cppHost::native)

add_library(qc2cpp_bad_abi_plugin MODULE fixture_plugin.c)
set_target_properties(qc2cpp_bad_abi_plugin PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF
	PREFIX ""
	OUTPUT_NAME "bad")
target_compile_definitions(qc2cpp_bad_abi_plugin PRIVATE QC2CPP_FIXTURE_BAD_ABI=1)
target_link_libraries(qc2cpp_bad_abi_plugin PRIVATE Qc2cppHost::native)

function(qc2cpp_add_bad_globals_fixture target output definition)
	add_library(${target} MODULE fixture_plugin.c)
	set_target_properties(${target} PROPERTIES
		C_STANDARD 11
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS OFF
		PREFIX ""
		OUTPUT_NAME ${output})
	if(APPLE)
		set_target_properties(${target} PROPERTIES SUFFIX ".dylib")
	endif()
	target_compile_definitions(${target} PRIVATE ${definition}=1)
	target_link_libraries(${target} PRIVATE Qc2cppHost::native)
endfunction()

qc2cpp_add_bad_globals_fixture(qc2cpp_bad_globals_abi_plugin bad_globals_abi
	QC2CPP_FIXTURE_BAD_ENGINE_ABI)
qc2cpp_add_bad_globals_fixture(qc2cpp_bad_globals_offset_plugin bad_globals_offset
	QC2CPP_FIXTURE_BAD_ENGINE_OFFSET)
qc2cpp_add_bad_globals_fixture(qc2cpp_bad_globals_size_plugin bad_globals_size
	QC2CPP_FIXTURE_BAD_GLOBAL_SIZE)

qc2cpp_add_bad_globals_fixture(qc2cpp_bad_entities_capacity_plugin bad_entities_capacity
	QC2CPP_FIXTURE_BAD_ENTITY_CAPACITY)
qc2cpp_add_bad_globals_fixture(qc2cpp_bad_entities_stride_plugin bad_entities_stride
	QC2CPP_FIXTURE_BAD_ENTITY_STRIDE)
qc2cpp_add_bad_globals_fixture(qc2cpp_bad_entities_overflow_plugin bad_entities_overflow
	QC2CPP_FIXTURE_BAD_ENTITY_OVERFLOW)

add_library(qc2cpp_no_query_plugin MODULE no_query_plugin.c)
set_target_properties(qc2cpp_no_query_plugin PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF
	PREFIX ""
	OUTPUT_NAME "no_query")
if(APPLE)
	set_target_properties(qc2cpp_bad_abi_plugin qc2cpp_no_query_plugin PROPERTIES SUFFIX ".dylib")
endif()

add_executable(qc2cpp_transport_test
	transport_test.c
	${PROJECT_SOURCE_DIR}/src/qc2cpp/transport.c
	${PROJECT_SOURCE_DIR}/src/qc2cpp/native.c
	${PROJECT_SOURCE_DIR}/src/qc2cpp/wasm.c)
set_target_properties(qc2cpp_transport_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qc2cpp_transport_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(qc2cpp_transport_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(qc2cpp_transport_test PRIVATE MVDSV_QC2CPP_NATIVE=1)
if(MVDSV_QC2CPP_WASM)
	target_link_libraries(qc2cpp_transport_test PRIVATE Qc2cppHost::wasmtime ${MVDSV_WASMTIME_LIBRARY})
	set_property(TARGET qc2cpp_transport_test APPEND PROPERTY BUILD_RPATH
		"${MVDSV_WASMTIME_ROOT}/lib")
	target_compile_definitions(qc2cpp_transport_test PRIVATE MVDSV_QC2CPP_WASM=1)
endif()
target_link_libraries(qc2cpp_transport_test PRIVATE Qc2cppHost::native ${CMAKE_DL_LIBS})
add_dependencies(qc2cpp_transport_test qc2cpp_fixture_plugin qc2cpp_bad_abi_plugin qc2cpp_no_query_plugin)
add_test(NAME qc2cpp_transport COMMAND qc2cpp_transport_test $<TARGET_FILE_DIR:qc2cpp_fixture_plugin>)

add_executable(qc2cpp_adapter_state_test
	adapter_state_test.c
	${PROJECT_SOURCE_DIR}/src/qc2cpp/adapter_state.c)
set_target_properties(qc2cpp_adapter_state_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qc2cpp_adapter_state_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(qc2cpp_adapter_state_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME qc2cpp_adapter_state COMMAND qc2cpp_adapter_state_test)

add_executable(qc2cpp_terminal_test terminal_test.c)
set_target_properties(qc2cpp_terminal_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qc2cpp_terminal_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qc2cpp_terminal_test PRIVATE
	-Wall -Wextra -Wpedantic -Werror -ffunction-sections)
if(APPLE)
	target_link_options(qc2cpp_terminal_test PRIVATE -Wl,-dead_strip)
endif()
target_compile_definitions(qc2cpp_terminal_test PRIVATE
	MVDSV_QC2CPP_ENABLED=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qc2cpp_terminal_test PRIVATE Qc2cppHost::native)
add_test(NAME qc2cpp_terminal COMMAND qc2cpp_terminal_test)

add_executable(qc2cpp_globals_test
	globals_test.c
	${PROJECT_SOURCE_DIR}/src/qc2cpp/globals.c
	${PROJECT_SOURCE_DIR}/src/qc2cpp/transport.c
	${PROJECT_SOURCE_DIR}/src/qc2cpp/native.c
	${PROJECT_SOURCE_DIR}/src/qc2cpp/wasm.c)
set_target_properties(qc2cpp_globals_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qc2cpp_globals_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qc2cpp_globals_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(qc2cpp_globals_test PRIVATE
	MVDSV_QC2CPP_ENABLED=1
	MVDSV_QC2CPP_NATIVE=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qc2cpp_globals_test PRIVATE Qc2cppHost::native ${CMAKE_DL_LIBS})
add_dependencies(qc2cpp_globals_test qc2cpp_fixture_plugin
	qc2cpp_bad_globals_abi_plugin qc2cpp_bad_globals_offset_plugin
	qc2cpp_bad_globals_size_plugin)
add_test(NAME qc2cpp_globals COMMAND qc2cpp_globals_test $<TARGET_FILE_DIR:qc2cpp_fixture_plugin>)

add_executable(qc2cpp_entities_test
	entities_test.c
${PROJECT_SOURCE_DIR}/src/qc2cpp/entities.c
${PROJECT_SOURCE_DIR}/src/qc2cpp/transport.c
${PROJECT_SOURCE_DIR}/src/qc2cpp/native.c
${PROJECT_SOURCE_DIR}/src/qc2cpp/wasm.c)
set_target_properties(qc2cpp_entities_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qc2cpp_entities_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qc2cpp_entities_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(qc2cpp_entities_test PRIVATE
	MVDSV_QC2CPP_ENABLED=1
	MVDSV_QC2CPP_NATIVE=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qc2cpp_entities_test PRIVATE Qc2cppHost::native ${CMAKE_DL_LIBS})
add_dependencies(qc2cpp_entities_test qc2cpp_fixture_plugin
	qc2cpp_bad_entities_capacity_plugin qc2cpp_bad_entities_stride_plugin
	qc2cpp_bad_entities_overflow_plugin)
add_test(NAME qc2cpp_entities COMMAND qc2cpp_entities_test $<TARGET_FILE_DIR:qc2cpp_fixture_plugin>)

add_executable(qc2cpp_services_world_test
	services_world_test.c
	${PROJECT_SOURCE_DIR}/src/qc2cpp/services_world.c)
set_target_properties(qc2cpp_services_world_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qc2cpp_services_world_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qc2cpp_services_world_test PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter)
target_compile_definitions(qc2cpp_services_world_test PRIVATE
	MVDSV_QC2CPP_ENABLED=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qc2cpp_services_world_test PRIVATE Qc2cppHost::native)
add_test(NAME qc2cpp_services_world COMMAND qc2cpp_services_world_test)

add_executable(qc2cpp_client_test

	client_test.c
	${PROJECT_SOURCE_DIR}/src/qc2cpp/services_network.c)
set_target_properties(qc2cpp_client_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qc2cpp_client_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qc2cpp_client_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(qc2cpp_client_test PRIVATE
	MVDSV_QC2CPP_ENABLED=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qc2cpp_client_test PRIVATE Qc2cppHost::native)
add_test(NAME qc2cpp_client COMMAND qc2cpp_client_test)

add_test(NAME qc2cpp_client_routes
	COMMAND ${CMAKE_COMMAND}
		-DMVDSV_SOURCE_DIR=${PROJECT_SOURCE_DIR}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/client_routes_test.cmake)

add_executable(qc2cpp_reentry_test
	reentry_test.c
	${PROJECT_SOURCE_DIR}/src/qc2cpp/entries.c
	${PROJECT_SOURCE_DIR}/src/qc2cpp/services_movement.c
	${PROJECT_SOURCE_DIR}/src/sv_phys.c)
set_target_properties(qc2cpp_reentry_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qc2cpp_reentry_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qc2cpp_reentry_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/sv_phys.c PROPERTIES
	COMPILE_OPTIONS "-Wno-missing-field-initializers;-Wno-unused-parameter")
target_compile_definitions(qc2cpp_reentry_test PRIVATE
	MVDSV_QC2CPP_ENABLED=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qc2cpp_reentry_test PRIVATE Qc2cppHost::native)
add_test(NAME qc2cpp_reentry COMMAND qc2cpp_reentry_test)

add_test(NAME qc2cpp_entry_routes
	COMMAND ${CMAKE_COMMAND}
		-DMVDSV_SOURCE_DIR=${PROJECT_SOURCE_DIR}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/entry_routes_test.cmake)

add_executable(qc2cpp_save_format_test
	save_format_test.c
	${PROJECT_SOURCE_DIR}/src/qc2cpp/save_format.c)
set_target_properties(qc2cpp_save_format_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qc2cpp_save_format_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(qc2cpp_save_format_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(qc2cpp_save_format_test PRIVATE Qc2cppHost::native)
add_test(NAME qc2cpp_save_format COMMAND qc2cpp_save_format_test)

add_executable(qc2cpp_restore_test
	restore_test.c
	${PROJECT_SOURCE_DIR}/src/qc2cpp/save.c)
set_target_properties(qc2cpp_restore_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qc2cpp_restore_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qc2cpp_restore_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(qc2cpp_restore_test PRIVATE
	MVDSV_QC2CPP_ENABLED=1
	MVDSV_QC2CPP_NATIVE=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qc2cpp_restore_test PRIVATE Qc2cppHost::native)
add_test(NAME qc2cpp_restore COMMAND qc2cpp_restore_test)

if(QC2CPP_COMPILER)
	add_test(NAME qc2cpp_reentry_fixture
		COMMAND ${CMAKE_COMMAND}
			-DQC2CPP_COMPILER=${QC2CPP_COMPILER}
			-DFIXTURE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/reentry
			-DFIXTURE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/reentry-generated
			-P ${CMAKE_CURRENT_SOURCE_DIR}/reentry_fixture_test.cmake)
endif()
