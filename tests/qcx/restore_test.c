/* glibc requires this to declare mkdtemp in strict C11 mode. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "qcx/save.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { test_entity_capacity = 4 };

server_t sv;
server_static_t svs;
#ifdef FTE_PEXT_FLOATCOORDS
int msg_coordsize = 2;
cvar_t sv_bigcoords;
#endif
cvar_t sv_progsname;
cvar_t deathmatch, coop, teamplay;
void Cvar_Set(cvar_t *cvar, char *value)
{
	cvar->value = Q_atof(value);
}
int QCX_ConfigureGlobals(float saved_deathmatch, float saved_coop, float saved_teamplay)
{
	(void)saved_deathmatch;
	(void)saved_coop;
	(void)saved_teamplay;
	return 1;
}
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
static uint32_t entity_capacity = test_entity_capacity;
static uint32_t create_path_calls;
static uint32_t flush_fs_calls;
static qcx_restore_status_t guest_validation_status;
static jmp_buf restore_error;
static qbool expect_restore_error;
static uint32_t active_profile = QCX_GAME_PROFILE_QUAKEWORLD;
static unsigned session_installs;

uint32_t QCX_GameCompatibility(void) { return active_profile; }

qbool QCX_RestoreSessionBlocksSave(void) { return false; }
qbool QCX_RestoreSessionInstall(const qcx_save_image_t *image, double monotonic_now)
{
	(void)image;
	(void)monotonic_now;
	++session_installs;
	return true;
}
double Sys_DoubleTime(void) { return 100.0; }

qbool QCX_Active(void) { return true; }
qcx_shared_global_state_v1_t *QCX_Globals(void) { return &restore_shared_globals; }
uint32_t QCX_EntityCapacity(void) { return entity_capacity; }
qcx_restore_status_t QCX_SetSaveSelection(const uint8_t *bitmap, qcx_byte_count_t size)
{
	assert(bitmap != NULL);
	assert(size == (entity_capacity + 7U) / 8U);
	++selection_calls;
	saved_selection = bitmap[0];
	return QCX_RESTORE_OK;
}
qcx_byte_count_t QCX_SaveGuest(uint8_t *out, qcx_byte_count_t capacity)
{
	if (out != NULL) {
		assert(capacity == 2U);
		memcpy(out, "OK", 2U);
	}
	return 2U;
}
qcx_restore_status_t QCX_ValidateGuestRestore(const uint8_t *data, qcx_byte_count_t size,
	const uint8_t *selection, qcx_byte_count_t selection_size)
{
	assert(data != NULL);
	assert(size == 2U);
	assert(selection != NULL);
	assert(selection_size == (entity_capacity + 7U) / 8U);
	assert(selection[0] == expected_validation_selection);
	++guest_validation_calls;
	return guest_validation_status;
}
qcx_restore_status_t QCX_RestoreGuest(const uint8_t *data, qcx_byte_count_t size)
{
	assert(data != NULL);
	assert(size == 2U);
	++guest_restore_calls;
	return QCX_RESTORE_OK;
}
void FS_CreatePath(char *path) { (void)path; ++create_path_calls; }
void FS_FlushFSHash(void) { ++flush_fs_calls; }
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
	assert(expect_restore_error);
	longjmp(restore_error, 1);
}
void Sys_Error(const char *error, ...)
{
	(void)error;
	abort();
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
	image.metadata.compatibility_profile = active_profile;
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
#ifdef FTE_PEXT_FLOATCOORDS
	msg_coordsize = 2;
	sv_bigcoords.value = 0.0f;
#endif
	memset(&sv, 0, sizeof(sv));
	memset(&svs, 0, sizeof(svs));
	memset(&restore_test_globals, 0, sizeof(restore_test_globals));
	memset(&restore_shared_globals, 0, sizeof(restore_shared_globals));
	pr_global_struct = (globalvars_t *)&restore_shared_globals;
	pr_globals = (float *)&restore_shared_globals;
	guest_validation_calls = 0U;
	guest_validation_status = QCX_RESTORE_OK;
	expect_restore_error = false;
	guest_restore_calls = 0U;
	selection_calls = 0U;
	link_calls = 0U;
	client_replication_updates = 0U;
	saved_selection = 0U;
	expected_validation_selection = 1U;
	entity_capacity = test_entity_capacity;
	create_path_calls = 0U;
	session_installs = 0U;
	flush_fs_calls = 0U;
	sv.max_edicts = test_entity_capacity;
	sv.time = 7.0;
	sv.map_checksum = UINT32_C(0x12345678);
	strcpy(sv.mapname, "e1m1");
	sv_progsname.string = "game";
	sv.edicts[0].e.free = false;
}

static void assert_empty_plan(const qcx_restore_plan_t *plan)
{
	assert(plan->image == NULL && plan->num_edicts == 0U);
	for (size_t index = 0U; index < sizeof(plan->selection); ++index) {
		assert(plan->selection[index] == 0U);
	}
}

static void expect_apply_rejected(qcx_restore_plan_t *plan)
{
	expect_restore_error = true;
	if (setjmp(restore_error) == 0) {
		QCX_ApplyRestore(plan);
		assert(!"unprepared restore must be rejected");
	}
	expect_restore_error = false;
}

static void test_invalid_image_is_rejected_before_guest_or_host_mutation(void)
{
	qcx_save_image_t image;
	qcx_restore_plan_t plan = {0};
	reset_live_engine();
	image = make_valid_image(true, false);
	strcpy(image.metadata.map_name, "e1m2");
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan) != QCX_RESTORE_OK);
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
	qcx_restore_plan_t plan = {0};
	reset_live_engine();
	image = make_valid_image(true, false);
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan) == QCX_RESTORE_OK);
	assert(guest_validation_calls == 1U);
	assert(guest_restore_calls == 0U);
	assert(sv.time == 7.0);
	QCX_ApplyRestore(&plan);
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

static void test_signon_encoding_mismatch_rejects_before_restore(void)
{
	qcx_restore_plan_t plan = {0};
	reset_live_engine();
	qcx_save_image_t image = make_valid_image(true, false);
	image.engine.signon_count = 1U;
	image.engine.signon_coord_bytes = 4U;
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan)
		== QCX_RESTORE_ENTITY_SET_MISMATCH);
	assert_empty_plan(&plan);
	assert(guest_validation_calls == 0U && guest_restore_calls == 0U);
	assert(sv.time == 7.0);
}

static void write_preflight_image(const char *directory, const char *name,
	const qcx_save_image_t *image)
{
	char path[MAX_OSPATH];
	uint8_t *bytes = NULL;
	uint32_t size = 0U;
	assert(QCX_SaveEncode(image, &bytes, &size) == QCX_PLUGIN_OK);
	assert(snprintf(path, sizeof(path), "%s/save/%s.sav", directory, name) > 0);
	FILE *file = fopen(path, "wb");
	assert(file != NULL);
	assert(fwrite(bytes, 1U, size, file) == size);
	assert(fclose(file) == 0);
	free(bytes);
}

static void test_profile_mismatch_retains_world_and_prepared_image(const char *directory)
{
	for (uint32_t profile = 1U; profile <= 2U; ++profile) {
		reset_live_engine();
		active_profile = profile;
		strcpy(fs_gamedir, directory);
		char map_name[QCX_SAVE_NAME_CAPACITY] = "unchanged";
		qcx_save_image_t prior = make_valid_image(true, false);
		write_preflight_image(directory, "profile-prior", &prior);
		assert(QCX_PrepareLoadGame("profile-prior", map_name, sizeof(map_name)));
		assert(QCX_PreparedGameProfileMatches(profile));
		assert(!QCX_PreparedGameProfileMatches(3U - profile));
		qcx_save_image_t other = make_valid_image(true, true);
		other.metadata.compatibility_profile = 3U - profile;
		other.engine.time = 91.0;
		write_preflight_image(directory, "profile-other", &other);
		strcpy(map_name, "unchanged");
		deathmatch.value = 4.0f;
		coop.value = 1.0f;
		teamplay.value = 2.0f;
		assert(!QCX_PrepareLoadGame("profile-other", map_name, sizeof(map_name)));
		assert(strcmp(map_name, "unchanged") == 0);
		assert(QCX_PreparedGameProfileMatches(profile));
		assert(!QCX_PreparedGameProfileMatches(3U - profile));
		assert(guest_validation_calls == 0U && guest_restore_calls == 0U);
		assert(selection_calls == 0U && link_calls == 0U && session_installs == 0U);
		assert(sv.time == 7.0 && strcmp(sv.mapname, "e1m1") == 0);
		assert(deathmatch.value == 4 && coop.value == 1 && teamplay.value == 2);
		qcx_restore_plan_t plan = {0};
		assert(QCX_PrepareRestore(&other, QCX_RESTORE_RESOURCES_EXACT, &plan)
			== QCX_RESTORE_ENTITY_SET_MISMATCH);
		assert_empty_plan(&plan);
		assert(guest_validation_calls == 0U && session_installs == 0U);
		assert(QCX_CommitPreparedLoadGame());
		assert(sv.time == 42.5); /* Still the original prepared save. */
		assert(QCX_PreparedGameProfileMatches(3U - profile)); /* No prepared image. */
	}
	active_profile = QCX_GAME_PROFILE_QUAKEWORLD;
}

