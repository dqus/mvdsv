if(NOT DEFINED MVDSV_SOURCE_DIR)
	message(FATAL_ERROR "MVDSV_SOURCE_DIR is required")
endif()

function(require_count relative_path needle expected)
	file(READ "${MVDSV_SOURCE_DIR}/${relative_path}" source)
	string(REGEX MATCHALL "${needle}" matches "${source}")
	list(LENGTH matches actual)
	if(NOT actual EQUAL expected)
		message(FATAL_ERROR "${relative_path}: expected ${expected} occurrences of ${needle}, found ${actual}")
	endif()
endfunction()

# Gameplay sites set canonical globals and enter the existing PR2 seam. They
# must not select QCX or marshal callback arguments themselves.
foreach(server_source IN ITEMS src/sv_phys.c src/sv_world.c src/sv_user.c)
	file(READ "${MVDSV_SOURCE_DIR}/${server_source}" source)
	if(source MATCHES "QCX_Dispatch")
		message(FATAL_ERROR "${server_source}: gameplay dispatch leaked past PR2")
	endif()
	if(source MATCHES "QCX_Active")
		message(FATAL_ERROR "${server_source}: QCX backend selection leaked past PR2")
	endif()
endforeach()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_save.c" save_source)
if(save_source MATCHES "QCX_")
	message(FATAL_ERROR "src/sv_save.c: QCX save selection leaked past PR2")
endif()

# The legacy call expressions deliberately remain at the server sites.  PR2
# selects QCX using canonical self/other/time/frametime, so it never evaluates
# their scalar func_t in QCX mode.
require_count("src/sv_phys.c" "PR_EdictTouch" 2)
require_count("src/sv_phys.c" "PR_EdictThink" 2)
require_count("src/sv_phys.c" "PR_EdictBlocked" 1)
require_count("src/sv_world.c" "PR_EdictTouch" 1)
require_count("src/sv_user.c" "PR_EdictTouch" 1)
require_count("src/sv_user.c" "PR_EdictBlocked" 1)

# PR2 is the only place that owns typed QCX dispatch for these entries.
require_count("src/pr2_exec.c" "QCX_DispatchEdictTouch" 1)
require_count("src/pr2_exec.c" "QCX_DispatchEdictThink" 1)
require_count("src/pr2_exec.c" "QCX_DispatchEdictBlocked" 1)

# These PR2 extension entries have no QC source-level implementation in the
# current QCX game model.  While QCX is selected, they must not fall through
# to the legacy PR1/VM paths.
function(require_qcx_noop function_declaration return_statement)
	file(READ "${MVDSV_SOURCE_DIR}/src/pr2_exec.c" source)
	string(FIND "${source}" "${function_declaration}" begin)
	if(begin EQUAL -1)
		message(FATAL_ERROR "src/pr2_exec.c: could not find ${function_declaration}")
	endif()
	string(SUBSTRING "${source}" ${begin} -1 function_tail)
	string(FIND "${function_tail}" "//===========================================================================" end)
	if(end EQUAL -1)
		message(FATAL_ERROR "src/pr2_exec.c: could not isolate ${function_declaration}")
	endif()
	string(SUBSTRING "${function_tail}" 0 ${end} function_source)
	if(NOT function_source MATCHES "#ifdef QCX_ENABLED")
		message(FATAL_ERROR "${function_declaration}: QCX branch is missing")
	endif()
	if(NOT function_source MATCHES "if \\(QCX_Active\\(\\)\\)")
		message(FATAL_ERROR "${function_declaration}: QCX active check is missing")
	endif()
	if(NOT function_source MATCHES "${return_statement}")
		message(FATAL_ERROR "${function_declaration}: QCX early return is missing")
	endif()
endfunction()

require_qcx_noop("void PR2_GameConsoleCommand(void)" "return;")
require_qcx_noop("void PR2_PausedTic(float duration)" "return;")
require_qcx_noop("qbool PR2_SendEntity(edict_t* e, edict_t* to, int sendflags)" "return false;")
