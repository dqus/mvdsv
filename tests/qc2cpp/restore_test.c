#include "qc2cpp/save.h"

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
globalvars_t *pr_global_struct = &restore_test_globals;
float *pr_globals = (float *)&restore_test_globals;
qc_shared_global_state_v1_t restore_shared_globals;

static uint32_t guest_validation_calls;
static uint32_t guest_restore_calls;
static uint32_t selection_calls;
static uint32_t link_calls;
static uint32_t client_replication_updates;
static uint8_t saved_selection;
static uint8_t expected_validation_selection;

qbool QC_Active(void) { return true; }
qc_shared_global_state_v1_t *QC_Globals(void) { return &restore_shared_globals; }
uint32_t QC_EntityCapacity(void) { return test_entity_capacity; }
qc_restore_status_t QC_SetSaveSelection(const uint8_t *bitmap, qc_byte_count_t size)
{
	assert(bitmap != NULL);
	assert(size == 1U);
	++selection_calls;
	saved_selection = bitmap[0];
	return QC_RESTORE_OK;
}
qc_byte_count_t QC_SaveGuest(uint8_t *out, qc_byte_count_t capacity)
{
	(void)out;
	(void)capacity;
	return 0U;
}
qc_restore_status_t QC_ValidateGuestRestore(const uint8_t *data, qc_byte_count_t size,
	const uint8_t *selection, qc_byte_count_t selection_size)
{
	assert(data != NULL);
	assert(size == 2U);
	assert(selection != NULL);
	assert(selection_size == 1U);
	assert(selection[0] == expected_validation_selection);
	++guest_validation_calls;
	return QC_RESTORE_OK;
}
qc_restore_status_t QC_RestoreGuest(const uint8_t *data, qc_byte_count_t size)
{
	assert(data != NULL);
	assert(size == 2U);
	++guest_restore_calls;
	return QC_RESTORE_OK;
}
qc_plugin_status_t QC_SaveEncode(const qc_save_image_t *image, uint8_t **out,
	uint32_t *size)
{
	(void)image;
	(void)out;
	(void)size;
	return QC_PLUGIN_BAD_ARGUMENT;
}
qc_plugin_status_t QC_SaveParse(const uint8_t *bytes, uint32_t size,
	qc_save_image_t **out)
{
	(void)bytes;
	(void)size;
	*out = NULL;
	return QC_PLUGIN_BAD_ARGUMENT;
}
void QC_SaveImageFree(qc_save_image_t *image) { (void)image; }
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

typedef struct byte_writer_s {
	uint8_t bytes[1024];
	uint32_t size;
} byte_writer_t;

static void write_u32(byte_writer_t *writer, uint32_t value)
{
	assert(writer->size + 4U <= sizeof(writer->bytes));
	writer->bytes[writer->size++] = (uint8_t)value;
	writer->bytes[writer->size++] = (uint8_t)(value >> 8U);
	writer->bytes[writer->size++] = (uint8_t)(value >> 16U);
	writer->bytes[writer->size++] = (uint8_t)(value >> 24U);
}

static void write_f32(byte_writer_t *writer, float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	write_u32(writer, bits);
}

static void write_f64(byte_writer_t *writer, double value)
{
	uint64_t bits;
	uint32_t index;
	memcpy(&bits, &value, sizeof(bits));
	assert(writer->size + 8U <= sizeof(writer->bytes));
	for (index = 0U; index < 8U; ++index) {
		writer->bytes[writer->size++] = (uint8_t)(bits >> (8U * index));
	}
}

static void write_resource(byte_writer_t *writer, const char *value)
{
	const uint32_t size = (uint32_t)strlen(value);
	assert(writer->size + 4U + size <= sizeof(writer->bytes));
	write_u32(writer, size);
	memcpy(writer->bytes + writer->size, value, size);
	writer->size += size;
}