#ifdef FTE_PEXT_FLOATCOORDS
static void test_preflight_uses_next_map_encoding_and_retains_prepared_load(
	const char *directory, uint32_t saved_coord_bytes)
{
	char map_name[QCX_SAVE_NAME_CAPACITY];
	char path[MAX_OSPATH];
	reset_live_engine();
	strcpy(fs_gamedir, directory);
	qcx_save_image_t prior = make_valid_image(true, false);
	write_preflight_image(directory, "prior", &prior);
	assert(QCX_PrepareLoadGame("prior", map_name, sizeof(map_name)));
	qcx_save_image_t candidate = make_valid_image(true, false);
	candidate.engine.time = 91.0;
	candidate.engine.signon_count = 1U;
	candidate.engine.signon_coord_bytes = saved_coord_bytes;
	write_preflight_image(directory, "candidate", &candidate);
	/* The running map matches the save, but the next map's setting does not. */
	msg_coordsize = (int)saved_coord_bytes;
	sv_bigcoords.value = saved_coord_bytes == 2U ? 1.0f : 0.0f;
	strcpy(map_name, "unchanged");
	assert(!QCX_PrepareLoadGame("candidate", map_name, sizeof(map_name)));
	assert(strcmp(map_name, "unchanged") == 0);
	assert(QCX_HasPreparedLoadGame());
	assert(guest_validation_calls == 0U && guest_restore_calls == 0U);
	assert(selection_calls == 0U && link_calls == 0U);
	assert(sv.time == 7.0 && strcmp(sv.mapname, "e1m1") == 0);
	assert(!sv.edicts[0].e.free);
	/* Rejection must not dispose of or replace the previously prepared image. */
	assert(QCX_CommitPreparedLoadGame());
	assert(sv.time == 42.5);
	/* Conversely, stale running-map encoding must not reject a compatible
	 * next-map request. Only map startup updates msg_coordsize. */
	msg_coordsize = saved_coord_bytes == 2U ? 4 : 2;
	sv_bigcoords.value = saved_coord_bytes == 4U ? 1.0f : 0.0f;
	assert(QCX_PrepareLoadGame("candidate", map_name, sizeof(map_name)));
	assert(strcmp(map_name, "e1m1") == 0);
	msg_coordsize = (int)saved_coord_bytes;
	assert(QCX_CommitPreparedLoadGame());
	assert(sv.time == 91.0);
	assert(snprintf(path, sizeof(path), "%s/save/prior.sav", directory) > 0);
	assert(remove(path) == 0);
	assert(snprintf(path, sizeof(path), "%s/save/candidate.sav", directory) > 0);
	assert(remove(path) == 0);
}
#endif

