#include "qwsvdef.h"

#include "qcx/adapter.h"
#include "qcx/test_observer.h"
#include "qcx/entities.h"
#include "qcx/globals.h"
#include "qcx/restore_session.h"

#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct qcx_test_observer_s {
	uint32_t frame_count;
	uint32_t init_count;
	uint32_t normal_unpublish_count;
	uint32_t legacy_game_entries;
	uint32_t gameplay_during_init;
	uint32_t server_state_bound_count;
	uint32_t gameplay_before_server_state;
	uint32_t initializing;
	uint32_t client_connect_count;
	int32_t last_client_userid;
	uint32_t put_client_in_server_count;
	uint32_t spectator_put_client_in_server_count;
	uint32_t client_disconnect_count;
	uint32_t client_userinfo_before_count;
	uint32_t client_userinfo_after_count;
	uint32_t client_command_count;
	uint32_t client_kill_count;
	int32_t player_health_after_kill;
	int32_t player_frags_after_kill;
	float player_origin_at_spawn[3];
	uint32_t player_moved_after_spawn;
	uint32_t client_prethink_count;
	uint32_t client_postthink_count;
	uint32_t spectator_think_count;
	uint32_t teledeath_owner;
	uint32_t teledeath_toucher;
	uint32_t shared_self_after_put_client;
	uint32_t restore_replication_begin_count;
	uint32_t restore_replication_complete_count;
	uint32_t save_probe_slot;
	uint32_t save_probe_trigger_dispatches;
	uint32_t qwsp_monster_slot;
	uint32_t qwsp_monster_think_dispatches;
	float qwsp_monster_last_think_time;
} qcx_test_observer_t;

static qcx_test_observer_t observer;
static int forced_next_client_userid = -1;
extern int sv_playermodel;
static void QCX_TestReuseUserId_f(void);
static void QCX_TestSaveState_f(void);
static void QCX_TestReleaseConnectedClient_f(void);
static void QCX_TestFatal_f(void);
static void QCX_TestRestoreOom_f(void);
static void QCX_TestEntityReferences_f(void);
static void QCX_TestOptionalFields_f(void);
static void QCX_TestLegacyStrings_f(void);
static void QCX_TestModelPresence_f(void);
static void QCX_TestQwspMonster_f(void);
static void QCX_TestQwspSave_f(void);
static void QCX_TestRestoreSession_f(void);
static void QCX_TestStuffClient_f(void);
static void QCX_TestReleaseClient_f(void);
static void QCX_TestObserverSendRestoreMarker(const char *marker);

static void QCX_TestSnapshot_f(void)
{
	Con_Printf("{\"qc2cpp_test_snapshot\":{\"map\":\"%s\",\"frame_count\":%u,\"time\":%.6f,\"globals_address\":%" PRIuPTR ",\"player_model_index\":%d}}\n",
		sv.mapname, observer.frame_count, sv.time, (uintptr_t)QCX_Globals(), sv_playermodel);
}

static void QCX_TestEvents_f(void)
{
	Con_Printf("{\"qc2cpp_test_events\":{\"normal_unpublish_count\":%u,"
		"\"legacy_game_entries\":%u,\"gameplay_during_init\":%u,\"init_count\":%u,"
		"\"server_state_bound_count\":%u,\"gameplay_before_server_state\":%u,"
		"\"client_connect_count\":%u,\"put_client_in_server_count\":%u,"
		"\"last_client_userid\":%d,"
		"\"spectator_put_client_in_server_count\":%u,"
		"\"client_disconnect_count\":%u,"
		"\"client_userinfo_before_count\":%u,"
		"\"client_userinfo_after_count\":%u,\"client_command_count\":%u,"
		"\"client_kill_count\":%u,"
		"\"player_health_after_kill\":%d,"
		"\"player_frags_after_kill\":%d,"
		"\"player_moved_after_spawn\":%u,"
		"\"client_prethink_count\":%u,\"client_postthink_count\":%u,"
		"\"spectator_think_count\":%u,"
		"\"teledeath_owner\":%u,\"teledeath_toucher\":%u,"
		"\"shared_self_after_put_client\":%u,"
		"\"restore_replication_begin_count\":%u,"
		"\"restore_replication_complete_count\":%u}}\n",
		observer.normal_unpublish_count, observer.legacy_game_entries,
		observer.gameplay_during_init, observer.init_count,
		observer.server_state_bound_count, observer.gameplay_before_server_state,
		observer.client_connect_count,
		observer.put_client_in_server_count, observer.last_client_userid,
		observer.spectator_put_client_in_server_count,
		observer.client_disconnect_count,
		observer.client_userinfo_before_count,
		observer.client_userinfo_after_count,
		observer.client_command_count,
		observer.client_kill_count, observer.player_health_after_kill,
		observer.player_frags_after_kill,
		observer.player_moved_after_spawn,
		observer.client_prethink_count,
		observer.client_postthink_count, observer.spectator_think_count,
		observer.teledeath_owner,
		observer.teledeath_toucher, observer.shared_self_after_put_client,
		observer.restore_replication_begin_count,
		observer.restore_replication_complete_count);
}