static qc_save_image_t make_valid_image(byte_writer_t *engine, uint32_t world_flags,
	qbool connected_client, uint32_t client_flags)
{
	qc_save_image_t image = {0};
	uint32_t index;
	write_u32(engine, 1U);
	write_f64(engine, 42.5);
	write_u32(engine, 17U);
	write_u32(engine, MAX_LIGHTSTYLES);
	for (index = 0U; index < MAX_LIGHTSTYLES; ++index) write_resource(engine, "m");
	write_u32(engine, 0U);
	write_u32(engine, test_entity_capacity);
	write_u32(engine, world_flags);
	write_f32(engine, 0.0f);
	write_u32(engine, world_flags != 1U ? 0U : connected_client ? 1U : 2U);
	write_f32(engine, connected_client ? 0.0f : 3.0f);
	write_u32(engine, 0U);
	write_f32(engine, 0.0f);
	write_u32(engine, 0U);
	write_f32(engine, 0.0f);
	write_u32(engine, connected_client ? 1U : 0U);
	if (connected_client) {
		uint32_t parm;
		write_u32(engine, 0U);
		write_u32(engine, client_flags);
		for (parm = 0U; parm < NUM_SPAWN_PARMS; ++parm) write_f32(engine, (float)parm);
	}
	strcpy(image.metadata.logical_game, "game");
	strcpy(image.metadata.map_name, "e1m1");
	image.metadata.map_bsp_checksum = UINT32_C(0x12345678);
	image.metadata.entity_capacity = test_entity_capacity;
	image.metadata.contains_connected_clients = connected_client;
	image.engine_state = (qc_save_bytes_t){engine->bytes, engine->size};
	image.guest_payload = (qc_save_bytes_t){(uint8_t *)"OK", 2U};
	return image;
}

static void reset_live_engine(void)
{
	memset(&sv, 0, sizeof(sv));
	memset(&svs, 0, sizeof(svs));
	memset(&restore_test_globals, 0, sizeof(restore_test_globals));
	memset(&restore_shared_globals, 0, sizeof(restore_shared_globals));
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
	byte_writer_t engine = {0};
	qc_save_image_t image;
	reset_live_engine();
	image = make_valid_image(&engine, true, false, 0U);
	strcpy(image.metadata.map_name, "e1m2");
	assert(QC_ValidateSaveGame(&image) != QC_RESTORE_OK);
	assert(guest_validation_calls == 0U);
	assert(guest_restore_calls == 0U);
	assert(selection_calls == 0U);
	assert(link_calls == 0U);
	assert(sv.time == 7.0);
	assert(sv.edicts[0].e.free == false);
}

static void test_valid_image_validates_then_applies_in_place(void)
{
	byte_writer_t engine = {0};
	qc_save_image_t image;
	reset_live_engine();
	image = make_valid_image(&engine, true, false, 0U);
	assert(QC_ValidateSaveGame(&image) == QC_RESTORE_OK);
	assert(guest_validation_calls == 1U);
	assert(guest_restore_calls == 0U);
	assert(sv.time == 7.0);
	QC_ApplySaveGame(&image);
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

static void test_connected_restore_refreshes_client_replication(void)
{
	byte_writer_t engine = {0};
	qc_save_image_t image;
	reset_live_engine();
	svs.clients[0].state = cs_spawned;
	svs.clients[0].edict = &sv.edicts[1];
	svs.clients[0].delta_sequence = 23;
	expected_validation_selection = 3U;
	image = make_valid_image(&engine, true, true, 3U);
	assert(QC_ValidateSaveGame(&image) == QC_RESTORE_OK);
	QC_ApplySaveGame(&image);
	assert(client_replication_updates == 1U);
	assert(svs.clients[0].delta_sequence == -1);
}

static void test_rejects_a_save_without_an_active_world_slot(void)
{
	byte_writer_t engine = {0};
	qc_save_image_t image;
	reset_live_engine();
	expected_validation_selection = 0U;
	image = make_valid_image(&engine, false, false, 0U);
	assert(QC_ValidateSaveGame(&image) != QC_RESTORE_OK);
	assert(guest_validation_calls == 0U);
	assert(guest_restore_calls == 0U);
	assert(sv.edicts[0].e.free == false);
}

static void test_rejects_a_connected_save_when_the_client_lifecycle_changed(void)
{
	byte_writer_t engine = {0};
	qc_save_image_t image;
	reset_live_engine();
	svs.clients[0].state = cs_spawned;
	svs.clients[0].edict = &sv.edicts[1];
	expected_validation_selection = 3U;
	image = make_valid_image(&engine, true, true, 1U);
	assert(QC_ValidateSaveGame(&image) != QC_RESTORE_OK);
	assert(guest_validation_calls == 0U);
	assert(guest_restore_calls == 0U);
	assert(sv.time == 7.0);
}

int main(void)
{
	test_invalid_image_is_rejected_before_guest_or_host_mutation();
	test_valid_image_validates_then_applies_in_place();
	test_connected_restore_refreshes_client_replication();
	test_rejects_a_save_without_an_active_world_slot();
	test_rejects_a_connected_save_when_the_client_lifecycle_changed();
	return 0;
}