static void test_empty_world_restore_removes_previous_static_state(void)
{
	qcx_restore_plan_t plan = {0};
	reset_live_engine();
	qcx_save_image_t image = make_valid_image(true, false);
	sv.static_entity_count = 1;
	sv.static_entities[0].modelindex = 7;
	sv.num_signon_buffers = 2U;
	sv.signon_buffer_size[0] = 3;
	memset(sv.signon_buffers[0], 99, 3U);
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan)
		== QCX_RESTORE_OK);
	QCX_ApplyRestore(&plan);
	assert(sv.static_entity_count == 0 && sv.static_entities[0].modelindex == 0);
	assert(sv.num_signon_buffers == 1U && sv.signon.cursize == 0);
	assert(sv.signon.data == sv.signon_buffers[0]);
	assert(sv.signon_buffer_size[0] == 0 && sv.signon_buffer_size[1] == 0);
}

static void test_preparing_another_image_does_not_replace_first_restore(qbool second_valid)
{
	qcx_restore_plan_t first_plan = {0};
	qcx_restore_plan_t second_plan = {0};
	reset_live_engine();
	qcx_save_image_t first = make_valid_image(true, false);
	qcx_save_image_t second = make_valid_image(true, true);
	second.engine.time = 91.0;
	second.engine.serverflags = 99U;
	assert(QCX_PrepareRestore(&first, QCX_RESTORE_RESOURCES_EXACT, &first_plan) == QCX_RESTORE_OK);
	expected_validation_selection = 3U;
	if (!second_valid) {
		strcpy(second.metadata.map_name, "e1m2");
	}
	const qcx_restore_status_t status = QCX_PrepareRestore(&second,
		QCX_RESTORE_RESOURCES_EXACT, &second_plan);
	assert(status == (second_valid ? QCX_RESTORE_OK : QCX_RESTORE_ENTITY_SET_MISMATCH));

	QCX_ApplyRestore(&first_plan);

	assert(saved_selection == 1U && guest_restore_calls == 1U);
	assert(sv.time == 42.5 && svs.serverflags == 17U);
	assert(sv.num_edicts == 2 && sv.edicts[1].e.free);
}

