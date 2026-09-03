if(NOT DEFINED MVDSV_SOURCE_DIR)
	message(FATAL_ERROR "MVDSV_SOURCE_DIR is required")
endif()

# This is the complete ledger for the entity-reference audit. The first group
# is reachable by a QCX game and must use the selected conversion boundary.
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
	src/pr_exec.c
	src/pr2_cmds.c
	src/pr2_exec.c
	src/progs.h
	src/sv_init.c)

if(EXISTS "${MVDSV_SOURCE_DIR}/src/pr_entity_references.c")
	message(FATAL_ERROR "standalone entity-reference implementation must be removed")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/progs.h" progs_header)
if(progs_header MATCHES "#define[ \t]+EDICT_TO_PROG"
		OR progs_header MATCHES "#define[ \t]+PROG_TO_EDICT")
	message(FATAL_ERROR "canonical entity conversions must be functions")
endif()
if(NOT progs_header MATCHES "int EDICT_TO_PROG\\(const edict_t \\*entity\\);"
		OR NOT progs_header MATCHES "edict_t \\*PROG_TO_EDICT\\(int reference\\);"
		OR NOT progs_header MATCHES "edict_t \\*PR_EntityFieldToEdict\\(const edict_t \\*owner, int field_offset\\);")
	message(FATAL_ERROR "canonical entity conversion declarations are incomplete")
endif()

execute_process(
	COMMAND rg -n -g "!entity_reference_routes_test.cmake"
		"PR1_EntityReference|PR2_EntityReference|PR_EntityReference|PR1_EntityFromReference|PR2_EntityFromReference|PR_EntityFromReference"
		src tests/qcx
	WORKING_DIRECTORY "${MVDSV_SOURCE_DIR}"
	OUTPUT_VARIABLE parallel_routes
	RESULT_VARIABLE parallel_result)
if(parallel_result EQUAL 0)
	message(FATAL_ERROR "parallel entity-reference facade remains:\n${parallel_routes}")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/pr2_exec.c" pr2_exec)
if(pr2_exec MATCHES "QCX_SlotToEdict|QCX_EdictToSlot")
	message(FATAL_ERROR
		"pr2_exec.c must route program entity words through the canonical functions")
endif()

execute_process(
	COMMAND sh -c "rg -n 'G_EDICT\\b|G_EDICTNUM|NUM_FOR_GAME_EDICT|sv\\.game_edicts|pr_edict_size' src | cut -d: -f1 | sort -u"
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
	if(content MATCHES "G_EDICT\\b|G_EDICTNUM|NUM_FOR_GAME_EDICT|sv\\.game_edicts|pr_edict_size")
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


require_selected_route(src/pr_cmds.c "EDICT_TO_PROG")
require_selected_route(src/pr2_cmds.c "EDICT_TO_PROG")
require_selected_route(src/pr2_exec.c "EDICT_TO_PROG")
require_selected_route(src/sv_init.c "EDICT_TO_PROG")
require_selected_route(src/sv_main.c "EDICT_TO_PROG")
require_selected_route(src/sv_move.c "EDICT_TO_PROG")
require_selected_route(src/sv_phys.c "EDICT_TO_PROG")
require_selected_route(src/sv_user.c "EDICT_TO_PROG")
require_selected_route(src/sv_world.c "EDICT_TO_PROG")

require_selected_route(src/qcx/test_observer.c "PROG_TO_EDICT")

require_selected_route(src/sv_world.c "PROG_TO_EDICT")
require_selected_route(src/sv_move.c "PROG_TO_EDICT")
require_selected_route(src/sv_phys.c "PROG_TO_EDICT")
require_selected_route(src/sv_send.c "PROG_TO_EDICT")
require_selected_route(src/sv_ents.c "PR_EntityFieldToEdict")

file(READ "${MVDSV_SOURCE_DIR}/src/sv_ents.c" ents_content)
string(REGEX MATCHALL "PR_EntityFieldToEdict\\(client->edict, fofs_hideentity\\)"
	hideentity_routes "${ents_content}")
list(LENGTH hideentity_routes hideentity_route_count)
if(NOT hideentity_route_count EQUAL 2)
	message(FATAL_ERROR "sv_ents.c: both hideentity consumers must use the selected boundary")
endif()

# qcx_reentry_test deliberately exercises fatal invalid-reference paths.  Its
# SV_Error double must never longjmp to a helper frame that has already
# returned, and its slot double must not compare unrelated pointers by order.
file(READ "${MVDSV_SOURCE_DIR}/tests/qcx/reentry_test.c" reentry_test)
if(NOT reentry_test MATCHES "static qbool expecting_error;")
	message(FATAL_ERROR "reentry fixture must track expected SV_Error calls")
endif()
string(FIND "${reentry_test}" "void SV_Error(char *error, ...)\n{\n\t(void)error;\n\tif (!expecting_error)\n\t\tabort();"
	unexpected_error_guard)
if(unexpected_error_guard EQUAL -1)
	message(FATAL_ERROR "reentry fixture must abort on an unexpected SV_Error")
endif()
string(FIND "${reentry_test}" "static void expect_invalid_qcx_reference(int reference)\n{\n\texpecting_error = true;"
	invalid_reference_armed)
string(FIND "${reentry_test}" "static void expect_invalid_qcx_edict(const edict_t *entity)\n{\n\texpecting_error = true;"
	invalid_edict_armed)
if(invalid_reference_armed EQUAL -1 OR invalid_edict_armed EQUAL -1)
	message(FATAL_ERROR "each expected invalid-reference path must arm SV_Error")
endif()
if(reentry_test MATCHES "entity >= entities|entity < entities|entity >= sv\\.edicts|entity < sv\\.edicts")
	message(FATAL_ERROR "reentry fixture must not order-compare unrelated edict pointers")
endif()
