#include "qwsvdef.h"

#include "qc2cpp/test_observer.h"
#include "qc2cpp/entities.h"
#include "qc2cpp/globals.h"

#include <stdint.h>
#include <string.h>

typedef struct qc_test_observer_s {
	uint32_t frame_count;
	uint32_t normal_unpublish_count;
	uint32_t legacy_game_entries;
	uint32_t gameplay_during_init;
	uint32_t initializing;
	uint32_t client_connect_count;
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
} qc_test_observer_t;

static qc_test_observer_t observer;

static void QC_TestSnapshot_f(void)
{
	Con_Printf("{\"qc2cpp_test_snapshot\":{\"map\":\"%s\",\"frame_count\":%u}}\n",
		sv.mapname, observer.frame_count);
}

static void QC_TestEvents_f(void)
{
	Con_Printf("{\"qc2cpp_test_events\":{\"normal_unpublish_count\":%u,"
		"\"legacy_game_entries\":%u,\"gameplay_during_init\":%u,"
		"\"client_connect_count\":%u,\"put_client_in_server_count\":%u,"
		"\"spectator_put_client_in_server_count\":%u,"
		"\"client_disconnect_count\":%u,\"client_kill_count\":%u,"
		"\"player_health_after_kill\":%d,"
		"\"player_frags_after_kill\":%d,"
		"\"player_moved_after_spawn\":%u,"
		"\"client_prethink_count\":%u,\"client_postthink_count\":%u,"
		"\"spectator_think_count\":%u,"
		"\"teledeath_owner\":%u,\"teledeath_toucher\":%u,"
		"\"shared_self_after_put_client\":%u}}\n",
		observer.normal_unpublish_count, observer.legacy_game_entries,
		observer.gameplay_during_init, observer.client_connect_count,
		observer.put_client_in_server_count, observer.spectator_put_client_in_server_count,
		observer.client_disconnect_count,
		observer.client_kill_count, observer.player_health_after_kill,
		observer.player_frags_after_kill,
		observer.player_moved_after_spawn,
		observer.client_prethink_count,
		observer.client_postthink_count, observer.spectator_think_count,
		observer.teledeath_owner,
		observer.teledeath_toucher, observer.shared_self_after_put_client);
}

void QC_TestObserverRegisterCommands(void)
{
	Cmd_AddCommand("qc2cpp_test_snapshot", QC_TestSnapshot_f);
	Cmd_AddCommand("qc2cpp_test_events", QC_TestEvents_f);
}

void QC_TestObserverInitBegin(void)
{
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

void QC_TestObserverClientConnect(void) { ++observer.client_connect_count; }
void QC_TestObserverPutClientInServer(uint32_t self, uint32_t spectator)
{
	++observer.put_client_in_server_count;
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