static qbool QCX_TestJsonAppend(char **cursor, size_t *remaining, const char *format, ...)
{
	va_list arguments;
	int written;
	if (*remaining == 0U) return false;
	va_start(arguments, format);
	written = vsnprintf(*cursor, *remaining, format, arguments);
	va_end(arguments);
	if (written < 0 || (size_t)written >= *remaining) return false;
	*cursor += written;
	*remaining -= (size_t)written;
	return true;
}

static qbool QCX_TestJsonString(char **cursor, size_t *remaining, const char *value)
{
	const unsigned char *input = (const unsigned char *)(value == NULL ? "" : value);
	if (!QCX_TestJsonAppend(cursor, remaining, "\"")) return false;
	while (*input != '\0') {
		if (*input == '\\' || *input == '\"') {
			if (!QCX_TestJsonAppend(cursor, remaining, "\\%c", *input)) return false;
		} else if (*input < 0x20U || *input >= 0x80U) {
			if (!QCX_TestJsonAppend(cursor, remaining, "?")) return false;
		} else if (!QCX_TestJsonAppend(cursor, remaining, "%c", *input)) {
			return false;
		}
		++input;
	}
	return QCX_TestJsonAppend(cursor, remaining, "\"");
}

static void QCX_TestRestoreSession_f(void)
{
	qcx_restore_session_status_t status;
	char json[8192];
	char *cursor = json;
	size_t remaining = sizeof(json);
	int slot;
	qbool first = true;
	QCX_RestoreSessionGetStatus(&status, Sys_DoubleTime());
	if (!QCX_TestJsonAppend(&cursor, &remaining,
		"{\"qc2cpp_test_restore_session\":{\"waiting\":%s,"
		"\"paused\":%d,\"remaining_seconds\":%.3f,\"available\":%u,"
		"\"bound\":%u,\"active\":%u,\"clients\":[",
		status.waiting ? "true" : "false", sv.paused, status.remaining_seconds,
		status.available_count, status.bound_count, status.active_count)) goto overflow;
	for (slot = 0; slot < MAX_CLIENTS; ++slot) {
		client_t *const client = &svs.clients[slot];
		int edict_slot = -1;
		if (client->state != cs_preconnected && client->state != cs_connected
			&& client->state != cs_spawned) continue;
		if (!first && !QCX_TestJsonAppend(&cursor, &remaining, ",")) goto overflow;
		first = false;
		if (client->edict != NULL) edict_slot = (int)(client->edict - sv.edicts);
		if (!QCX_TestJsonAppend(&cursor, &remaining, "{\"slot\":%d,\"name\":", slot)
			|| !QCX_TestJsonString(&cursor, &remaining, client->name)
			|| !QCX_TestJsonAppend(&cursor, &remaining, ",\"spectator\":%s,\"team\":",
				client->spectator ? "true" : "false")
			|| !QCX_TestJsonString(&cursor, &remaining, client->team)
			|| !QCX_TestJsonAppend(&cursor, &remaining,
				",\"userinfo_spectator\":")
			|| !QCX_TestJsonString(&cursor, &remaining,
				Info_Get(&client->_userinfo_ctx_, "*spectator"))
			|| !QCX_TestJsonAppend(&cursor, &remaining, ",\"userinfo_team\":")
			|| !QCX_TestJsonString(&cursor, &remaining,
				Info_Get(&client->_userinfo_ctx_, "team"))
			|| !QCX_TestJsonAppend(&cursor, &remaining, ",\"wire_spectator\":")
			|| !QCX_TestJsonString(&cursor, &remaining,
				Info_Get(&client->_userinfoshort_ctx_, "*spectator"))
			|| !QCX_TestJsonAppend(&cursor, &remaining, ",\"wire_team\":")
			|| !QCX_TestJsonString(&cursor, &remaining,
				Info_Get(&client->_userinfoshort_ctx_, "team"))
			|| !QCX_TestJsonAppend(&cursor, &remaining,
				",\"waiting\":%s,\"pending\":%s,\"edict_slot\":%d,"
			"\"userid\":%d,\"netchan_qport\":%d,\"netchan_remote\":",
			QCX_RestoreSessionClientWaiting(client) ? "true" : "false",
			QCX_RestoreSessionClientPending(client) ? "true" : "false",
			edict_slot, client->userid, client->netchan.qport)
			|| !QCX_TestJsonString(&cursor, &remaining,
				NET_AdrToString(client->netchan.remote_address))
			|| !QCX_TestJsonAppend(&cursor, &remaining, ",\"spawn_parms\":[")) goto overflow;
		for (int parm = 0; parm < NUM_SPAWN_PARMS; ++parm) {
			if ((parm != 0 && !QCX_TestJsonAppend(&cursor, &remaining, ","))
				|| !QCX_TestJsonAppend(&cursor, &remaining, "%.9g",
					client->spawn_parms[parm])) goto overflow;
		}
		if (!QCX_TestJsonAppend(&cursor, &remaining, "]}")) goto overflow;
	}
	if (!QCX_TestJsonAppend(&cursor, &remaining, "]}}")) goto overflow;
	Con_Printf("%s\n", json);
	return;

overflow:
	Con_Printf("{\"qc2cpp_test_restore_session\":{\"error\":\"overflow\"}}\n");
}

