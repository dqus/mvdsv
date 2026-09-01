#include "qwsvdef.h"

#include "qc2cpp/test_observer.h"
#include "qc2cpp/entities.h"
#include "qc2cpp/globals.h"

#include <stdint.h>
#include <inttypes.h>
#include <string.h>

typedef struct qc_test_observer_s {
	uint32_t frame_count;
	uint32_t init_count;
	uint32_t normal_unpublish_count;
	uint32_t legacy_game_entries;
	uint32_t gameplay_during_init;
	uint32_t initializing;
	uint32_t client_connect_count;
	int32_t last_client_userid;
	uint32_t put_client_in_server_count;
	uint32_t spectator_put_client_in_server_count;
	uint32_t client_disconnect_count;
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
} qc_test_observer_t;

static qc_test_observer_t observer;
static int forced_next_client_userid = -1;
static void QC_TestReuseUserId_f(void);
static void QC_TestSaveState_f(void);
static void QC_TestReleaseConnectedClient_f(void);
static void QC_TestObserverSendRestoreMarker(const char *marker);

static void QC_TestSnapshot_f(void)
{
	Con_Printf("{\"qc2cpp_test_snapshot\":{\"map\":\"%s\",\"frame_count\":%u,\"time\":%.6f,\"globals_address\":%" PRIuPTR "}}\n",
		sv.mapname, observer.frame_count, sv.time, (uintptr_t)QC_Globals());
}

