if(NOT DEFINED MVDSV_SOURCE_DIR)
	message(FATAL_ERROR "MVDSV_SOURCE_DIR is required")
endif()

foreach(source IN ITEMS sv_demo.c sv_ents.c sv_phys.c sv_send.c sv_user.c sv_world.c)
	file(READ "${MVDSV_SOURCE_DIR}/src/${source}" contents)
	if(contents MATCHES "QCX_CopyEntityString")
		message(FATAL_ERROR "${source} must use PR_GetEntityString, not a QCX string branch")
	endif()
endforeach()

foreach(required IN ITEMS
	"sv_demo.c:PR_GetEntityString(sv.edicts->v->message)"
	"sv_demo.c:PR_GetEntityString(ent->v->weaponmodel)"
	"sv_phys.c:PR_GetEntityString(ent->v->classname)"
	"sv_send.c:PR_GetEntityString(ent->v->weaponmodel)"
	"sv_user.c:PR_GetEntityString(sv.edicts->v->message)"
	"sv_user.c:PR_GetEntityString(e->v->classname)"
	"sv_world.c:PR_GetEntityString(touch->v->classname)")
	string(REPLACE ":" ";" parts "${required}")
	list(GET parts 0 source)
	list(GET parts 1 expression)
	file(READ "${MVDSV_SOURCE_DIR}/src/${source}" contents)
	string(FIND "${contents}" "${expression}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "${source} is missing ${expression}")
	endif()
endforeach()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_ents.c" entity_send_source)
string(REGEX MATCHALL "PR_EntityHasModel[ \t\r\n]*\\([ \t\r\n]*ent[ \t\r\n]*\\)"
	model_presence_calls "${entity_send_source}")
list(LENGTH model_presence_calls model_presence_call_count)
if(NOT model_presence_call_count EQUAL 2)
	message(FATAL_ERROR
		"sv_ents.c must contain exactly two PR_EntityHasModel(ent) calls")
endif()
if(entity_send_source MATCHES "QCX_EntityHasModel|#include[ \t]+[<\"]qcx/")
	message(FATAL_ERROR "sv_ents.c must not select the QCX model-presence backend")
endif()

# The audit is a fixed current compatibility surface: nine text calls can reach
# QCX; two retain their legacy-only contexts. A new call requires a fresh audit.
set(audited_sources pr_cmds.c pr2_cmds.c sv_demo.c sv_ents.c sv_phys.c sv_send.c sv_user.c sv_world.c)
set(audited_contents "")
foreach(source IN LISTS audited_sources)
	file(READ "${MVDSV_SOURCE_DIR}/src/${source}" contents)
	string(APPEND audited_contents "${contents}")
endforeach()
string(REGEX MATCHALL "PR_GetEntityString[ \t\r\n]*\\(" audited_calls "${audited_contents}")
list(LENGTH audited_calls audited_call_count)
if(NOT audited_call_count EQUAL 11)
	message(FATAL_ERROR "expected exactly 11 audited PR_GetEntityString calls, found ${audited_call_count}")
endif()

function(require_legacy_call_context source marker)
	file(READ "${MVDSV_SOURCE_DIR}/src/${source}" contents)
	string(FIND "${contents}" "${marker}" marker_position)
	if(marker_position EQUAL -1)
		message(FATAL_ERROR "legacy-only classification is missing ${source}:${marker}")
	endif()
	string(SUBSTRING "${contents}" ${marker_position} 4096 context)
	string(FIND "${context}" "PR_GetEntityString" call_position)
	if(call_position EQUAL -1)
		message(FATAL_ERROR "${source}:${marker} does not contain its legacy PR_GetEntityString call")
	endif()
endfunction()

# Legacy-only calls are intentionally retained for the PR1 and legacy PR2 paths.
# No QCX server route may be added beside them.
require_legacy_call_context(pr_cmds.c "void PF_makestatic (void)")
require_legacy_call_context(pr2_cmds.c "void PF2_makestatic(edict_t *ent)")