static client_t *QCX_TestClientForSlot(const char *command)
{
	const int slot = Q_atoi(Cmd_Argv(1));
	if (Cmd_Argc() < 2 || slot < 0 || slot >= MAX_CLIENTS) {
		Con_Printf("Usage: %s <live client slot>\n", command);
		return NULL;
	}
	if (svs.clients[slot].state != cs_preconnected
		&& svs.clients[slot].state != cs_connected
		&& svs.clients[slot].state != cs_spawned) {
		Con_Printf("qc2cpp test client slot %d is not live\n", slot);
		return NULL;
	}
	return &svs.clients[slot];
}

static void QCX_TestStuffClient_f(void)
{
	client_t *client;
	char command[256];
	if (Cmd_Argc() != 3) {
		Con_Printf("Usage: qc2cpp_test_stuff_client <live client slot> <command>\n");
		return;
	}
	client = QCX_TestClientForSlot("qc2cpp_test_stuff_client");
	if (client == NULL) return;
	if (strchr(Cmd_Argv(2), '\n') != NULL || strlen(Cmd_Argv(2)) >= sizeof(command) - 1U) {
		Con_Printf("qc2cpp test client command is invalid or too long\n");
		return;
	}
	snprintf(command, sizeof(command), "%s\n", Cmd_Argv(2));
	ClientReliableWrite_Begin(client, svc_stufftext, (int)strlen(command) + 2);
	ClientReliableWrite_String(client, command);
	Con_Printf("{\"qc2cpp_test_stuff_client\":{\"slot\":%d}}\n",
		(int)(client - svs.clients));
}

static void QCX_TestReleaseClient_f(void)
{
	client_t *const client = QCX_TestClientForSlot("qc2cpp_test_release_client");
	if (client == NULL) return;
	Con_Printf("{\"qc2cpp_test_release_client\":{\"slot\":%d}}\n",
		(int)(client - svs.clients));
	SV_DropClient(client);
}

void QCX_TestObserverRegisterCommands(void)
{
	Cmd_AddCommand("qc2cpp_test_snapshot", QCX_TestSnapshot_f);
	Cmd_AddCommand("qc2cpp_test_events", QCX_TestEvents_f);
	Cmd_AddCommand("qc2cpp_test_reuse_userid", QCX_TestReuseUserId_f);
	Cmd_AddCommand("qc2cpp_test_save_state", QCX_TestSaveState_f);
	Cmd_AddCommand("qc2cpp_test_release_connected_client",
		QCX_TestReleaseConnectedClient_f);
	Cmd_AddCommand("qc2cpp_test_fatal", QCX_TestFatal_f);
	Cmd_AddCommand("qc2cpp_test_restore_oom", QCX_TestRestoreOom_f);
	Cmd_AddCommand("qc2cpp_test_entity_references", QCX_TestEntityReferences_f);
	Cmd_AddCommand("qc2cpp_test_optional_fields", QCX_TestOptionalFields_f);
	Cmd_AddCommand("qc2cpp_test_legacy_strings", QCX_TestLegacyStrings_f);
	Cmd_AddCommand("qc2cpp_test_model_presence", QCX_TestModelPresence_f);
	Cmd_AddCommand("qc2cpp_test_qwsp_monster", QCX_TestQwspMonster_f);
	Cmd_AddCommand("qc2cpp_test_qwsp_save", QCX_TestQwspSave_f);
	Cmd_AddCommand("qc2cpp_test_restore_session", QCX_TestRestoreSession_f);
	Cmd_AddCommand("qc2cpp_test_stuff_client", QCX_TestStuffClient_f);
	Cmd_AddCommand("qc2cpp_test_release_client", QCX_TestReleaseClient_f);
}

