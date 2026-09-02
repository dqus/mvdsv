if(NOT DEFINED MVDSV_SOURCE_DIR)
	message(FATAL_ERROR "qcx_globals_boundary requires MVDSV_SOURCE_DIR")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/progs.h" progs)
if(progs MATCHES "PR_Global_" OR progs MATCHES "PR_SetGlobal_")
	message(FATAL_ERROR "canonical globals reject field-selection facades")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/pr2_exec.c" pr2)
string(REGEX MATCHALL "QCX_SetMapName" mapname_writers "${pr2}")
list(LENGTH mapname_writers mapname_writer_count)
if(NOT pr2 MATCHES "void PR_SetMapName\\(const char \\*value\\)"
	OR NOT mapname_writer_count EQUAL 1)
	message(FATAL_ERROR "mapname requires one semantic PR_SetMapName boundary")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_init.c" startup)
if(startup MATCHES "QCX_SetMapName" OR NOT startup MATCHES "PR_SetMapName\\(sv.mapname\\)")
	message(FATAL_ERROR "startup must use PR_SetMapName")
endif()
