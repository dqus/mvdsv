if(NOT DEFINED MVDSV_SOURCE_DIR)
	message(FATAL_ERROR "MVDSV_SOURCE_DIR is required")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/server.h" server_header)
if(server_header MATCHES "cs_loadzombie")
	message(FATAL_ERROR "restored QCX clients must not add cs_loadzombie")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_user.c" sv_user)
foreach(required IN ITEMS
	"QCX_RestoreSessionClientWaiting(sv_client)"
	"QCX_RestoreSessionPrepareSpawn(sv_client)"
	"QCX_RestoreSessionCommitBegin(sv_client, &restoring_qcx_client)"
	"QCX_RestoreSessionClientRoleLocked(sv_client)"
	"QCX_RestoreSessionNameChanged(sv_client)"
	"qcx_restore_list")
	string(FIND "${sv_user}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "src/sv_user.c: missing restored-client route ${required}")
	endif()
endforeach()

string(FIND "${sv_user}" "static void Cmd_New_f (void)" new_begin)
string(FIND "${sv_user}" "void SV_QCXStartClientSignon(client_t *client)" new_end)
if(new_begin EQUAL -1 OR new_end EQUAL -1 OR NOT new_begin LESS new_end)
	message(FATAL_ERROR "could not isolate Cmd_New_f")
endif()
math(EXPR new_length "${new_end} - ${new_begin}")
string(SUBSTRING "${sv_user}" ${new_begin} ${new_length} new_source)
string(FIND "${new_source}" "QCX_RestoreSessionEffectiveSpectator(sv_client)" effective_role)
if(effective_role EQUAL -1)
	message(FATAL_ERROR "Cmd_New_f must encode the saved role for a bound QCX client")
endif()

string(FIND "${sv_user}" "if (sv.paused && !pending_qcx_client && !waiting_qcx_client)" position)
if(position EQUAL -1)
	message(FATAL_ERROR "Cmd_Begin must defer pause notification for every QCX restore handshake")
endif()

string(FIND "${sv_user}" "{\"qcx_restore_list\", Cmd_RestoreList_f, false}" position)
if(position EQUAL -1)
	message(FATAL_ERROR "qcx_restore_list must be a non-overrideable engine command")
endif()

string(FIND "${sv_user}" "static void Cmd_Spawn_f (void)" spawn_begin)
string(FIND "${sv_user}" "static void SV_SpawnSpectator (void)" spawn_end)
if(spawn_begin EQUAL -1 OR spawn_end EQUAL -1 OR NOT spawn_begin LESS spawn_end)
	message(FATAL_ERROR "could not isolate Cmd_Spawn_f")
endif()
math(EXPR spawn_length "${spawn_end} - ${spawn_begin}")
string(SUBSTRING "${sv_user}" ${spawn_begin} ${spawn_length} spawn_source)
string(FIND "${spawn_source}" "QCX_RestoreSessionPrepareSpawn(sv_client)" prepare_spawn)
string(FIND "${spawn_source}" "else if (sv.loadgame)" legacy_load)
if(prepare_spawn EQUAL -1 OR legacy_load EQUAL -1 OR NOT prepare_spawn LESS legacy_load)
	message(FATAL_ERROR
		"Cmd_Spawn_f must choose the per-client QCX restored path before legacy sv.loadgame")
endif()
string(FIND "${spawn_source}" "if (QCX_RestoreSessionClientWaiting(sv_client)) return;" spawn_wait_guard)
string(FIND "${spawn_source}" "if (sv_client->state != cs_connected)" spawn_state_check)
if(spawn_wait_guard EQUAL -1 OR spawn_state_check EQUAL -1 OR NOT spawn_wait_guard LESS spawn_state_check)
	message(FATAL_ERROR "Cmd_Spawn_f must reject waiting QCX clients before state changes")
endif()

string(FIND "${sv_user}" "static void Cmd_Begin_f (void)" begin_begin)
string(FIND "${sv_user}" "//=============================================================================" begin_end)
if(begin_begin EQUAL -1 OR begin_end EQUAL -1 OR NOT begin_begin LESS begin_end)
	message(FATAL_ERROR "could not isolate Cmd_Begin_f")
endif()
math(EXPR begin_length "${begin_end} - ${begin_begin}")
string(SUBSTRING "${sv_user}" ${begin_begin} ${begin_length} begin_source)
foreach(required IN ITEMS
	"pending_qcx_client = QCX_RestoreSessionClientPending(sv_client)"
	"waiting_qcx_client = QCX_RestoreSessionClientWaiting(sv_client)"
	"if (!restoring_qcx_client && !waiting_qcx_client && !sv.loadgame)"
	"if (sv.loadgame || restoring_qcx_client)"
	"if (pending_qcx_client"
	"!QCX_RestoreSessionCommitBegin(sv_client, &restoring_qcx_client)")
	string(FIND "${begin_source}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "Cmd_Begin_f must use per-client restored state: ${required}")
	endif()
endforeach()
string(FIND "${begin_source}" "if (QCX_RestoreSessionClientWaiting(sv_client)) return;" begin_wait_guard)
string(FIND "${begin_source}" "if (sv_client->state == cs_spawned)" begin_state_check)
if(begin_wait_guard EQUAL -1 OR begin_state_check EQUAL -1 OR NOT begin_wait_guard LESS begin_state_check)
	message(FATAL_ERROR "Cmd_Begin_f must reject waiting QCX clients before state changes")
endif()

# The committed spawned flag must control the entire ordinary initialization
# block; committing afterward would initialize the wrong role/team/parms.
string(FIND "${begin_source}" "pending_qcx_client = QCX_RestoreSessionClientPending(sv_client)" begin_pending)
string(FIND "${begin_source}" "QCX_RestoreSessionCommitBegin(sv_client, &restoring_qcx_client)" begin_commit)
string(FIND "${begin_source}" "sv_client->state = cs_spawned;" begin_spawned)
string(FIND "${begin_source}" "if (!restoring_qcx_client && !waiting_qcx_client && !sv.loadgame)" begin_callbacks)
string(FIND "${begin_source}" "SV_SpawnSpectator ();" spectator_spawn)
string(FIND "${begin_source}" "PR_GameClientConnect(sv_client->spectator);" client_connect)
string(FIND "${begin_source}" "PR_GamePutClientInServer(sv_client->spectator);" put_in_server)
if(begin_commit EQUAL -1 OR spectator_spawn EQUAL -1 OR client_connect EQUAL -1
	OR put_in_server EQUAL -1 OR NOT begin_wait_guard LESS begin_pending
	OR NOT begin_pending LESS begin_commit OR NOT begin_commit LESS begin_spawned
	OR NOT begin_spawned LESS begin_callbacks OR NOT begin_callbacks LESS spectator_spawn
	OR NOT spectator_spawn LESS client_connect OR NOT client_connect LESS put_in_server)
	message(FATAL_ERROR "Cmd_Begin_f must commit saved identity before marking spawned or running ordinary gameplay callbacks")
endif()
string(FIND "${sv_user}" "QCX_RestoreSessionBegin(" old_begin)
if(NOT old_begin EQUAL -1)
	message(FATAL_ERROR "Cmd_Begin_f must not retain the old restore begin path")
endif()

# Execute the real Cmd_Begin_f body with narrow server/VM boundary doubles.
# Session tests exercise CommitBegin itself; this harness proves its returned
# spawned flag and committed identity reach the ordinary gameplay callbacks.
set(begin_harness [=[
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define QCX_ENABLED 1
#define NUM_SPAWN_PARMS 16
#define PRINT_HIGH 2
#define SV_PAUSE_MANUAL 1
#define svc_setpause 24
#define svc_setangle 10
#define OFS_PARM0 0
typedef int qbool;
enum { false, true, cs_connected, cs_spawned };
typedef struct { int unused; } ctxinfo_t;
typedef struct { float v_angle[3]; } entvars_t;
typedef struct { entvars_t *v; } edict_t;
typedef struct {
	int state, spectator, vip;
	float spawn_parms[NUM_SPAWN_PARMS];
	struct { int frame_latency, frame_rate, drop_count, good_count, message; } netchan;
	ctxinfo_t _userinfo_ctx_;
	char name[32], team[32];
	double lastservertimeupdate;
} client_t;
static struct { client_t clients[1]; int spawncount; } svs;
static struct {
	int loadgame, paused;
	unsigned model_newplayer_checksum, model_player_checksum, eyes_player_checksum;
	float time;
} sv;
static client_t *sv_client = svs.clients;
static entvars_t vars;
static edict_t entity = { &vars }, *sv_player = &entity;
static struct { float time, self, parms[NUM_SPAWN_PARMS]; } globals;
#define pr_global_struct (&globals)
#define PR_GLOBAL(name) globals.parms[0]
static float arguments[1];
#define G_FLOAT(index) arguments[index]
#define EDICT_TO_PROG(ent) ((void)(ent), 1)
#define EDICT_NUM(index) ((void)(index), &entity)
static int pending, waiting, saved_spectator, saved_spawned, commit_allowed;
static int commits, connects, spawns, spectator_spawns, drops;
static qbool QCX_RestoreSessionClientWaiting(client_t *client) { (void)client; return waiting; }
static qbool QCX_RestoreSessionClientPending(client_t *client) { (void)client; return pending; }
static qbool QCX_RestoreSessionCommitBegin(client_t *client, qbool *spawned)
{
	int i;
	++commits;
	assert(client->state == cs_connected);
	assert(connects == 0 && spawns == 0 && spectator_spawns == 0);
	if (!commit_allowed) return false;
	client->spectator = saved_spectator;
	strcpy(client->team, "red");
	for (i = 0; i < NUM_SPAWN_PARMS; ++i) client->spawn_parms[i] = 200.0f + i;
	*spawned = saved_spawned;
	pending = waiting = false;
	return true;
}
static void assert_committed_identity(int spectator)
{
	int i;
	assert(commits == 1 && spectator == saved_spectator);
	assert(strcmp(sv_client->team, "red") == 0);
	for (i = 0; i < NUM_SPAWN_PARMS; ++i) assert(globals.parms[i] == 200.0f + i);
}
static void SV_SpawnSpectator(void) { assert(commits == 1); ++spectator_spawns; }
static void PR_GameClientConnect(int spectator) { assert_committed_identity(spectator); ++connects; }
static void PR_GamePutClientInServer(int spectator) { assert_committed_identity(spectator); ++spawns; }
static void SV_DropClient(client_t *client) { (void)client; ++drops; }
static char *Cmd_Argv(int index) { (void)index; return "1"; }
static int Q_atoi(const char *value) { return atoi(value); }
static void SV_ClearReliable(client_t *client) { (void)client; }
static void Con_Printf(char *format, ...) { (void)format; }
static void Cmd_New_f(void) { assert(0); }
static char *Info_Get(ctxinfo_t *context, const char *key) { (void)context; (void)key; return "1"; }
static void SV_BroadcastPrintf(int level, char *format, ...) { (void)level; (void)format; }
static void SV_ClientPrintf(client_t *client, int level, char *format, ...)
{ (void)client; (void)level; (void)format; }
static void ClientReliableWrite_Begin(client_t *client, int command, int size)
{ (void)client; (void)command; (void)size; }
static void ClientReliableWrite_Byte(client_t *client, int value) { (void)client; (void)value; }
static void MSG_WriteByte(int *message, int value) { (void)message; (void)value; }
static void MSG_WriteAngle(int *message, float value) { (void)message; (void)value; }
]=])
string(APPEND begin_harness "${begin_source}")
string(APPEND begin_harness [=[
static void reset_begin(int spectator, int spawned)
{
	memset(&svs, 0, sizeof(svs));
	memset(&sv, 0, sizeof(sv));
	memset(&globals, 0, sizeof(globals));
	svs.spawncount = 1;
	sv_client->state = cs_connected;
	sv_client->spectator = !spectator;
	strcpy(sv_client->team, "blue");
	sv.model_newplayer_checksum = sv.eyes_player_checksum = 1;
	saved_spectator = spectator;
	saved_spawned = spawned;
	pending = commit_allowed = true;
	waiting = commits = connects = spawns = spectator_spawns = drops = 0;
}
int main(void)
{
	int role, spawned;
	for (role = 0; role <= 1; ++role) {
		for (spawned = 0; spawned <= 1; ++spawned) {
			reset_begin(role, spawned);
			Cmd_Begin_f();
			assert(commits == 1 && drops == 0);
			assert(sv_client->state == cs_spawned);
			assert(sv_client->spectator == role);
			assert(connects == !spawned && spawns == !spawned);
			assert(spectator_spawns == (role && !spawned));
			assert(sv_client->lastservertimeupdate == -99);
		}
	}
	reset_begin(false, false);
	waiting = true;
	Cmd_Begin_f();
	assert(commits == 0 && connects == 0 && spawns == 0);
	assert(sv_client->state == cs_connected);
	reset_begin(false, false);
	commit_allowed = false;
	Cmd_Begin_f();
	assert(commits == 1 && drops == 1 && connects == 0 && spawns == 0);
	assert(sv_client->state == cs_connected);
	return 0;
}
]=])
find_program(begin_c_compiler NAMES cc clang gcc REQUIRED)
set(begin_harness_path "${CMAKE_CURRENT_BINARY_DIR}/qcx_restore_begin_route_test")
file(WRITE "${begin_harness_path}.c" "${begin_harness}")
execute_process(COMMAND "${begin_c_compiler}" -std=c11 -Wall -Wextra -Werror
	"${begin_harness_path}.c" -o "${begin_harness_path}"
	RESULT_VARIABLE begin_compile_result OUTPUT_VARIABLE begin_compile_stdout ERROR_VARIABLE begin_compile_stderr)
if(NOT begin_compile_result EQUAL 0)
	message(FATAL_ERROR "Cmd_Begin_f harness compilation failed: ${begin_compile_stdout}${begin_compile_stderr}")
endif()
execute_process(COMMAND "${begin_harness_path}" RESULT_VARIABLE begin_run_result
	OUTPUT_VARIABLE begin_run_stdout ERROR_VARIABLE begin_run_stderr)
if(NOT begin_run_result EQUAL 0)
	message(FATAL_ERROR "Cmd_Begin_f callback contract failed: ${begin_run_stdout}${begin_run_stderr}")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_main.c" sv_main)
foreach(required IN ITEMS
	"QCX_RestoreSessionAdmissionRole("
	"SV_EvaluateRoleAdmission("
	"qcx_restore_fallback_allowed"
	"QCX_RestoreSessionClientDropped(drop)"
	"!qcx_restore_identity")
	string(FIND "${sv_main}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "src/sv_main.c: missing restored-client admission/drop route ${required}")
	endif()
endforeach()

string(FIND "${sv_main}" "QCX_RestoreSessionAdmissionRole(" admission_role)
string(FIND "${sv_main}" "qcx_saved_auth = SV_EvaluateRoleAdmission(userinfo, qcx_saved_spectator)" saved_role_auth)
string(FIND "${sv_main}" "QCX_RestoreSessionAdmissionSlot(Info_ValueForKey(userinfo, \"name\"))" admission_slot)
if(admission_role EQUAL -1 OR saved_role_auth EQUAL -1 OR admission_slot EQUAL -1
	OR NOT admission_role LESS saved_role_auth OR NOT saved_role_auth LESS admission_slot)
	message(FATAL_ERROR
		"SVC_DirectConnect must evaluate the saved role before roster admission")
endif()

foreach(required IN ITEMS
	"SV_FindReconnectingClient(net_from, qport)"
	"qcx_requested_auth = SV_EvaluateRoleAdmission(userinfo,"
	"newcl->qcx_restore_fallback_allowed = qcx_requested_auth.allowed")
	string(FIND "${sv_main}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "SVC_DirectConnect is missing independent role admission ${required}")
	endif()
endforeach()

string(FIND "${sv_main}" "QCX_RestoreSessionRememberAdmissionIdentity(newcl," remember_identity)
if(NOT remember_identity EQUAL -1)
	message(FATAL_ERROR
		"SVC_DirectConnect must not retain a rollback snapshot for restore admission")
endif()

string(FIND "${sv_main}" "spectator = qcx_saved_spectator" saved_role_overwrite)
string(FIND "${sv_main}" "SV_NormalizeRoleUserinfo(userinfo, sizeof(userinfo)," requested_role_normalize)
string(FIND "${sv_main}" "newcl->spectator = spectator" connection_role)
if(NOT saved_role_overwrite EQUAL -1 OR requested_role_normalize EQUAL -1
	OR connection_role EQUAL -1 OR NOT requested_role_normalize LESS connection_role)
	message(FATAL_ERROR
		"SVC_DirectConnect must retain requested role after saved-role admission")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/pr2_exec.c" pr2_exec)
string(FIND "${pr2_exec}" "pr2_save_result_t PR2_LoadGame(" load_begin)
if(load_begin EQUAL -1)
	message(FATAL_ERROR "could not locate PR2_LoadGame")
endif()
string(SUBSTRING "${pr2_exec}" ${load_begin} -1 load_tail)
string(FIND "${load_tail}" "\n}\n" load_end)
if(load_end EQUAL -1)
	message(FATAL_ERROR "could not isolate PR2_LoadGame")
endif()
string(SUBSTRING "${load_tail}" 0 ${load_end} pr2_load)
foreach(forbidden IN ITEMS "QCX_LoadGame(" "PR2_SAVE_COMPLETE")
	string(FIND "${pr2_load}" "${forbidden}" position)
	if(NOT position EQUAL -1)
		message(FATAL_ERROR "PR2_LoadGame must not retain the in-place QCX route: ${forbidden}")
	endif()
endforeach()
string(FIND "${pr2_load}" "QCX_Active()" position)
if(NOT position EQUAL -1)
	message(FATAL_ERROR "PR2_LoadGame must prepare a QCMS load before QCX is active")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/qcx/save.c" save_source)
foreach(forbidden IN ITEMS
	"qcx_connected_snapshot"
	"QCX_SaveFingerprint"
	"QCX_LoadGame(")
	string(FIND "${save_source}" "${forbidden}" position)
	if(NOT position EQUAL -1)
		message(FATAL_ERROR "src/qcx/save.c must remove the connected-snapshot path: ${forbidden}")
	endif()
endforeach()
foreach(required IN ITEMS
	"QCX_RestoreSessionBlocksSave()"
	"QCX_HasPreparedLoadGame()"
	"QCX_RestoreSessionInstall(qcx_prepared_image, Sys_DoubleTime())")
	string(FIND "${save_source}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "src/qcx/save.c missing fresh-restore route ${required}")
	endif()
endforeach()

foreach(required IN ITEMS
	"QCMS V1 saves are unsupported"
	"QCX_SaveIsV1Image")
	string(FIND "${save_source}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "src/qcx/save.c missing QCMS V1 diagnostic: ${required}")
	endif()
endforeach()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_save.c" sv_save)
string(FIND "${sv_save}" "SV_SpawnServer(mapname, false, NULL, false, true)" fresh_spawn)
if(fresh_spawn EQUAL -1)
	message(FATAL_ERROR "SV_LoadGame_f must fresh-spawn every prepared QCMS save")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_init.c" sv_init)
foreach(required IN ITEMS
	"QCX_RestoreSessionCancel();"
	"if (!restoring_qc2cpp) QCX_DiscardPreparedLoadGame();"
	"preserved_pause_reasons"
	"SV_PAUSE_MANUAL"
	"qc2cpp restore could not load saved map")
	string(FIND "${sv_init}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "SV_SpawnServer missing fresh-restore lifecycle route: ${required}")
	endif()
endforeach()
string(FIND "${sv_init}" "memset (&sv, 0, sizeof(sv));" server_wipe)
string(FIND "${sv_init}" "sv.paused = preserved_pause_reasons;" restored_manual_pause)
if(server_wipe EQUAL -1 OR restored_manual_pause EQUAL -1
	OR NOT server_wipe LESS restored_manual_pause)
	message(FATAL_ERROR
		"SV_SpawnServer must preserve manual pause after wiping the per-level server state")
endif()

string(FIND "${begin_source}" "QCX_RestoreSessionCommitBegin(sv_client, &restoring_qcx_client)" restored_begin)
string(FIND "${begin_source}" "if (pending_qcx_client && (sv.paused & SV_PAUSE_MANUAL))" restored_pause)
if(restored_begin EQUAL -1 OR restored_pause EQUAL -1 OR NOT restored_begin LESS restored_pause)
	message(FATAL_ERROR
		"Cmd_Begin_f must send a surviving pause only after the restored client completes begin")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_main.c" sv_main_lifecycle)
foreach(required IN ITEMS
	"#include \"qcx/save.h\""
	"QCX_DiscardPreparedLoadGame();"
	"SV_PAUSE_MANUAL")
	string(FIND "${sv_main_lifecycle}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "src/sv_main.c missing restore lifecycle or pause route: ${required}")
	endif()
endforeach()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_user.c" sv_user_pause)
foreach(required IN ITEMS "void SV_SetPauseReason" "SV_PAUSE_MANUAL")
	string(FIND "${sv_user_pause}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "src/sv_user.c missing pause-reason route: ${required}")
	endif()
endforeach()