void QCX_TestObserverInitBegin(void)
{
	++observer.init_count;
	++observer.initializing;
}

void QCX_TestObserverInitEnd(void)
{
	if (observer.initializing != 0U) {
		--observer.initializing;
	}
}

void QCX_TestObserverGameplayImport(void)
{
	if (observer.initializing != 0U) {
		++observer.gameplay_during_init;
	}
}

void QCX_TestObserverServerStateBound(void)
{
	++observer.server_state_bound_count;
}

void QCX_TestObserverStartFrame(void)
{
	++observer.frame_count;
	if (observer.initializing != 0U) {
		++observer.gameplay_during_init;
	}
	if (observer.server_state_bound_count < observer.init_count) {
		++observer.gameplay_before_server_state;
	}
}

void QCX_TestObserverNormalUnpublish(void)
{
	++observer.normal_unpublish_count;
}

void QCX_TestObserverTerminalUnpublish(void)
{
	Con_Printf("[qc2cpp-fatal] terminal-unpublish\n");
}

void QCX_TestObserverLegacyGameEntry(void)
{
	++observer.legacy_game_entries;
}

static void QCX_TestReuseUserId_f(void)
{
	if (Cmd_Argc() != 2) {
		Con_Printf("Usage: qc2cpp_test_reuse_userid <positive id>\n");
		return;
	}
	const int requested = Q_atoi(Cmd_Argv(1));
	if (requested <= 0) {
		Con_Printf("qc2cpp test userid must be positive\n");
		return;
	}
	forced_next_client_userid = requested;
	Con_Printf("{\"qc2cpp_test_userid\":{\"userid\":%d}}\n", requested);
}

static int QCX_TestFindSaveProbe(void)
{
	int slot;
	for (slot = 1; slot < sv.num_edicts; ++slot) {
		if (!sv.edicts[slot].e.free && sv.edicts[slot].v != NULL
			&& sv.edicts[slot].v->think != 0) return slot;
	}
	return -1;
}

static void QCX_TestReportSaveState(void)
{
	char message[64] = "";
	uint32_t probe_slot = observer.save_probe_slot;
	int32_t probe_think = 0;
	float probe_nextthink = 0.0f;
	if (probe_slot == 0U) {
		const int discovered = QCX_TestFindSaveProbe();
		if (discovered > 0) probe_slot = (uint32_t)discovered;
	}
	if (sv.num_edicts > 0 && sv.edicts[0].v != NULL) {
		(void)QCX_CopyEntityString(&sv.edicts[0], "message", message,
			sizeof(message), NULL);
	}
	if (probe_slot < (uint32_t)sv.num_edicts && sv.edicts[probe_slot].v != NULL) {
		probe_think = sv.edicts[probe_slot].v->think;
		probe_nextthink = sv.edicts[probe_slot].v->nextthink;
	}
	Con_Printf("{\"qc2cpp_test_save_state\":{\"parm16\":%.1f,\"world_health\":%.1f,"
		"\"world_message\":\"%s\",\"probe_slot\":%u,\"probe_think\":%d,"
		"\"probe_nextthink\":%.6f,\"probe_trigger_dispatches\":%u}}\n",
		QCX_Globals() == NULL ? 0.0f : QCX_Globals()->parm16,
		sv.num_edicts > 0 && sv.edicts[0].v != NULL ? sv.edicts[0].v->health : 0.0f,
		message, probe_slot, probe_think, probe_nextthink,
		observer.save_probe_trigger_dispatches);
}

