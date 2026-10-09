#include "qcx/save.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { test_entity_capacity = 4 };

server_t sv;
server_static_t svs;
cvar_t sv_progsname;
char fs_gamedir[MAX_OSPATH];
globalvars_t restore_test_globals;
qcx_shared_global_state_v1_t restore_shared_globals;
globalvars_t *pr_global_struct = (globalvars_t *)&restore_shared_globals;
float *pr_globals = (float *)&restore_shared_globals;

static uint32_t guest_validation_calls;
static uint32_t guest_restore_calls;
static uint32_t selection_calls;
static uint32_t link_calls;
static uint32_t client_replication_updates;
static uint8_t saved_selection;
static uint8_t expected_validation_selection;

qbool QCX_RestoreSessionBlocksSave(void) { return false; }
qbool QCX_RestoreSessionInstall(const qcx_save_image_t *image, double monotonic_now)
{
	(void)image;
	(void)monotonic_now;
	return true;
}
double Sys_DoubleTime(void) { return 100.0; }

qbool QCX_Active(void) { return true; }
qcx_shared_global_state_v1_t *QCX_Globals(void) { return &restore_shared_globals; }
uint32_t QCX_EntityCapacity(void) { return test_entity_capacity; }
qcx_restore_status_t QCX_SetSaveSelection(const uint8_t *bitmap, qcx_byte_count_t size)
{
	assert(bitmap != NULL);
	assert(size == 1U);
	++selection_calls;
	saved_selection = bitmap[0];
	return QCX_RESTORE_OK;
}
qcx_byte_count_t QCX_SaveGuest(uint8_t *out, qcx_byte_count_t capacity)
{
	(void)out;
	(void)capacity;
	return 0U;
}
qcx_restore_status_t QCX_ValidateGuestRestore(const uint8_t *data, qcx_byte_count_t size,
	const uint8_t *selection, qcx_byte_count_t selection_size)
{
	assert(data != NULL);
	assert(size == 2U);
	assert(selection != NULL);
	assert(selection_size == 1U);
	assert(selection[0] == expected_validation_selection);
	++guest_validation_calls;
	return QCX_RESTORE_OK;
}
qcx_restore_status_t QCX_RestoreGuest(const uint8_t *data, qcx_byte_count_t size)
{
	assert(data != NULL);
	assert(size == 2U);
	++guest_restore_calls;
	return QCX_RESTORE_OK;
}
void FS_CreatePath(char *path) { (void)path; }
void FS_FlushFSHash(void) {}
void *Hunk_Alloc(int size) { return malloc((size_t)size); }
void SV_ClearWorld(void) {}
void SV_LinkEdict(edict_t *edict, qbool touch_triggers)
{
	assert(edict == &sv.edicts[0] || edict == &sv.edicts[1]);
	assert(!touch_triggers);
	++link_calls;
}
void SV_FullClientUpdateToClient(client_t *source, client_t *recipient)
{
	assert(source == &svs.clients[0]);
	assert(recipient == &svs.clients[0]);
	++client_replication_updates;
}
void SV_Error(char *error, ...)
{
	(void)error;
	assert(!"unexpected restore commit failure");
}
void Con_Printf(char *fmt, ...)
{
	va_list arguments;
	va_start(arguments, fmt);
	va_end(arguments);
}

static qcx_save_image_t make_valid_image(uint32_t world_flags, qbool restored_client)
{
	qcx_save_image_t image = {0};
	uint32_t index;
	image.engine.time = 42.5;
	image.engine.serverflags = 17U;
	for (index = 0U; index < QCX_SAVE_LIGHTSTYLE_COUNT; ++index) {
		strcpy(image.engine.lightstyles[index], "m");
	}
	image.engine.edicts[0].state = (qcx_save_edict_state_t)world_flags;
	image.engine.edicts[1].state = world_flags != 1U ? QCX_SAVE_UNUSED_EDICT
		: restored_client ? QCX_SAVE_ACTIVE_EDICT : QCX_SAVE_FREE_EDICT;
	image.engine.edicts[1].freetime = restored_client ? 0.0f : 3.0f;
	strcpy(image.metadata.logical_game, "game");
	strcpy(image.metadata.map_name, "e1m1");
	image.metadata.map_bsp_checksum = UINT32_C(0x12345678);
	image.metadata.entity_capacity = test_entity_capacity;
	image.metadata.client_slot_capacity = 3U;
	image.roster_count = restored_client ? 1U : 0U;
	if (restored_client) {
		image.roster[0].saved_slot = 0U;
		image.roster[0].spawned = 1U;
		image.roster[0].role = QCX_SAVE_ROLE_PLAYER;
		strcpy(image.roster[0].name, "Alice");
		strcpy(image.roster[0].team, "red");
		for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
			image.roster[0].spawn_parms[index] = (float)index + 0.5f;
		}
	}
	image.guest_payload = (qcx_save_bytes_t){(uint8_t *)"OK", 2U};
	return image;
}

