add_library(qcx_fixture_plugin MODULE fixture_plugin.c)
add_test(NAME qcx_naming
	COMMAND ${CMAKE_COMMAND}
		-DMVDSV_SOURCE_DIR=${PROJECT_SOURCE_DIR}
		-DMVDSV_BINARY_DIR=${PROJECT_BINARY_DIR}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/naming_test.cmake)
set_target_properties(qcx_fixture_plugin PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF
	PREFIX ""
	OUTPUT_NAME "game")
if(APPLE)
	set_target_properties(qcx_fixture_plugin PROPERTIES SUFFIX ".dylib")
endif()
target_link_libraries(qcx_fixture_plugin PRIVATE Qc2cppHost::native)

add_library(qcx_bad_abi_plugin MODULE fixture_plugin.c)
set_target_properties(qcx_bad_abi_plugin PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF
	PREFIX ""
	OUTPUT_NAME "bad")
target_compile_definitions(qcx_bad_abi_plugin PRIVATE QC2CPP_FIXTURE_BAD_ABI=1)
target_link_libraries(qcx_bad_abi_plugin PRIVATE Qc2cppHost::native)

function(qcx_add_bad_globals_fixture target output definition)
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

qcx_add_bad_globals_fixture(qcx_bad_globals_abi_plugin bad_globals_abi
	QC2CPP_FIXTURE_BAD_ENGINE_ABI)
qcx_add_bad_globals_fixture(qcx_bad_globals_offset_plugin bad_globals_offset
	QC2CPP_FIXTURE_BAD_ENGINE_OFFSET)
qcx_add_bad_globals_fixture(qcx_bad_globals_size_plugin bad_globals_size
	QC2CPP_FIXTURE_BAD_GLOBAL_SIZE)

qcx_add_bad_globals_fixture(qcx_bad_entities_capacity_plugin bad_entities_capacity
	QC2CPP_FIXTURE_BAD_ENTITY_CAPACITY)
qcx_add_bad_globals_fixture(qcx_bad_entities_stride_plugin bad_entities_stride
	QC2CPP_FIXTURE_BAD_ENTITY_STRIDE)
qcx_add_bad_globals_fixture(qcx_bad_entities_overflow_plugin bad_entities_overflow
	QC2CPP_FIXTURE_BAD_ENTITY_OVERFLOW)

add_library(qcx_no_query_plugin MODULE no_query_plugin.c)
set_target_properties(qcx_no_query_plugin PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF
	PREFIX ""
	OUTPUT_NAME "no_query")
if(APPLE)
	set_target_properties(qcx_bad_abi_plugin qcx_no_query_plugin PROPERTIES SUFFIX ".dylib")
endif()

add_executable(qcx_transport_test
	transport_test.c
	${PROJECT_SOURCE_DIR}/src/qcx/transport.c
	${PROJECT_SOURCE_DIR}/src/qcx/native.c
	${PROJECT_SOURCE_DIR}/src/qcx/wasm.c)