static void QCX_TestSaveState_f(void)
{
	const char *mode;
	if (Cmd_Argc() != 2) {
		Con_Printf("Usage: qc2cpp_test_save_state <save|mutate|read|trigger>\n");
		return;
	}
	mode = Cmd_Argv(1);
	if (QCX_Globals() == NULL || sv.num_edicts <= 0 || sv.edicts[0].v == NULL) {
		Con_Printf("qc2cpp save-state probe is unavailable\n");
		return;
	}
	if (!strcmp(mode, "save")) {
		const int slot = QCX_TestFindSaveProbe();
		if (slot < 0) {
			Con_Printf("qc2cpp save-state probe has no callback owner\n");
			return;
		}
		observer.save_probe_slot = (uint32_t)slot;
		QCX_Globals()->parm16 = 101.0f;
		sv.edicts[0].v->health = 101.0f;
		if (!QCX_SetEntityString(&sv.edicts[0], "message", "qcms-saved")) {
			Con_Printf("qc2cpp save-state probe could not write string\n");
			return;
		}
	} else if (!strcmp(mode, "mutate")) {
		if (observer.save_probe_slot >= (uint32_t)sv.num_edicts
			|| sv.edicts[observer.save_probe_slot].v == NULL) {
			Con_Printf("qc2cpp save-state probe has no saved callback owner\n");
			return;
		}
		QCX_Globals()->parm16 = 202.0f;
		sv.edicts[0].v->health = 202.0f;
		sv.edicts[observer.save_probe_slot].v->think = 0;
		sv.edicts[observer.save_probe_slot].v->nextthink = 0.0f;
		if (!QCX_SetEntityString(&sv.edicts[0], "message", "qcms-mutated")) {
			Con_Printf("qc2cpp save-state probe could not mutate string\n");
			return;
		}
	} else if (!strcmp(mode, "trigger")) {
		int slot = (int)observer.save_probe_slot;
		if (slot <= 0 || slot >= sv.num_edicts || sv.edicts[slot].v == NULL
			|| sv.edicts[slot].v->think == 0) {
			slot = QCX_TestFindSaveProbe();
		}
		if (slot <= 0) {
			Con_Printf("qc2cpp save-state probe has no callback to trigger\n");
			return;
		}
		observer.save_probe_slot = (uint32_t)slot;
		/* Exercise the restored symbolic callback through the normal engine
		 * scheduler boundary, rather than merely inspecting its slot number. */
		sv.edicts[slot].v->nextthink = (float)sv.time;
		observer.save_probe_trigger_dispatches = 0U;
		(void)SV_RunThink(&sv.edicts[slot]);
	} else if (strcmp(mode, "read")) {
		Con_Printf("Usage: qc2cpp_test_save_state <save|mutate|read|trigger>\n");
		return;
	}
	QCX_TestReportSaveState();
}

static void QCX_TestReleaseConnectedClient_f(void)
{
	if (Cmd_Argc() != 1) {
		Con_Printf("Usage: qc2cpp_test_release_connected_client\n");
		return;
	}
	QCX_TestObserverSendRestoreMarker("[qc2cpp-save-connected] release");
	Con_Printf("{\"qc2cpp_test_release_connected_client\":{\"released\":true}}\n");
}

static void QCX_TestFatal_f(void)
{
	int slot;
	if (Cmd_Argc() != 1) {
		Con_Printf("Usage: qc2cpp_test_fatal\n");
		return;
	}
	for (slot = 1; slot < sv.num_edicts; ++slot) {
		if (!sv.edicts[slot].e.free && sv.edicts[slot].v != NULL
			&& sv.edicts[slot].v->think != 0) {
			break;
		}
	}
	if (slot == sv.num_edicts) {
		Con_Printf("qc2cpp fatal probe has no think callback\n");
		return;
	}
	Con_Printf("[qc2cpp-fatal] trigger\n");
	sv.edicts[slot].v->nextthink = (float)sv.time;
	(void)SV_RunThink(&sv.edicts[slot]);
	Con_Printf("[qc2cpp-fatal] outer-gameplay-resumed\n");
}

static void QCX_TestRestoreOom_f(void)
{
	enum { restore_oom_bytes = 64U * 1024U };
	char *value;
	if (Cmd_Argc() != 1) {
		Con_Printf("Usage: qc2cpp_test_restore_oom\n");
		return;
	}
	if (sv.num_edicts <= 0 || sv.edicts[0].v == NULL) {
		Con_Printf("qc2cpp restore-oom probe is unavailable\n");
		return;
	}
	value = malloc((size_t)restore_oom_bytes + 1U);
	if (value == NULL) {
		Con_Printf("qc2cpp restore-oom probe could not allocate input\n");
		return;
	}
	memset(value, 'x', restore_oom_bytes);
	value[restore_oom_bytes] = '\0';
	if (!QCX_SetEntityString(&sv.edicts[0], "message", value)) {
		free(value);
		Con_Printf("qc2cpp restore-oom probe could not write world string\n");
		return;
	}
	free(value);
	Con_Printf("{\"qc2cpp_test_restore_oom\":{\"prepared\":true}}\n");
}