static void reset_live_engine(void)
{
	memset(&sv, 0, sizeof(sv));
	memset(&svs, 0, sizeof(svs));
	memset(&restore_test_globals, 0, sizeof(restore_test_globals));
	memset(&restore_shared_globals, 0, sizeof(restore_shared_globals));
	pr_global_struct = (globalvars_t *)&restore_shared_globals;
	pr_globals = (float *)&restore_shared_globals;
	guest_validation_calls = 0U;
	guest_restore_calls = 0U;
	selection_calls = 0U;
	link_calls = 0U;
	client_replication_updates = 0U;
	saved_selection = 0U;
	expected_validation_selection = 1U;
	sv.max_edicts = test_entity_capacity;
	sv.time = 7.0;
	sv.map_checksum = UINT32_C(0x12345678);
	strcpy(sv.mapname, "e1m1");
	sv_progsname.string = "game";
	sv.edicts[0].e.free = false;
}

static void test_invalid_image_is_rejected_before_guest_or_host_mutation(void)
{
	qcx_save_image_t image;
	reset_live_engine();
	image = make_valid_image(true, false);
	strcpy(image.metadata.map_name, "e1m2");
	assert(QCX_ValidateSaveGame(&image) != QCX_RESTORE_OK);
	assert(guest_validation_calls == 0U);
	assert(guest_restore_calls == 0U);
	assert(selection_calls == 0U);
	assert(link_calls == 0U);
	assert(sv.time == 7.0);
	assert(sv.edicts[0].e.free == false);
}

static void test_valid_image_validates_then_applies_in_place(void)
{
	qcx_save_image_t image;
	reset_live_engine();
	image = make_valid_image(true, false);
	assert(QCX_ValidateSaveGame(&image) == QCX_RESTORE_OK);
	assert(guest_validation_calls == 1U);
	assert(guest_restore_calls == 0U);
	assert(sv.time == 7.0);
	QCX_ApplySaveGame(&image);
	assert(selection_calls == 1U);
	assert(saved_selection == 1U);
	assert(guest_restore_calls == 1U);
	assert(link_calls == 1U);
	assert(sv.time == 42.5);
	assert(sv.old_time == 42.5);
	assert(svs.serverflags == 17U);
	assert(restore_shared_globals.serverflags == 17U);
	assert(sv.num_edicts == 2);
	assert(sv.edicts[0].e.free == false);
	assert(sv.edicts[1].e.free == true);
	assert(sv.edicts[1].e.freetime == 3.0f);
}

static void test_roster_restore_defers_client_replication_until_session_completion(void)
{
	qcx_save_image_t image;
	reset_live_engine();
	svs.clients[0].state = cs_spawned;
	svs.clients[0].edict = &sv.edicts[1];
	svs.clients[0].delta_sequence = 23;
	expected_validation_selection = 3U;
	image = make_valid_image(true, true);
	assert(QCX_ValidateSaveGame(&image) == QCX_RESTORE_OK);
	QCX_ApplySaveGame(&image);
	assert(client_replication_updates == 0U);
	assert(svs.clients[0].delta_sequence == 23);
}

static void test_rejects_a_save_without_an_active_world_slot(void)
{
	qcx_save_image_t image;
	reset_live_engine();
	expected_validation_selection = 0U;
	image = make_valid_image(false, false);
	assert(QCX_ValidateSaveGame(&image) != QCX_RESTORE_OK);
	assert(guest_validation_calls == 0U);
	assert(guest_restore_calls == 0U);
	assert(sv.edicts[0].e.free == false);
}

static void test_fresh_restore_does_not_require_current_client_lifecycle(void)
{
	qcx_save_image_t image;
	reset_live_engine();
	svs.clients[0].state = cs_spawned;
	svs.clients[0].edict = &sv.edicts[1];
	image = make_valid_image(true, false);
	assert(QCX_ValidateSaveGame(&image) == QCX_RESTORE_OK);
	assert(guest_validation_calls == 1U);
	assert(guest_restore_calls == 0U);
}

static void test_fresh_restore_does_not_require_matching_current_slot_state(void)
{
	qcx_save_image_t image;
	reset_live_engine();
	svs.clients[0].state = cs_connected;
	svs.clients[0].edict = &sv.edicts[1];
	expected_validation_selection = 3U;
	image = make_valid_image(true, true);
	assert(QCX_ValidateSaveGame(&image) == QCX_RESTORE_OK);
	assert(guest_validation_calls == 1U);
	assert(guest_restore_calls == 0U);
}

static void test_fresh_restore_does_not_require_matching_current_role(void)
{
	qcx_save_image_t image;
	reset_live_engine();
	svs.clients[0].state = cs_spawned;
	svs.clients[0].spectator = 1;
	svs.clients[0].edict = &sv.edicts[1];
	expected_validation_selection = 3U;
	image = make_valid_image(true, true);
	assert(QCX_ValidateSaveGame(&image) == QCX_RESTORE_OK);
	assert(guest_validation_calls == 1U);
	assert(guest_restore_calls == 0U);
}

