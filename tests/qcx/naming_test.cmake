if(NOT DEFINED MVDSV_SOURCE_DIR)
	message(FATAL_ERROR "qcx_naming requires MVDSV_SOURCE_DIR")
endif()
if(NOT DEFINED MVDSV_BINARY_DIR)
	message(FATAL_ERROR "qcx_naming requires MVDSV_BINARY_DIR")
endif()

foreach(required IN ITEMS src/qcx tests/qcx)
	if(NOT EXISTS "${MVDSV_SOURCE_DIR}/${required}")
		message(FATAL_ERROR "QCX naming requires ${required}")
	endif()
endforeach()

file(READ "${MVDSV_SOURCE_DIR}/CMakeLists.txt" root_cmake)
foreach(required IN ITEMS
	"QCX_ENABLED requires USE_PR2"
	"PRIVATE QCX_ENABLED=1"
	"PRIVATE QCX_NATIVE=1"
	"PRIVATE QCX_WASM=1"
	"PRIVATE QCX_TESTS=1")
	string(FIND "${root_cmake}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "QCX naming requires ${required} in CMakeLists.txt")
	endif()
endforeach()

execute_process(
	COMMAND "${CMAKE_COMMAND}" -S "${MVDSV_SOURCE_DIR}"
		-B "${MVDSV_BINARY_DIR}/qcx-naming-use-pr2-off"
		-DGIT_SUBMODULE=OFF -DUSE_PR2=OFF -DMVDSV_QC2CPP_NATIVE=ON
	RESULT_VARIABLE guard_status
	OUTPUT_VARIABLE guard_output
	ERROR_VARIABLE guard_error)
if(guard_status EQUAL 0
	OR NOT "${guard_output}${guard_error}" MATCHES "QCX_ENABLED requires USE_PR2")
	message(FATAL_ERROR "QCX naming requires QCX_ENABLED to reject USE_PR2=OFF")
endif()
foreach(obsolete IN ITEMS src/qc2cpp tests/qc2cpp)
	if(EXISTS "${MVDSV_SOURCE_DIR}/${obsolete}")
		message(FATAL_ERROR "QCX naming rejects live ${obsolete}")
	endif()
endforeach()

file(GLOB_RECURSE qcx_sources
	"${MVDSV_SOURCE_DIR}/src/qcx/*" "${MVDSV_SOURCE_DIR}/tests/qcx/*")
foreach(source IN LISTS qcx_sources)
	if(IS_DIRECTORY "${source}")
		continue()
	endif()
	if(source STREQUAL "${CMAKE_CURRENT_LIST_FILE}")
		continue()
	endif()
	file(READ "${source}" contents)
	if(contents MATCHES "(^|[^A-Za-z0-9_])QC_[A-Za-z0-9_]+"
		OR contents MATCHES "(^|[^A-Za-z0-9_])qc_[A-Za-z0-9_]+"
		OR contents MATCHES "MVDSV_QC2CPP_ENABLED")
		message(FATAL_ERROR "QCX naming rejects legacy private spelling in ${source}")
	endif()
endforeach()