static void QCX_TestEntityReferences_f(void)
{
	if (!QCX_Active() || sv.max_edicts < 8) {
		Con_Printf("{\"qc2cpp_test_entity_references\":{\"ready\":false}}\n");
		return;
	}
	edict_t *const subject = &sv.edicts[3];
	subject->v->owner = 4;
	subject->v->enemy = 5;
	subject->v->groundentity = 6;
	subject->v->dmg_inflictor = 7;
	PR_GLOBAL(newmis) = 7;
	Con_Printf("{\"qc2cpp_test_entity_references\":{\"ready\":true,"
		"\"owner\":%d,\"enemy\":%d,\"groundentity\":%d,"
		"\"dmg_inflictor\":%d,\"newmis\":%d}}\n",
		NUM_FOR_EDICT(PROG_TO_EDICT(subject->v->owner)),
		NUM_FOR_EDICT(PROG_TO_EDICT(subject->v->enemy)),
		NUM_FOR_EDICT(PROG_TO_EDICT(subject->v->groundentity)),
		NUM_FOR_EDICT(PROG_TO_EDICT(subject->v->dmg_inflictor)),
		NUM_FOR_EDICT(PROG_TO_EDICT(PR_GLOBAL(newmis))));
	PR_GLOBAL(newmis) = 0;
}

static void QCX_TestOptionalFields_f(void)
{
	if (!QCX_Active() || sv.max_edicts < 5 || fofs_items2 == 0 || fofs_maxspeed == 0
		|| fofs_gravity == 0 || fofs_movement == 0 || fofs_vw_index == 0
		|| fofs_hideentity == 0 || fofs_trackent == 0 || fofs_visibility == 0
		|| fofs_hide_players == 0 || fofs_teleported == 0) {
		Con_Printf("{\"qc2cpp_test_optional_fields\":{\"ready\":false}}\n");
		return;
	}
	edict_t *const subject = &sv.edicts[3];
	((eval_t *)((byte *)subject->v + fofs_hideentity))->_int = 4;
	Con_Printf("{\"qc2cpp_test_optional_fields\":{\"ready\":true,\"hideentity\":%d}}\n",
		NUM_FOR_EDICT(PR_EntityFieldToEdict(subject, fofs_hideentity)));
}

static void QCX_TestLegacyStrings_f(void)
{
	if (!QCX_Active() || sv.num_edicts <= 0 || sv.edicts[0].v == NULL) {
		Con_Printf("{\"qc2cpp_test_legacy_strings\":{\"ready\":false}}\n");
		return;
	}
	{
		const char *const before = PR_GetEntityString(sv.edicts[0].v->message);
		if (before == NULL) {
			Con_Printf("{\"qc2cpp_test_legacy_strings\":{\"ready\":false}}\n");
			return;
		}
	}
	if (!QCX_SetEntityString(&sv.edicts[0], "message", "qcx-mutated")) {
		Con_Printf("{\"qc2cpp_test_legacy_strings\":{\"ready\":false}}\n");
		return;
	}
	const char *const after = PR_GetEntityString(sv.edicts[0].v->message);
	if (after == NULL) {
		Con_Printf("{\"qc2cpp_test_legacy_strings\":{\"ready\":false}}\n");
		return;
	}
	Con_Printf("{\"qc2cpp_test_legacy_strings\":{\"ready\":true,\"value\":\"%s\"}}\n",
		after);
}

static void QCX_TestModelPresence_f(void)
{
	qbool ready = false;
	qbool visible = false;
	qbool hidden = false;
	qbool restored = false;
	qbool static_model = false;
	edict_t *entity = NULL;

	if (!QCX_Active() || Cmd_Argc() != 1) {
		goto done;
	}
	entity = ED_Alloc();
	if (entity == NULL || entity->v == NULL) {
		goto done;
	}
	const char *const model = sv.model_precache[1];
	entity->v->modelindex = 1;
	if (model == NULL || !QCX_SetEntityString(entity, "model", model)) {
		goto done;
	}
	visible = PR_EntityHasModel(entity);
	if (!QCX_SetEntityString(entity, "model", "")) {
		goto done;
	}
	hidden = !PR_EntityHasModel(entity);
	if (!QCX_SetEntityString(entity, "model", model)) {
		goto done;
	}
	restored = PR_EntityHasModel(entity);
	const int static_before = sv.static_entity_count;
	PF2_makestatic(entity);
	entity = NULL;
	static_model = sv.static_entity_count == static_before + 1
		&& sv.static_entities[static_before].modelindex != 0;
	ready = static_model;

done:
	if (entity != NULL) {
		ED_Free(entity);
	}
	Con_Printf("{\"qc2cpp_test_model_presence\":{\"ready\":%s,"
		"\"visible\":%s,\"hidden\":%s,\"restored\":%s,\"static_model\":%s}}\n",
		ready ? "true" : "false", visible ? "true" : "false",
		hidden ? "true" : "false", restored ? "true" : "false",
		static_model ? "true" : "false");
}