static void test_rejects_a_roster_client_without_an_active_player_entity(void)
{
	qcx_save_image_t image;
	reset_live_engine();
	image = make_valid_image(true, true);
	image.roster[0].saved_slot = 1U;
	assert(QCX_ValidateSaveGame(&image) != QCX_RESTORE_OK);
	assert(guest_validation_calls == 0U);
	assert(guest_restore_calls == 0U);
	assert(sv.time == 7.0);
}

static void test_prepared_resources_survive_image_disposal(const char *directory)
{
	qcx_save_precache_t precaches[] = {
		{.kind = 'M', .slot = 7U, .name = "progs/player.mdl"},
		{.kind = 'S', .slot = 19U, .name = "misc/menu.wav"}
	};
	char path[MAX_OSPATH];
	char map_name[QCX_SAVE_NAME_CAPACITY];
	uint8_t *encoded = NULL;
	uint32_t encoded_size = 0U;
	reset_live_engine();
	qcx_save_image_t image = make_valid_image(true, false);
	image.engine.precache_count = 2U;
	image.engine.precaches = precaches;
	assert(QCX_SaveEncode(&image, &encoded, &encoded_size) == QCX_PLUGIN_OK);
	assert(strlcpy(fs_gamedir, directory, sizeof(fs_gamedir)) < sizeof(fs_gamedir));
	assert(snprintf(path, sizeof(path), "%s/save/prepared.sav", directory) > 0);
	FILE *file = fopen(path, "wb");
	assert(file != NULL);
	assert(fwrite(encoded, 1U, encoded_size, file) == encoded_size);
	assert(fclose(file) == 0);
	free(encoded);
	assert(QCX_PrepareLoadGame("prepared", map_name, sizeof(map_name)));
	assert(strcmp(map_name, "e1m1") == 0);
	assert(QCX_HasPreparedLoadGame());
	assert(sv.model_precache[7] == NULL && sv.sound_precache[19] == NULL);
	assert(sv.time == 7.0 && guest_restore_calls == 0U);
	assert(QCX_PrepareLoadResources());
	assert(strcmp(sv.model_precache[0], "") == 0);
	assert(strcmp(sv.sound_precache[0], "") == 0);
	assert(QCX_CommitPreparedLoadGame());
	assert(!QCX_HasPreparedLoadGame());
	assert(strcmp(sv.model_precache[7], "progs/player.mdl") == 0);
	assert(strcmp(sv.sound_precache[19], "misc/menu.wav") == 0);
	assert(strcmp(sv.lightstyles[63], "m") == 0);
	assert(sv.time == 42.5 && guest_restore_calls == 1U);
	free(sv.model_precache[7]);
	free(sv.sound_precache[19]);
	assert(remove(path) == 0);
}

static void test_checks_parsed_precaches_against_the_current_world(void)
{
	qcx_save_precache_t precaches[] = {
		{.kind = 'M', .slot = 7U, .name = "progs/player.mdl"},
		{.kind = 'S', .slot = 19U, .name = "misc/menu.wav"}
	};
	reset_live_engine();
	qcx_save_image_t image = make_valid_image(true, false);
	image.engine.precache_count = 2U;
	image.engine.precaches = precaches;
	sv.model_precache[7] = "progs/player.mdl";
	sv.sound_precache[19] = "misc/menu.wav";
	assert(QCX_ValidateSaveGame(&image) == QCX_RESTORE_OK);
	assert(guest_validation_calls == 1U);
	sv.sound_precache[19] = "misc/other.wav";
	assert(QCX_ValidateSaveGame(&image) == QCX_RESTORE_ENTITY_SET_MISMATCH);
	assert(guest_validation_calls == 1U);
	sv.sound_precache[19] = "misc/menu.wav";
	sv.model_precache[8] = "progs/extra.mdl";
	assert(QCX_ValidateSaveGame(&image) == QCX_RESTORE_ENTITY_SET_MISMATCH);
	assert(guest_validation_calls == 1U);
	assert(guest_restore_calls == 0U);
	assert(sv.time == 7.0);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	test_invalid_image_is_rejected_before_guest_or_host_mutation();
	test_valid_image_validates_then_applies_in_place();
	test_roster_restore_defers_client_replication_until_session_completion();
	test_rejects_a_save_without_an_active_world_slot();
	test_fresh_restore_does_not_require_current_client_lifecycle();
	test_fresh_restore_does_not_require_matching_current_slot_state();
	test_fresh_restore_does_not_require_matching_current_role();
	test_rejects_a_roster_client_without_an_active_player_entity();
	test_checks_parsed_precaches_against_the_current_world();
	test_prepared_resources_survive_image_disposal(argv[1]);
	return 0;
}