static void test_failed_preparation_clears_a_reused_output_plan(qbool guest_rejection)
{
	qcx_restore_plan_t plan = {0};
	reset_live_engine();
	qcx_save_image_t image = make_valid_image(true, false);
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan) == QCX_RESTORE_OK);
	if (guest_rejection) {
		guest_validation_status = QCX_RESTORE_MALFORMED_CHUNK;
	} else {
		strcpy(image.metadata.map_name, "e1m2");
	}
	const qcx_restore_status_t status = QCX_PrepareRestore(&image,
		QCX_RESTORE_RESOURCES_EXACT, &plan);
	assert(status == (guest_rejection ? QCX_RESTORE_MALFORMED_CHUNK
		: QCX_RESTORE_ENTITY_SET_MISMATCH));
	assert_empty_plan(&plan);
	expect_apply_rejected(&plan);
	assert(selection_calls == 0U && guest_restore_calls == 0U && link_calls == 0U);
	assert(sv.time == 7.0 && !sv.edicts[0].e.free);
}

static void test_apply_consumes_a_prepared_plan(void)
{
	qcx_restore_plan_t plan = {0};
	reset_live_engine();
	qcx_save_image_t image = make_valid_image(true, false);
	expect_apply_rejected(&plan);
	assert(selection_calls == 0U && guest_restore_calls == 0U && link_calls == 0U);
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan) == QCX_RESTORE_OK);
	QCX_ApplyRestore(&plan);
	assert_empty_plan(&plan);
	expect_apply_rejected(&plan);
	assert(selection_calls == 1U && guest_restore_calls == 1U && link_calls == 1U);
	assert(sv.time == 42.5);
}

static void test_roster_restore_defers_client_replication_until_session_completion(void)
{
	qcx_save_image_t image;
	qcx_restore_plan_t plan = {0};
	reset_live_engine();
	svs.clients[0].state = cs_spawned;
	svs.clients[0].edict = &sv.edicts[1];
	svs.clients[0].delta_sequence = 23;
	expected_validation_selection = 3U;
	image = make_valid_image(true, true);
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan) == QCX_RESTORE_OK);
	QCX_ApplyRestore(&plan);
	assert(client_replication_updates == 0U);
	assert(svs.clients[0].delta_sequence == 23);
}

