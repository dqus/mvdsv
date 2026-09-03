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
	"sv_ents.c:PR_GetEntityString(ent->v->model)"
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

# The audit is a fixed current compatibility surface: nine calls can reach QCX;
# three retain their legacy-only contexts.  A new call requires a fresh audit.
set(audited_sources pr_cmds.c pr2_cmds.c sv_demo.c sv_ents.c sv_phys.c sv_send.c sv_user.c sv_world.c)
set(audited_contents "")
foreach(source IN LISTS audited_sources)
	file(READ "${MVDSV_SOURCE_DIR}/src/${source}" contents)
	string(APPEND audited_contents "${contents}")
endforeach()
string(REGEX MATCHALL "PR_GetEntityString[ \t\r\n]*\\(" audited_calls "${audited_contents}")
list(LENGTH audited_calls audited_call_count)
if(NOT audited_call_count EQUAL 12)
	message(FATAL_ERROR "expected exactly 12 audited PR_GetEntityString calls, found ${audited_call_count}")
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

# Legacy-only calls are intentionally retained for the PR1, legacy PR2 and
# NetQuake-only paths.  No QCX server route may be added beside them.
require_legacy_call_context(pr_cmds.c "void PF_makestatic (void)")
require_legacy_call_context(pr2_cmds.c "void PF2_makestatic(edict_t *ent)")
require_legacy_call_context(sv_ents.c "// Translate NQ progs' EF_MUZZLEFLASH")
