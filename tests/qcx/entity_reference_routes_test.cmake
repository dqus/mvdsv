if(NOT DEFINED MVDSV_SOURCE_DIR)
	message(FATAL_ERROR "MVDSV_SOURCE_DIR is required")
endif()

# This is the complete ledger for the entity-reference audit.  The first group
# is reachable by a QCX game and must use the selected PR conversion boundary.
# The remaining uses execute only legacy PR1/PR2 VM data or define its ABI.
set(qcx_live_sources
	src/sv_ents.c
	src/sv_move.c
	src/sv_phys.c
	src/sv_send.c
	src/sv_world.c)
set(legacy_only_sources
	src/g_public.h
	src/pr_cmds.c
	src/pr_edict.c
	src/pr_entity_references.c
	src/pr_exec.c
	src/pr2_cmds.c
	src/pr2_exec.c
	src/progs.h
	src/sv_init.c)

execute_process(
	COMMAND sh -c "rg -n 'EDICT_TO_PROG|PROG_TO_EDICT|G_EDICT\\b|G_EDICTNUM|NUM_FOR_GAME_EDICT|sv\\.game_edicts|pr_edict_size' src | cut -d: -f1 | sort -u"
	WORKING_DIRECTORY "${MVDSV_SOURCE_DIR}"
	OUTPUT_VARIABLE audit_sources
	RESULT_VARIABLE audit_result)
if(NOT audit_result EQUAL 0)
	message(FATAL_ERROR "entity-reference audit unexpectedly found no source routes")
endif()

string(REPLACE "\n" ";" audit_source_list "${audit_sources}")
foreach(source IN LISTS audit_source_list)
	if(source STREQUAL "")
		continue()
	endif()
	list(FIND qcx_live_sources "${source}" live_index)
	list(FIND legacy_only_sources "${source}" legacy_index)
	if(live_index EQUAL -1 AND legacy_index EQUAL -1)
		message(FATAL_ERROR "unclassified entity-reference source: ${source}")
	endif()
endforeach()

foreach(source IN LISTS qcx_live_sources)
	file(READ "${MVDSV_SOURCE_DIR}/${source}" content)
	if(content MATCHES "PROG_TO_EDICT|/ pr_edict_size")
		message(FATAL_ERROR "${source}: live QCX route bypasses selected entity conversion")
	endif()
endforeach()

# The live route ledger is intentionally concrete.  The unit tests exercise
# both representations at the selected PR boundary; these assertions prevent
# the actual server call sites from quietly returning to byte arithmetic.
function(require_selected_route source pattern)
	file(READ "${MVDSV_SOURCE_DIR}/${source}" content)
	if(NOT content MATCHES "${pattern}")
		message(FATAL_ERROR "${source}: missing selected entity conversion route")
	endif()
endfunction()

require_selected_route(src/sv_world.c "PR_EntityFromReference")
require_selected_route(src/sv_move.c "PR_EntityFromReference")
require_selected_route(src/sv_phys.c "PR_EntityFromReference")
require_selected_route(src/sv_send.c "PR_EntityFromReference")
require_selected_route(src/sv_ents.c "PR_EntityFieldToEdict")

file(READ "${MVDSV_SOURCE_DIR}/src/sv_ents.c" ents_content)
string(REGEX MATCHALL "PR_EntityFieldToEdict\\(client->edict, fofs_hideentity\\)"
	hideentity_routes "${ents_content}")
list(LENGTH hideentity_routes hideentity_route_count)
if(NOT hideentity_route_count EQUAL 2)
	message(FATAL_ERROR "sv_ents.c: both hideentity consumers must use the selected boundary")
endif()