set_target_properties(qcx_transport_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qcx_transport_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(qcx_transport_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(qcx_transport_test PRIVATE QCX_NATIVE=1)
if(MVDSV_QC2CPP_WASM)
	target_link_libraries(qcx_transport_test PRIVATE Qc2cppHost::wasmtime ${MVDSV_WASMTIME_LIBRARY})
	set_property(TARGET qcx_transport_test APPEND PROPERTY BUILD_RPATH
		"${MVDSV_WASMTIME_ROOT}/lib")
	target_compile_definitions(qcx_transport_test PRIVATE QCX_WASM=1)
endif()
target_link_libraries(qcx_transport_test PRIVATE Qc2cppHost::native ${CMAKE_DL_LIBS})
add_dependencies(qcx_transport_test qcx_fixture_plugin qcx_bad_abi_plugin qcx_no_query_plugin)
add_test(NAME qcx_transport COMMAND qcx_transport_test $<TARGET_FILE_DIR:qcx_fixture_plugin>)

add_executable(qcx_adapter_state_test
	adapter_state_test.c
	${PROJECT_SOURCE_DIR}/src/qcx/adapter_state.c)
set_target_properties(qcx_adapter_state_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qcx_adapter_state_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(qcx_adapter_state_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME qcx_adapter_state COMMAND qcx_adapter_state_test)

add_executable(qcx_terminal_test terminal_test.c)
set_target_properties(qcx_terminal_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qcx_terminal_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qcx_terminal_test PRIVATE
	-Wall -Wextra -Wpedantic -Werror -ffunction-sections -fvisibility=hidden)
if(APPLE)
	target_link_options(qcx_terminal_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
	target_link_options(qcx_terminal_test PRIVATE -Wl,--gc-sections)
endif()
target_compile_definitions(qcx_terminal_test PRIVATE
	QCX_ENABLED=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qcx_terminal_test PRIVATE Qc2cppHost::native)
add_test(NAME qcx_terminal COMMAND qcx_terminal_test)

add_executable(qcx_globals_test
	globals_test.c
	${PROJECT_SOURCE_DIR}/src/qcx/globals.c
	${PROJECT_SOURCE_DIR}/src/qcx/transport.c
	${PROJECT_SOURCE_DIR}/src/qcx/native.c
	${PROJECT_SOURCE_DIR}/src/qcx/wasm.c)
set_target_properties(qcx_globals_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qcx_globals_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qcx_globals_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(qcx_globals_test PRIVATE
	QCX_ENABLED=1
	QCX_NATIVE=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qcx_globals_test PRIVATE Qc2cppHost::native ${CMAKE_DL_LIBS})
add_dependencies(qcx_globals_test qcx_fixture_plugin
	qcx_bad_globals_abi_plugin qcx_bad_globals_offset_plugin
	qcx_bad_globals_size_plugin)
add_test(NAME qcx_globals COMMAND qcx_globals_test $<TARGET_FILE_DIR:qcx_fixture_plugin>)

add_test(NAME qcx_globals_boundary
	COMMAND ${CMAKE_COMMAND}
		-DMVDSV_SOURCE_DIR=${PROJECT_SOURCE_DIR}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/globals_boundary_test.cmake)

add_executable(qcx_entities_test
	entities_test.c
${PROJECT_SOURCE_DIR}/src/qcx/entities.c
${PROJECT_SOURCE_DIR}/src/qcx/transport.c
${PROJECT_SOURCE_DIR}/src/qcx/native.c
${PROJECT_SOURCE_DIR}/src/qcx/wasm.c)
set_target_properties(qcx_entities_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qcx_entities_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qcx_entities_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(qcx_entities_test PRIVATE
	QCX_ENABLED=1
	QCX_NATIVE=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qcx_entities_test PRIVATE Qc2cppHost::native ${CMAKE_DL_LIBS})
add_dependencies(qcx_entities_test qcx_fixture_plugin
	qcx_bad_entities_capacity_plugin qcx_bad_entities_stride_plugin
	qcx_bad_entities_overflow_plugin)
add_test(NAME qcx_entities COMMAND qcx_entities_test $<TARGET_FILE_DIR:qcx_fixture_plugin>)

add_executable(qcx_services_world_test
	services_world_test.c
	${PROJECT_SOURCE_DIR}/src/qcx/services_world.c)
set_target_properties(qcx_services_world_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qcx_services_world_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qcx_services_world_test PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter)
target_compile_definitions(qcx_services_world_test PRIVATE
	QCX_ENABLED=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qcx_services_world_test PRIVATE Qc2cppHost::native)
add_test(NAME qcx_services_world COMMAND qcx_services_world_test)

add_executable(qcx_client_test

	client_test.c
	${PROJECT_SOURCE_DIR}/src/qcx/services_network.c)
set_target_properties(qcx_client_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qcx_client_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qcx_client_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(qcx_client_test PRIVATE
	QCX_ENABLED=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qcx_client_test PRIVATE Qc2cppHost::native)
add_test(NAME qcx_client COMMAND qcx_client_test)

add_test(NAME qcx_client_routes
	COMMAND ${CMAKE_COMMAND}
		-DMVDSV_SOURCE_DIR=${PROJECT_SOURCE_DIR}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/client_routes_test.cmake)

add_executable(qcx_reentry_test
	reentry_test.c
	${PROJECT_SOURCE_DIR}/src/qcx/entries.c
	${PROJECT_SOURCE_DIR}/src/qcx/services_movement.c
	${PROJECT_SOURCE_DIR}/src/pr2_exec.c
	${PROJECT_SOURCE_DIR}/src/sv_phys.c)
set_target_properties(qcx_reentry_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qcx_reentry_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qcx_reentry_test PRIVATE
	-Wall -Wextra -Wpedantic -Werror -ffunction-sections -fdata-sections)
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/sv_phys.c PROPERTIES
	COMPILE_OPTIONS "-Wno-missing-field-initializers;-Wno-unused-parameter;-Wno-empty-body")
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/pr2_exec.c PROPERTIES
	COMPILE_OPTIONS "-Wno-missing-field-initializers")
target_compile_definitions(qcx_reentry_test PRIVATE
	QCX_ENABLED=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qcx_reentry_test PRIVATE Qc2cppHost::native m)
if(APPLE)
	target_link_options(qcx_reentry_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
	target_link_options(qcx_reentry_test PRIVATE -Wl,--gc-sections)
endif()
add_test(NAME qcx_reentry COMMAND qcx_reentry_test)

add_test(NAME qcx_entry_routes
	COMMAND ${CMAKE_COMMAND}
		-DMVDSV_SOURCE_DIR=${PROJECT_SOURCE_DIR}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/entry_routes_test.cmake)

add_test(NAME qcx_startup_routes
	COMMAND ${CMAKE_COMMAND}
		-DMVDSV_SOURCE_DIR=${PROJECT_SOURCE_DIR}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/startup_routes_test.cmake)

add_test(NAME qcx_entity_reference_routes
	COMMAND ${CMAKE_COMMAND}
		-DMVDSV_SOURCE_DIR=${PROJECT_SOURCE_DIR}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/entity_reference_routes_test.cmake)

add_executable(qcx_legacy_entity_references_test
	legacy_entity_references_test.c
	${PROJECT_SOURCE_DIR}/src/pr_entity_references.c)
set_target_properties(qcx_legacy_entity_references_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qcx_legacy_entity_references_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qcx_legacy_entity_references_test PRIVATE
	-Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(qcx_legacy_entity_references_test PRIVATE
	SERVERONLY
	__LITTLE_ENDIAN__Q__)
add_test(NAME qcx_legacy_entity_references COMMAND qcx_legacy_entity_references_test)

add_executable(qcx_optional_fields_test
	optional_fields_test.c
	${PROJECT_SOURCE_DIR}/src/qcx/entities.c)
set_target_properties(qcx_optional_fields_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qcx_optional_fields_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qcx_optional_fields_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(qcx_optional_fields_test PRIVATE
	QCX_ENABLED=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qcx_optional_fields_test PRIVATE Qc2cppHost::native)
add_test(NAME qcx_optional_fields COMMAND qcx_optional_fields_test)

add_test(NAME qcx_optional_field_routes
	COMMAND ${CMAKE_COMMAND}
		-DMVDSV_SOURCE_DIR=${PROJECT_SOURCE_DIR}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/optional_field_routes_test.cmake)

function(qcx_add_legacy_optional_fields_test target)
	add_executable(${target}
		legacy_optional_fields_test.c
		${PROJECT_SOURCE_DIR}/src/pr_optional_fields.c
		${PROJECT_SOURCE_DIR}/src/pr_entity_references.c)
	set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF)
	target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/qwprot/src)
	target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
	target_compile_definitions(${target} PRIVATE SERVERONLY __LITTLE_ENDIAN__Q__ ${ARGN})
	add_test(NAME ${target} COMMAND ${target})
endfunction()
qcx_add_legacy_optional_fields_test(qcx_legacy_optional_fields_pr1)
qcx_add_legacy_optional_fields_test(qcx_legacy_optional_fields_pr2 USE_PR2=1)

add_executable(qcx_save_format_test
	save_format_test.c
	${PROJECT_SOURCE_DIR}/src/qcx/save_format.c)
set_target_properties(qcx_save_format_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qcx_save_format_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(qcx_save_format_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(qcx_save_format_test PRIVATE Qc2cppHost::native)
add_test(NAME qcx_save_format COMMAND qcx_save_format_test)

add_executable(qcx_restore_test
	restore_test.c
	${PROJECT_SOURCE_DIR}/src/qcx/save.c)
set_target_properties(qcx_restore_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS OFF)
target_include_directories(qcx_restore_test PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/qwprot/src)
target_compile_options(qcx_restore_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(qcx_restore_test PRIVATE
	QCX_ENABLED=1
	QCX_NATIVE=1
	SERVERONLY
	USE_PR2
	__LITTLE_ENDIAN__Q__)
target_link_libraries(qcx_restore_test PRIVATE Qc2cppHost::native)
add_test(NAME qcx_restore COMMAND qcx_restore_test)

if(QC2CPP_COMPILER)
	add_custom_target(qc2cpp_optional_fields_native_assets
		COMMAND ${CMAKE_COMMAND}
			-DQC2CPP_COMPILER=${QC2CPP_COMPILER}
			-DFIXTURE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/optional_fields
			-DFIXTURE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/optional-fields-native
			-DNATIVE_PLUGIN=ON
			-P ${CMAKE_CURRENT_SOURCE_DIR}/optional_fields_fixture_test.cmake)
	add_test(NAME qc2cpp_optional_fields_native_build
		COMMAND ${CMAKE_COMMAND}
			-DQC2CPP_COMPILER=${QC2CPP_COMPILER}
			-DFIXTURE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/optional_fields
			-DFIXTURE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/optional-fields-native
			-DNATIVE_PLUGIN=ON
			-P ${CMAKE_CURRENT_SOURCE_DIR}/optional_fields_fixture_test.cmake)
	if(MVDSV_QC2CPP_WASM)
		add_custom_target(qc2cpp_optional_fields_wasm_assets
			COMMAND ${CMAKE_COMMAND}
				-DQC2CPP_COMPILER=${QC2CPP_COMPILER}
				-DFIXTURE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/optional_fields
				-DFIXTURE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/optional-fields-wasm
				-DWASI_SDK_ROOT=${QC2CPP_WASI_SDK_ROOT}
				-P ${CMAKE_CURRENT_SOURCE_DIR}/optional_fields_fixture_test.cmake)
		add_test(NAME qc2cpp_optional_fields_wasm_build
			COMMAND ${CMAKE_COMMAND}
				-DQC2CPP_COMPILER=${QC2CPP_COMPILER}
				-DFIXTURE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/optional_fields
				-DFIXTURE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/optional-fields-wasm
				-DWASI_SDK_ROOT=${QC2CPP_WASI_SDK_ROOT}
				-P ${CMAKE_CURRENT_SOURCE_DIR}/optional_fields_fixture_test.cmake)
	endif()
	add_test(NAME qcx_reentry_fixture
		COMMAND ${CMAKE_COMMAND}
			-DQC2CPP_COMPILER=${QC2CPP_COMPILER}
			-DFIXTURE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/reentry
			-DFIXTURE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/reentry-generated
			-P ${CMAKE_CURRENT_SOURCE_DIR}/reentry_fixture_test.cmake)
endif()
