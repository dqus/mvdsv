if(NOT DEFINED MVDSV_BINARY_DIR)
	message(FATAL_ERROR "MVDSV_BINARY_DIR is required")
endif()

execute_process(
	COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${MVDSV_BINARY_DIR}"
		--show-only=json-v1
	RESULT_VARIABLE ctest_result
	OUTPUT_VARIABLE ctest_json
	ERROR_VARIABLE ctest_error)
if(NOT ctest_result EQUAL 0)
	message(FATAL_ERROR "could not inspect CTest labels: ${ctest_error}")
endif()

set(expected_fast
	qcx_naming
	qcx_transport
	qcx_adapter_state
	qcx_program_type_routes
	qcx_terminal
	qcx_startup_failure
	qcx_globals
	qcx_globals_boundary
	qcx_entities
	qcx_services_world
	qcx_client
	qcx_client_routes
	qcx_pr2_api_routes
	qcx_reentry
	qcx_touch_links
	qcx_entry_routes
	qcx_startup_routes
	qcx_entity_reference_routes
	qcx_legacy_entity_references
	qcx_optional_fields
	qcx_optional_field_routes
	qcx_legacy_strings
	qcx_string_routes
	qcx_legacy_optional_fields_pr1
	qcx_legacy_optional_fields_pr2
	qcx_save_format
	qcx_restore
	qcx_label_contract)

set(expected_integration
	qcx_reentry_fixture
	qc2cpp_optional_fields_native_build
	qc2cpp_optional_fields_wasm_build
	qc2cpp_server_map_native
	qc2cpp_legacy_strings_native
	qc2cpp_optional_fields_native
	qc2cpp_save_native_native
	qc2cpp_fatal_native
	qc2cpp_client_native
	qc2cpp_network_native
	qc2cpp_spectator_native
	qc2cpp_save_connected_native
	qc2cpp_server_map_wasm
	qc2cpp_legacy_strings_wasm
	qc2cpp_optional_fields_wasm
	qc2cpp_save_wasm_wasm
	qc2cpp_fatal_wasm
	qc2cpp_restore_oom_wasm
	qc2cpp_save_native_wasm
	qc2cpp_save_wasm_native
	qc2cpp_network_wasm
	qc2cpp_spectator_wasm
	qc2cpp_save_connected_wasm)

string(JSON test_count LENGTH "${ctest_json}" tests)
math(EXPR last_test "${test_count} - 1")
set(observed_fast)
set(observed_integration)
set(observed_tests)
foreach(index RANGE ${last_test})
	string(JSON test_name GET "${ctest_json}" tests ${index} name)
	list(APPEND observed_tests "${test_name}")
	string(JSON property_count LENGTH "${ctest_json}" tests ${index} properties)
	set(test_labels)
	math(EXPR last_property "${property_count} - 1")
	foreach(property_index RANGE ${last_property})
		string(JSON property_name GET "${ctest_json}" tests ${index} properties
			${property_index} name)
		if(property_name STREQUAL "LABELS")
			string(JSON label_count LENGTH "${ctest_json}" tests ${index} properties
				${property_index} value)
			math(EXPR last_label "${label_count} - 1")
			foreach(label_index RANGE ${last_label})
				string(JSON label GET "${ctest_json}" tests ${index} properties
					${property_index} value ${label_index})
				list(APPEND test_labels "${label}")
			endforeach()
		endif()
	endforeach()
	list(FIND test_labels "fast" fast_index)
	list(FIND test_labels "integration" integration_index)
	if(NOT fast_index EQUAL -1)
		list(APPEND observed_fast "${test_name}")
	endif()
	if(NOT integration_index EQUAL -1)
		list(APPEND observed_integration "${test_name}")
	endif()
endforeach()

set(configured_integration)
foreach(test_name IN LISTS expected_integration)
	list(FIND observed_tests "${test_name}" configured_index)
	if(NOT configured_index EQUAL -1)
		list(APPEND configured_integration "${test_name}")
	endif()
endforeach()

set(expected_tests ${expected_fast} ${configured_integration})
list(SORT expected_tests)
list(SORT observed_tests)
if(NOT expected_tests STREQUAL observed_tests)
	message(FATAL_ERROR
		"MVDSV tiered tests differ: expected=${expected_tests} observed=${observed_tests}")
endif()

list(SORT expected_fast)
list(SORT observed_fast)
if(NOT expected_fast STREQUAL observed_fast)
	message(FATAL_ERROR
		"MVDSV fast tier differs: expected=${expected_fast} observed=${observed_fast}")
endif()

list(SORT configured_integration)
list(SORT observed_integration)
if(NOT configured_integration STREQUAL observed_integration)
	message(FATAL_ERROR
		"MVDSV integration tier differs: expected=${configured_integration} observed=${observed_integration}")
endif()