static void test_rejects_a_save_without_an_active_world_slot(void)
{
	qcx_save_image_t image;
	qcx_restore_plan_t plan = {0};
	reset_live_engine();
	expected_validation_selection = 0U;
	image = make_valid_image(false, false);
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan) != QCX_RESTORE_OK);
	assert(guest_validation_calls == 0U);
	assert(guest_restore_calls == 0U);
	assert(sv.edicts[0].e.free == false);
}

static void test_fresh_restore_does_not_require_current_client_lifecycle(void)
{
	qcx_save_image_t image;
	qcx_restore_plan_t plan = {0};
	reset_live_engine();
	svs.clients[0].state = cs_spawned;
	svs.clients[0].edict = &sv.edicts[1];
	image = make_valid_image(true, false);
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan) == QCX_RESTORE_OK);
	assert(guest_validation_calls == 1U);
	assert(guest_restore_calls == 0U);
}

static void test_fresh_restore_does_not_require_matching_current_slot_state(void)
{
	qcx_save_image_t image;
	qcx_restore_plan_t plan = {0};
	reset_live_engine();
	svs.clients[0].state = cs_connected;
	svs.clients[0].edict = &sv.edicts[1];
	expected_validation_selection = 3U;
	image = make_valid_image(true, true);
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan) == QCX_RESTORE_OK);
	assert(guest_validation_calls == 1U);
	assert(guest_restore_calls == 0U);
}

static void test_fresh_restore_does_not_require_matching_current_role(void)
{
	qcx_save_image_t image;
	qcx_restore_plan_t plan = {0};
	reset_live_engine();
	svs.clients[0].state = cs_spawned;
	svs.clients[0].spectator = 1;
	svs.clients[0].edict = &sv.edicts[1];
	expected_validation_selection = 3U;
	image = make_valid_image(true, true);
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan) == QCX_RESTORE_OK);
	assert(guest_validation_calls == 1U);
	assert(guest_restore_calls == 0U);
}

static void test_rejects_a_roster_client_without_an_active_player_entity(void)
{
	qcx_save_image_t image;
	qcx_restore_plan_t plan = {0};
	reset_live_engine();
	image = make_valid_image(true, true);
	image.roster[0].saved_slot = 1U;
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan) != QCX_RESTORE_OK);
	assert(guest_validation_calls == 0U);
	assert(guest_restore_calls == 0U);
	assert(sv.time == 7.0);
}

static void test_prepared_resources_survive_image_disposal(const char *directory)
{
	qcx_restore_plan_t plan = {0};
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
	image.engine.lightstyles[2][0] = '\0';
	memset(image.engine.lightstyles[3], 'a', 255U);
	image.engine.lightstyles[3][255] = '\0';
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
	assert(QCX_PrepareLoadStartup());
	assert(strcmp(sv.model_precache[0], "") == 0);
	assert(strcmp(sv.sound_precache[0], "") == 0);
	/* Pending load ownership must not silently relax other preparations. */
	sv.model_precache[8] = "progs/startup-extra.mdl";
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan)
		== QCX_RESTORE_ENTITY_SET_MISMATCH);
	assert_empty_plan(&plan);
	assert(QCX_CommitPreparedLoadGame());
	assert(!QCX_HasPreparedLoadGame());
	assert(strcmp(sv.model_precache[7], "progs/player.mdl") == 0);
	assert(strcmp(sv.sound_precache[19], "misc/menu.wav") == 0);
	assert(strcmp(sv.model_precache[8], "progs/startup-extra.mdl") == 0);
	assert(strcmp(sv.lightstyles[63], "m") == 0);
	assert(sv.lightstyles[2][0] == '\0');
	assert(strlen(sv.lightstyles[3]) == 255U);
	assert(memcmp(sv.lightstyles[3], image.engine.lightstyles[3], 256U) == 0);
	assert(sv.time == 42.5 && guest_restore_calls == 1U);
	free(sv.model_precache[7]);
	free(sv.sound_precache[19]);
	assert(remove(path) == 0);
}

