if(NOT DEFINED MVDSV_SOURCE_DIR)
	message(FATAL_ERROR "qcx_program_type_routes requires MVDSV_SOURCE_DIR")
endif()

function(require_text source label needle)
	string(FIND "${source}" "${needle}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "${label}: missing ${needle}")
	endif()
endfunction()

file(READ "${MVDSV_SOURCE_DIR}/src/pr2.h" pr2_header)
foreach(declaration IN ITEMS
	"PR2_PROGTYPE_PR1 = 0"
	"PR2_PROGTYPE_NATIVE = 1"
	"PR2_PROGTYPE_BYTECODE = 2"
	"PR2_PROGTYPE_COMPILED = 3"
	"PR2_PROGTYPE_QCX_NATIVE = 4"
	"PR2_PROGTYPE_QCX_WASM = 5")
	require_text("${pr2_header}" "src/pr2.h" "${declaration}")
endforeach()

file(READ "${MVDSV_SOURCE_DIR}/src/pr2_exec.c" pr2_source)
string(FIND "${pr2_source}" "void PR2_LoadProgs(void)" load_begin)
string(FIND "${pr2_source}" "void PR2_GameConsoleCommand(void)" load_end)
if(load_begin EQUAL -1 OR load_end EQUAL -1 OR load_end LESS load_begin)
	message(FATAL_ERROR "src/pr2_exec.c: could not isolate PR2_LoadProgs")
endif()
math(EXPR load_length "${load_end} - ${load_begin}")
string(SUBSTRING "${pr2_source}" ${load_begin} ${load_length} load_source)
foreach(required IN ITEMS
	"case PR2_PROGTYPE_QCX_NATIVE:"
	"case PR2_PROGTYPE_QCX_WASM:"
	"#ifdef QCX_ENABLED"
	"QCX_LoadProgs("
	"SV_Error(\"QCX program type"
	"VM_Create(")
	require_text("${load_source}" "PR2_LoadProgs" "${required}")
endforeach()
string(FIND "${load_source}" "case PR2_PROGTYPE_QCX_NATIVE:" native_case)
string(FIND "${load_source}" "case PR2_PROGTYPE_QCX_WASM:" wasm_case)
string(FIND "${load_source}" "#ifdef QCX_ENABLED" capability_gate)
string(FIND "${load_source}" "VM_Create(" vm_create)
if(native_case GREATER capability_gate OR wasm_case GREATER capability_gate
	OR capability_gate GREATER vm_create)
	message(FATAL_ERROR
		"PR2_LoadProgs: QCX cases must be unconditional and precede VM_Create")
endif()
string(REGEX MATCHALL "VM_Create\\(" vm_calls "${load_source}")
list(LENGTH vm_calls vm_call_count)
if(NOT vm_call_count EQUAL 1)
	message(FATAL_ERROR "PR2_LoadProgs: expected one VM_Create call")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/qcx/adapter.h" adapter_header)
if(adapter_header MATCHES "QCX_PROGTYPE_(NATIVE|WASM)")
	message(FATAL_ERROR "src/qcx/adapter.h must not own PR2 program types")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/qcx/transport.c" transport_source)
foreach(forbidden IN ITEMS "int mode" "mode == 4" "mode == 5" "transport->mode")
	if(transport_source MATCHES "${forbidden}")
		message(FATAL_ERROR "src/qcx/transport.c: forbidden ${forbidden}")
	endif()
endforeach()
foreach(required IN ITEMS "QCX_TRANSPORT_NATIVE" "QCX_TRANSPORT_WASM")
	require_text("${transport_source}" "src/qcx/transport.c" "${required}")
endforeach()