static int QCX_TestFindQwspMonster(void)
{
	int slot;
	for (slot = 1; slot < sv.num_edicts; ++slot) {
		edict_t *const candidate = &sv.edicts[slot];
		char classname[64];
		if (candidate->e.free || candidate->v == NULL || candidate->v->health <= 0.0f
			|| candidate->v->think == 0 || candidate->v->nextthink <= (float)sv.time) {
			continue;
		}
		if (QCX_CopyEntityString(candidate, "classname", classname, sizeof(classname), NULL)
			!= QCX_PLUGIN_OK || strncmp(classname, "monster_", 8U) != 0) {
			continue;
		}
		return slot;
	}
	return -1;
}

static void QCX_TestCaptureQwspMonster(void)
{
	const int slot = QCX_TestFindQwspMonster();
	if (slot > 0) {
		observer.qwsp_monster_slot = (uint32_t)slot;
		observer.qwsp_monster_think_dispatches = 0U;
		observer.qwsp_monster_last_think_time = 0.0f;
	}
}

static client_t *QCX_TestFindQwspPlayer(void)
{
	int slot;
	for (slot = 0; slot < MAX_CLIENTS; ++slot) {
		client_t *const client = &svs.clients[slot];
		if (client->state == cs_spawned && !client->spectator
			&& client->edict != NULL && !client->edict->e.free
			&& client->edict->v != NULL) {
			return client;
		}
	}
	return NULL;
}

static void QCX_TestReportQwspMonster(void)
{
	const uint32_t slot = observer.qwsp_monster_slot;
	char classname[64];
	client_t *const player = QCX_TestFindQwspPlayer();
	if (slot == 0U || slot >= (uint32_t)sv.num_edicts || sv.edicts[slot].e.free
		|| sv.edicts[slot].v == NULL
		|| QCX_CopyEntityString(&sv.edicts[slot], "classname", classname,
			sizeof(classname), NULL) != QCX_PLUGIN_OK) {
		Con_Printf("{\"qc2cpp_test_qwsp_monster\":{\"ready\":false}}\n");
		return;
	}
	if (player == NULL) {
		Con_Printf("{\"qc2cpp_test_qwsp_monster\":{\"ready\":true,\"slot\":%u,"
			"\"classname\":\"%s\",\"health\":%.6f,\"think\":%d,"
			"\"nextthink\":%.6f,\"server_time\":%.6f,\"think_dispatches\":%u,"
			"\"last_think_time\":%.6f,\"player\":{\"ready\":false},"
			"\"killed_monsters\":%.6f}}\n",
			slot, classname, sv.edicts[slot].v->health, sv.edicts[slot].v->think,
			sv.edicts[slot].v->nextthink, sv.time,
			observer.qwsp_monster_think_dispatches, observer.qwsp_monster_last_think_time,
			PR_GLOBAL(killed_monsters));
		return;
	}
	Con_Printf("{\"qc2cpp_test_qwsp_monster\":{\"ready\":true,\"slot\":%u,"
		"\"classname\":\"%s\",\"health\":%.6f,\"think\":%d,"
		"\"nextthink\":%.6f,\"server_time\":%.6f,\"think_dispatches\":%u,"
		"\"last_think_time\":%.6f,\"player\":{\"ready\":true,\"slot\":%d,"
		"\"edict_slot\":%d,\"origin\":[%.6f,%.6f,%.6f],\"health\":%.6f,"
		"\"items\":%.6f,\"ammo_shells\":%.6f,\"ammo_nails\":%.6f,"
		"\"ammo_rockets\":%.6f,\"ammo_cells\":%.6f},"
		"\"killed_monsters\":%.6f}}\n",
		slot, classname, sv.edicts[slot].v->health, sv.edicts[slot].v->think,
		sv.edicts[slot].v->nextthink, sv.time,
		observer.qwsp_monster_think_dispatches, observer.qwsp_monster_last_think_time,
		(int)(player - svs.clients), NUM_FOR_EDICT(player->edict),
		player->edict->v->origin[0], player->edict->v->origin[1],
		player->edict->v->origin[2], player->edict->v->health,
		player->edict->v->items, player->edict->v->ammo_shells,
		player->edict->v->ammo_nails, player->edict->v->ammo_rockets,
		player->edict->v->ammo_cells, PR_GLOBAL(killed_monsters));
}

static void QCX_TestQwspMonster_f(void)
{
	const char *mode;
	if (Cmd_Argc() != 2) {
		Con_Printf("Usage: qc2cpp_test_qwsp_monster <capture|read>\n");
		return;
	}
	mode = Cmd_Argv(1);
	if (!strcmp(mode, "capture")) {
		QCX_TestCaptureQwspMonster();
	} else if (strcmp(mode, "read")) {
		Con_Printf("Usage: qc2cpp_test_qwsp_monster <capture|read>\n");
		return;
	}
	QCX_TestReportQwspMonster();
}