static void test_checks_parsed_precaches_against_the_current_world(void)
{
	qcx_restore_plan_t plan = {0};
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
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan) == QCX_RESTORE_OK);
	assert(guest_validation_calls == 1U);
	sv.sound_precache[19] = "misc/other.wav";
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan)
		== QCX_RESTORE_ENTITY_SET_MISMATCH);
	assert(guest_validation_calls == 1U);
	sv.sound_precache[19] = "misc/menu.wav";
	sv.model_precache[8] = "progs/extra.mdl";
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan)
		== QCX_RESTORE_ENTITY_SET_MISMATCH);
	assert(guest_validation_calls == 1U);
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_ALLOW_EXTRA, &plan) == QCX_RESTORE_OK);
	assert(guest_validation_calls == 2U);
	sv.sound_precache[19] = "misc/other.wav";
	assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_ALLOW_EXTRA, &plan)
		== QCX_RESTORE_ENTITY_SET_MISMATCH);
	assert_empty_plan(&plan);
	assert(guest_validation_calls == 2U);
	assert(guest_restore_calls == 0U);
	assert(sv.time == 7.0);
}

static void test_save_path_limits_preserve_existing_file(void)
{
	char parent[] = "/tmp/qcx-save-path-XXXXXX";
	assert(mkdtemp(parent) != NULL);
	/* Full .tmp fits at MAX_OSPATH-5. The next four lengths fit .sav only;
	 * MAX_OSPATH also truncates the final path. These five cases must fail
	 * before file I/O. */
	for (size_t path_length = MAX_OSPATH - 5U; path_length <= MAX_OSPATH; ++path_length) {
		char directory[MAX_OSPATH];
		char save_directory[MAX_OSPATH];
		char path[MAX_OSPATH + 8U];
		char temporary[MAX_OSPATH + 8U];
		const char suffix[] = "/save/edge.sav";
		const char prior[] = "prior save";
		const size_t directory_length = path_length - (sizeof(suffix) - 1U);
		const size_t prefix_length = strlen(parent);
		reset_live_engine();
		entity_capacity = MAX_CLIENTS + 1U;
		sv.max_edicts = (int)entity_capacity;
		sv.num_edicts = 1;
		sv.state = ss_active;
		assert(prefix_length + 1U < directory_length);
		memcpy(directory, parent, prefix_length);
		directory[prefix_length] = '/';
		memset(directory + prefix_length + 1U, 'p', directory_length - prefix_length - 1U);
		directory[directory_length] = '\0';
		assert(mkdir(directory, 0700) == 0);
		assert(snprintf(save_directory, sizeof(save_directory), "%s/save", directory)
			== (int)directory_length + 5);
		assert(mkdir(save_directory, 0700) == 0);
		assert(snprintf(path, sizeof(path), "%s%s", directory, suffix) == (int)path_length);
		assert(snprintf(temporary, sizeof(temporary), "%s.tmp", path) == (int)path_length + 4);
		strcpy(fs_gamedir, directory);
		FILE *file = fopen(path, "wb");
		assert(file != NULL);
		assert(fwrite(prior, 1U, sizeof(prior), file) == sizeof(prior));
		assert(fclose(file) == 0);
		const qbool saved = QCX_SaveGame("edge");
		file = fopen(path, "rb");
		assert(file != NULL);
		if (path_length == MAX_OSPATH - 5U) {
			char magic[4];
			assert(saved && create_path_calls == 1U && flush_fs_calls == 1U);
			assert(fread(magic, 1U, sizeof(magic), file) == sizeof(magic));
			assert(memcmp(magic, "QCMS", sizeof(magic)) == 0);
		} else {
			char actual[sizeof(prior)];
			assert(fread(actual, 1U, sizeof(actual), file) == sizeof(actual));
			assert(memcmp(actual, prior, sizeof(prior)) == 0);
			assert(fgetc(file) == EOF);
			assert(!saved && create_path_calls == 0U && flush_fs_calls == 0U);
		}
		assert(fclose(file) == 0);
		assert(access(temporary, F_OK) != 0);
		assert(remove(path) == 0);
		assert(rmdir(save_directory) == 0);
		assert(rmdir(directory) == 0);
	}
	assert(rmdir(parent) == 0);
}