static void QC_TestEvents_f(void)
{
	Con_Printf("{\"qc2cpp_test_events\":{\"normal_unpublish_count\":%u,"
		"\"legacy_game_entries\":%u,\"gameplay_during_init\":%u,\"init_count\":%u,"
		"\"client_connect_count\":%u,\"put_client_in_server_count\":%u,"
		"\"last_client_userid\":%d,"
		"\"spectator_put_client_in_server_count\":%u,"
		"\"client_disconnect_count\":%u,\"client_kill_count\":%u,"
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
		observer.gameplay_during_init, observer.init_count, observer.client_connect_count,
		observer.put_client_in_server_count, observer.last_client_userid,
		observer.spectator_put_client_in_server_count,
		observer.client_disconnect_count,
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

void QC_TestObserverRegisterCommands(void)
{
	Cmd_AddCommand("qc2cpp_test_snapshot", QC_TestSnapshot_f);
	Cmd_AddCommand("qc2cpp_test_events", QC_TestEvents_f);
	Cmd_AddCommand("qc2cpp_test_reuse_userid", QC_TestReuseUserId_f);
	Cmd_AddCommand("qc2cpp_test_save_state", QC_TestSaveState_f);
	Cmd_AddCommand("qc2cpp_test_release_connected_client",
		QC_TestReleaseConnectedClient_f);
}

void QC_TestObserverInitBegin(void)
{
	++observer.init_count;
	++observer.initializing;
}

void QC_TestObserverInitEnd(void)
{
	if (observer.initializing != 0U) {
		--observer.initializing;
	}
}

void QC_TestObserverStartFrame(void)
{
	++observer.frame_count;
	if (observer.initializing != 0U) {
		++observer.gameplay_during_init;
	}
}

void QC_TestObserverNormalUnpublish(void)
{
	++observer.normal_unpublish_count;
}

void QC_TestObserverLegacyGameEntry(void)
{
	++observer.legacy_game_entries;
}

static void QC_TestReuseUserId_f(void)
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

static int QC_TestFindSaveProbe(void)
{
	int slot;
	for (slot = 1; slot < sv.num_edicts; ++slot) {
		if (!sv.edicts[slot].e.free && sv.edicts[slot].v != NULL
			&& sv.edicts[slot].v->think != 0) return slot;
	}
	return -1;
}

static void QC_TestReportSaveState(void)
{
	char message[64] = "";
	uint32_t probe_slot = observer.save_probe_slot;
	int32_t probe_think = 0;
	float probe_nextthink = 0.0f;
	if (probe_slot == 0U) {
		const int discovered = QC_TestFindSaveProbe();
		if (discovered > 0) probe_slot = (uint32_t)discovered;
	}
	if (sv.num_edicts > 0 && sv.edicts[0].v != NULL) {
		(void)QC_CopyEntityString(&sv.edicts[0], "message", message,
			sizeof(message), NULL);
	}
	if (probe_slot < (uint32_t)sv.num_edicts && sv.edicts[probe_slot].v != NULL) {
		probe_think = sv.edicts[probe_slot].v->think;
		probe_nextthink = sv.edicts[probe_slot].v->nextthink;
	}
	Con_Printf("{\"qc2cpp_test_save_state\":{\"parm16\":%.1f,\"world_health\":%.1f,"
		"\"world_message\":\"%s\",\"probe_slot\":%u,\"probe_think\":%d,"
		"\"probe_nextthink\":%.6f,\"probe_trigger_dispatches\":%u}}\n",
		QC_Globals() == NULL ? 0.0f : QC_Globals()->parm16,
		sv.num_edicts > 0 && sv.edicts[0].v != NULL ? sv.edicts[0].v->health : 0.0f,
		message, probe_slot, probe_think, probe_nextthink,
		observer.save_probe_trigger_dispatches);
}

static void QC_TestSaveState_f(void)
{
	const char *mode;
	if (Cmd_Argc() != 2) {
		Con_Printf("Usage: qc2cpp_test_save_state <save|mutate|read|trigger>\n");
		return;
	}
	mode = Cmd_Argv(1);
	if (QC_Globals() == NULL || sv.num_edicts <= 0 || sv.edicts[0].v == NULL) {
		Con_Printf("qc2cpp save-state probe is unavailable\n");
		return;
	}
	if (!strcmp(mode, "save")) {
		const int slot = QC_TestFindSaveProbe();
		if (slot < 0) {
			Con_Printf("qc2cpp save-state probe has no callback owner\n");
			return;
		}
		observer.save_probe_slot = (uint32_t)slot;
		QC_Globals()->parm16 = 101.0f;
		sv.edicts[0].v->health = 101.0f;
		if (!QC_SetEntityString(&sv.edicts[0], "message", "qcms-saved")) {
			Con_Printf("qc2cpp save-state probe could not write string\n");
			return;
		}
	} else if (!strcmp(mode, "mutate")) {
		if (observer.save_probe_slot >= (uint32_t)sv.num_edicts
			|| sv.edicts[observer.save_probe_slot].v == NULL) {
			Con_Printf("qc2cpp save-state probe has no saved callback owner\n");
			return;
		}
		QC_Globals()->parm16 = 202.0f;
		sv.edicts[0].v->health = 202.0f;
		sv.edicts[observer.save_probe_slot].v->think = 0;
		sv.edicts[observer.save_probe_slot].v->nextthink = 0.0f;
		if (!QC_SetEntityString(&sv.edicts[0], "message", "qcms-mutated")) {
			Con_Printf("qc2cpp save-state probe could not mutate string\n");
			return;
		}
	} else if (!strcmp(mode, "trigger")) {
		int slot = (int)observer.save_probe_slot;
		if (slot <= 0 || slot >= sv.num_edicts || sv.edicts[slot].v == NULL
			|| sv.edicts[slot].v->think == 0) {
			slot = QC_TestFindSaveProbe();
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
	QC_TestReportSaveState();
}

static void QC_TestReleaseConnectedClient_f(void)
{
	if (Cmd_Argc() != 1) {
		Con_Printf("Usage: qc2cpp_test_release_connected_client\n");
		return;
	}
	QC_TestObserverSendRestoreMarker("[qc2cpp-save-connected] release");
	Con_Printf("{\"qc2cpp_test_release_connected_client\":{\"released\":true}}\n");
}

void QC_TestObserverClientConnect(uint32_t self)
{
	++observer.client_connect_count;
	if (self > 0U && self <= MAX_CLIENTS) {
		observer.last_client_userid = svs.clients[self - 1U].userid;
	}
}
void QC_TestObserverPutClientInServer(uint32_t self, uint32_t spectator)
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
	if (QC_Globals() != NULL) {
		observer.shared_self_after_put_client = QC_Globals()->self;
	}
	if (self < (uint32_t)sv.num_edicts) {
		VectorCopy(EDICT_NUM(self)->v->origin, observer.player_origin_at_spawn);
	}
}
void QC_TestObserverClientDisconnect(void) { ++observer.client_disconnect_count; }
static void QC_TestObserverSendRestoreMarker(const char *marker)
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
void QC_TestObserverRestoreReplicationBegin(void)
{
	++observer.restore_replication_begin_count;
	QC_TestObserverSendRestoreMarker("[qc2cpp-save-connected] restore-begin");
}
void QC_TestObserverRestoreReplicationComplete(void)
{
	++observer.restore_replication_complete_count;
	QC_TestObserverSendRestoreMarker("[qc2cpp-save-connected] restore-complete");
}
void QC_TestObserverClientKill(uint32_t self)
{
	++observer.client_kill_count;
	if (self < (uint32_t)sv.num_edicts) {
		observer.player_health_after_kill = (int32_t)EDICT_NUM(self)->v->health;
		observer.player_frags_after_kill = (int32_t)EDICT_NUM(self)->v->frags;
	}
}
void QC_TestObserverClientPreThink(void) { ++observer.client_prethink_count; }
void QC_TestObserverClientPostThink(uint32_t self, uint32_t spectator)
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

void QC_TestObserverEdictThink(edict_t *thinking)
{
	if (thinking == EDICT_NUM((int)observer.save_probe_slot)) {
		++observer.save_probe_trigger_dispatches;
	}
}

void QC_TestObserverEdictTouch(edict_t *touched, edict_t *toucher)
{
	char classname[16];
	if (QC_CopyEntityString(touched, "classname", classname, sizeof(classname), NULL)
		!= QC_PLUGIN_OK || strcmp(classname, "teledeath") != 0) {
		return;
	}
	observer.teledeath_owner = (uint32_t)touched->v->owner;
	observer.teledeath_toucher = (uint32_t)toucher->e.entnum;
}