static void QCX_TestQwspSave_f(void)
{
	if (Cmd_Argc() != 2 || PR2_SaveGame(Cmd_Argv(1)) != PR2_SAVE_COMPLETE) {
		Con_Printf("{\"qc2cpp_test_qwsp_monster\":{\"ready\":false}}\n");
		return;
	}
	QCX_TestCaptureQwspMonster();
	QCX_TestReportQwspMonster();
}

void QCX_TestObserverClientConnect(uint32_t self)
{
	++observer.client_connect_count;
	if (self > 0U && self <= MAX_CLIENTS) {
		observer.last_client_userid = svs.clients[self - 1U].userid;
	}
}
void QCX_TestObserverPutClientInServer(uint32_t self, uint32_t spectator)
{
	++observer.put_client_in_server_count;
	if (self > 0U && self <= MAX_CLIENTS && forced_next_client_userid > 0) {
		svs.clients[self - 1U].userid = forced_next_client_userid;
		observer.last_client_userid = forced_next_client_userid;
		forced_next_client_userid = -1;
	}
	if (spectator != 0U) {
		++observer.spectator_put_client_in_server_count;
	}
	if (QCX_Globals() != NULL) {
		observer.shared_self_after_put_client = QCX_Globals()->self;
	}
	if (self < (uint32_t)sv.num_edicts) {
		VectorCopy(EDICT_NUM(self)->v->origin, observer.player_origin_at_spawn);
	}
}
void QCX_TestObserverClientDisconnect(void) { ++observer.client_disconnect_count; }
void QCX_TestObserverClientUserInfoChanged(uint32_t after)
{
	if (after != 0U) ++observer.client_userinfo_after_count;
	else ++observer.client_userinfo_before_count;
}
void QCX_TestObserverClientCommand(void) { ++observer.client_command_count; }
static void QCX_TestObserverSendRestoreMarker(const char *marker)
{
	int index;
	for (index = 0; index < MAX_CLIENTS; ++index) {
		client_t *const client = &svs.clients[index];
		if (client->state == cs_connected || client->state == cs_spawned) {
			/* Test markers must clear each client's configured message level;
			 * otherwise their absence says nothing about the reliable stream. */
			SV_ClientPrintf(client, PRINT_HIGH, "%s\n", marker);
		}
	}
}
void QCX_TestObserverRestoreReplicationBegin(void)
{
	++observer.restore_replication_begin_count;
	QCX_TestObserverSendRestoreMarker("[qc2cpp-save-connected] restore-begin");
}
void QCX_TestObserverRestoreReplicationComplete(void)
{
	++observer.restore_replication_complete_count;
	QCX_TestObserverSendRestoreMarker("[qc2cpp-save-connected] restore-complete");
}
void QCX_TestObserverClientKill(uint32_t self)
{
	++observer.client_kill_count;
	if (self < (uint32_t)sv.num_edicts) {
		observer.player_health_after_kill = (int32_t)EDICT_NUM(self)->v->health;
		observer.player_frags_after_kill = (int32_t)EDICT_NUM(self)->v->frags;
	}
}
void QCX_TestObserverClientPreThink(void) { ++observer.client_prethink_count; }
void QCX_TestObserverClientPostThink(uint32_t self, uint32_t spectator)
{
	int axis;
	++observer.client_postthink_count;
	if (spectator != 0U) {
		++observer.spectator_think_count;
	}
	if (self >= (uint32_t)sv.num_edicts) {
		return;
	}
	for (axis = 0; axis < 3; ++axis) {
		if (EDICT_NUM(self)->v->origin[axis] != observer.player_origin_at_spawn[axis]) {
			observer.player_moved_after_spawn = 1U;
			return;
		}
	}
}

void QCX_TestObserverEdictThink(edict_t *thinking)
{
	if (thinking == EDICT_NUM((int)observer.save_probe_slot)) {
		++observer.save_probe_trigger_dispatches;
	}
	if (thinking == EDICT_NUM((int)observer.qwsp_monster_slot)) {
		++observer.qwsp_monster_think_dispatches;
		observer.qwsp_monster_last_think_time = (float)sv.time;
	}
}

void QCX_TestObserverEdictTouch(edict_t *touched, edict_t *toucher)
{
	char classname[16];
	if (QCX_CopyEntityString(touched, "classname", classname, sizeof(classname), NULL)
		!= QCX_PLUGIN_OK || strcmp(classname, "teledeath") != 0) {
		return;
	}
	observer.teledeath_owner = (uint32_t)touched->v->owner;
	observer.teledeath_toucher = (uint32_t)toucher->e.entnum;
}