static void test_world_signon_survives_fresh_restore(const char *directory)
{
	char map_name[QCX_SAVE_NAME_CAPACITY];
	char path[MAX_OSPATH];
	const byte ambient[] = {29, 8, 0, 16, 0, 24, 0, 1, 127, 192};
	reset_live_engine();
	deathmatch.value = 0.0f;
	coop.value = 1.0f;
	teamplay.value = 2.0f;
	entity_capacity = MAX_CLIENTS + 1U;
	sv.max_edicts = (int)entity_capacity;
	sv.num_edicts = 1;
	sv.state = ss_active;
	sv.model_precache[1] = "progs/flame.mdl";
	sv.sound_precache[1] = "ambience/fire1.wav";
	sv.edicts[0].xv.alpha = 0.375f;
	VectorSet(sv.edicts[0].xv.colourmod, 0.25f, 0.5f, 2.0f);
	sv.num_edicts = 2;
	sv.edicts[1].e.free = true;
	sv.edicts[1].xv.alpha = 0.75f;
	VectorSet(sv.edicts[1].xv.colourmod, 2.0f, 1.0f, 0.5f);
	sv.static_entity_count = 1;
	entity_state_t *model = &sv.static_entities[0];
	model->number = 1;
	model->modelindex = 1;
	model->frame = 3;
	model->colormap = 4;
	model->skinnum = 5;
	VectorSet(model->origin, 32, 64, 96);
	VectorSet(model->angles, 0, 90, 0);
#ifdef FTE_PEXT_TRANS
	model->trans = 127;
#endif
#ifdef FTE_PEXT_COLOURMOD
	model->colourmod[0] = 32;
	model->colourmod[1] = 64;
	model->colourmod[2] = 96;
#endif
	const entity_state_t expected = *model;
	sv.num_signon_buffers = 2;
	sv.signon_buffer_size[0] = sizeof(ambient);
	memcpy(sv.signon_buffers[0], ambient, sizeof(ambient));
	sv.signon.data = sv.signon_buffers[1];
	sv.signon.maxsize = sizeof(sv.signon_buffers[1]);
	sv.signon.cursize = sizeof(ambient);
	memcpy(sv.signon.data, ambient, sizeof(ambient));
	/* The active chunk's authoritative size is cursize, not the sealed size. */
	sv.signon_buffer_size[1] = 0;
	strcpy(fs_gamedir, directory);
	assert(QCX_SaveGame("world"));
	reset_live_engine();
	deathmatch.value = 4.0f;
	coop.value = 0.0f;
	teamplay.value = 0.0f;
	entity_capacity = MAX_CLIENTS + 1U;
	sv.max_edicts = (int)entity_capacity;
	assert(QCX_PrepareLoadGame("world", map_name, sizeof(map_name)));
	/* Preflight must not change the current world's mode. */
	assert(deathmatch.value == 4.0f && coop.value == 0.0f && teamplay.value == 0.0f);
	assert(QCX_PrepareLoadStartup());
	/* Initialization of the replacement game must see the saved mode. */
	assert(deathmatch.value == 0.0f && coop.value == 1.0f && teamplay.value == 2.0f);
	assert(QCX_CommitPreparedLoadGame());
	assert(deathmatch.value == 0.0f && coop.value == 1.0f && teamplay.value == 2.0f);
	assert(sv.edicts[0].xv.alpha == 0.375f);
	assert(sv.edicts[0].xv.colourmod[0] == 0.25f);
	assert(sv.edicts[0].xv.colourmod[1] == 0.5f);
	assert(sv.edicts[0].xv.colourmod[2] == 2.0f);
	assert(sv.edicts[1].e.free && sv.edicts[1].xv.alpha == 0.75f);
	assert(sv.edicts[1].xv.colourmod[0] == 2.0f);
	assert(sv.edicts[1].xv.colourmod[1] == 1.0f);
	assert(sv.edicts[1].xv.colourmod[2] == 0.5f);
	assert(sv.static_entity_count == 1);
	assert(memcmp(&sv.static_entities[0], &expected, sizeof(expected)) == 0);
	assert(sv.num_signon_buffers == 2);
	assert(sv.signon_buffer_size[0] == sizeof(ambient));
	assert(sv.signon_buffer_size[1] == sizeof(ambient));
	assert(sv.signon.data == sv.signon_buffers[1]);
	assert(sv.signon.cursize == sizeof(ambient));
	assert(sv.signon.maxsize == sizeof(sv.signon_buffers[1]));
	assert(memcmp(sv.signon_buffers[0], ambient, sizeof(ambient)) == 0);
	assert(memcmp(sv.signon.data, ambient, sizeof(ambient)) == 0);
	free(sv.model_precache[1]);
	free(sv.sound_precache[1]);
	assert(snprintf(path, sizeof(path), "%s/save/world.sav", directory) > 0);
	assert(remove(path) == 0);
}

static void test_game_mode_round_trip_keeps_float_precision(void)
{
	const float values[] = {0.0f, 4.0f, 0.123456789f, 0.000000123f,
		-FLT_MAX, FLT_MAX, FLT_MIN, FLT_TRUE_MIN};
	for (size_t index = 0U; index < sizeof(values) / sizeof(values[0]); ++index) {
		reset_live_engine();
		qcx_save_image_t image = make_valid_image(true, false);
		image.engine.deathmatch = values[index];
		image.engine.coop = values[index];
		image.engine.teamplay = values[index];
		qcx_restore_plan_t plan = {0};
		assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan) == QCX_RESTORE_OK);
		QCX_ApplyRestore(&plan);
		assert(deathmatch.value == values[index]);
		assert(coop.value == values[index]);
		assert(teamplay.value == values[index]);
	}
}

static void test_invalid_mode_does_not_mutate_current_world(void)
{
	reset_live_engine();
	deathmatch.value = 4.0f;
	coop.value = 0.0f;
	teamplay.value = 2.0f;
	qcx_save_image_t image = make_valid_image(true, false);
	float *const fields[] = {&image.engine.deathmatch, &image.engine.coop,
		&image.engine.teamplay};
	for (size_t index = 0U; index < 3U; ++index) {
		*fields[index] = NAN;
		qcx_restore_plan_t plan = {0};
		assert(QCX_PrepareRestore(&image, QCX_RESTORE_RESOURCES_EXACT, &plan)
			== QCX_RESTORE_MALFORMED_CHUNK);
		assert_empty_plan(&plan);
		assert(deathmatch.value == 4.0f && coop.value == 0.0f && teamplay.value == 2.0f);
		assert(sv.time == 7.0 && !sv.edicts[0].e.free);
		assert(guest_validation_calls == 0U && guest_restore_calls == 0U && link_calls == 0U);
		*fields[index] = 0.0f;
	}
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	test_profile_mismatch_retains_world_and_prepared_image(argv[1]);
	test_game_mode_round_trip_keeps_float_precision();
	test_invalid_mode_does_not_mutate_current_world();
#ifdef FTE_PEXT_FLOATCOORDS
	test_preflight_uses_next_map_encoding_and_retains_prepared_load(argv[1], 2U);
	test_preflight_uses_next_map_encoding_and_retains_prepared_load(argv[1], 4U);
#endif
	test_world_signon_survives_fresh_restore(argv[1]);
	test_signon_encoding_mismatch_rejects_before_restore();
	test_empty_world_restore_removes_previous_static_state();
	test_preparing_another_image_does_not_replace_first_restore(true);
	test_preparing_another_image_does_not_replace_first_restore(false);
	test_failed_preparation_clears_a_reused_output_plan(false);
	test_failed_preparation_clears_a_reused_output_plan(true);
	test_apply_consumes_a_prepared_plan();
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
	test_save_path_limits_preserve_existing_file();
	return 0;
}
