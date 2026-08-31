#include "game/plugin_api.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct fixture_global_object_s {
	qc_shared_global_state_v1_t shared;
	float deathmatch;
	float coop;
	float teamplay;
} fixture_global_object_t;

static fixture_global_object_t fixture_globals;
static uint8_t fixture_mapname[64];
static qc_byte_count_t fixture_mapname_size;
static const uint8_t fixture_deathmatch_name[] = "deathmatch";
static const uint8_t fixture_coop_name[] = "coop";
static const uint8_t fixture_teamplay_name[] = "teamplay";
static qc_engine_field_descriptor_v1_t fixture_global_fields[3];
static qc_engine_field_exports_v1_t fixture_engine_fields = {
	.abi_version =
#if defined(QC2CPP_FIXTURE_BAD_ENGINE_ABI)
		QC_ENGINE_FIELD_EXPORTS_ABI_VERSION_V1 + 1U,
#else
		QC_ENGINE_FIELD_EXPORTS_ABI_VERSION_V1,
#endif
	.struct_size = sizeof(fixture_engine_fields),
	.globals_base = (qc_guest_address_t)(uintptr_t)&fixture_globals,
	.globals_size = sizeof(fixture_globals),
	.global_fields = {
		.descriptors = (qc_guest_address_t)(uintptr_t)fixture_global_fields,
		.count = 3U,
		.descriptor_stride = sizeof(fixture_global_fields[0]),
	},
};
static qc_game_global_memory_v1_t fixture_global_memory = {
	.abi_version = QC_GAME_GLOBAL_MEMORY_ABI_VERSION_V1,
	.struct_size = sizeof(fixture_global_memory),
	.shared_state_base = (qc_guest_address_t)(uintptr_t)&fixture_globals.shared,
	.global_object_base = (qc_guest_address_t)(uintptr_t)&fixture_globals,
	.global_object_size =
#if defined(QC2CPP_FIXTURE_BAD_GLOBAL_SIZE)
		sizeof(fixture_globals.shared),
#else
		sizeof(fixture_globals),
#endif
	.shared_state_size = sizeof(fixture_globals.shared),
	.shared_state_abi_version = QC_SHARED_GLOBAL_STATE_ABI_VERSION_V1,
};

static qc_engine_type_id_t fixture_engine_type_id(const char *name)
{
	qc_engine_type_id_t result = UINT64_C(14695981039346656037);
	while (*name != '\0') {
		result ^= (uint8_t)*name++;
		result *= UINT64_C(1099511628211);
	}
	return result;
}

static void fixture_configure_global_fields(void)
{
	fixture_global_fields[0] = (qc_engine_field_descriptor_v1_t){
		.name = {(qc_guest_address_t)(uintptr_t)fixture_deathmatch_name,
			sizeof(fixture_deathmatch_name) - 1U, 0U},
		.type_id = fixture_engine_type_id("qc.f32"),
		.offset =
#if defined(QC2CPP_FIXTURE_BAD_ENGINE_OFFSET)
		sizeof(fixture_globals),
#else
		offsetof(fixture_global_object_t, deathmatch),
#endif
		.size = sizeof(fixture_globals.deathmatch),
		.alignment = _Alignof(float),
		.access_flags = QC_ENGINE_FIELD_HOST_READ | QC_ENGINE_FIELD_HOST_WRITE,
	};
	fixture_global_fields[1] = (qc_engine_field_descriptor_v1_t){
		.name = {(qc_guest_address_t)(uintptr_t)fixture_coop_name,
			sizeof(fixture_coop_name) - 1U, 0U},
		.type_id = fixture_engine_type_id("qc.f32"),
		.offset = offsetof(fixture_global_object_t, coop),
		.size = sizeof(fixture_globals.coop),
		.alignment = _Alignof(float),
		.access_flags = QC_ENGINE_FIELD_HOST_READ | QC_ENGINE_FIELD_HOST_WRITE,
	};
	fixture_global_fields[2] = (qc_engine_field_descriptor_v1_t){
		.name = {(qc_guest_address_t)(uintptr_t)fixture_teamplay_name,
			sizeof(fixture_teamplay_name) - 1U, 0U},
		.type_id = fixture_engine_type_id("qc.f32"),
		.offset = offsetof(fixture_global_object_t, teamplay),
		.size = sizeof(fixture_globals.teamplay),
		.alignment = _Alignof(float),
		.access_flags = QC_ENGINE_FIELD_HOST_READ | QC_ENGINE_FIELD_HOST_WRITE,
	};
}

static qc_guest_address_t fixture_init(void *context, int32_t level_msec,
	uint32_t random_seed) { (void)context; (void)level_msec; (void)random_seed; fixture_configure_global_fields(); return 1U; }
static void fixture_shutdown(void *context) { (void)context; }
static qc_guest_address_t fixture_address(void *context) { (void)context; return 0U; }
static qc_guest_address_t fixture_global_memory_address(void *context)
{ (void)context; return (qc_guest_address_t)(uintptr_t)&fixture_global_memory; }
static qc_guest_address_t fixture_engine_fields_address(void *context)
{ (void)context; return (qc_guest_address_t)(uintptr_t)&fixture_engine_fields; }
static void fixture_load_entities(void *context, const uint8_t *data, qc_byte_count_t size)
{ (void)context; (void)data; (void)size; }
static void fixture_start_frame(void *context, float time, float frametime, uint32_t bot_frame)
{ (void)context; fixture_globals.shared.time = time; fixture_globals.shared.frametime = frametime; fixture_globals.shared.parm1 = fixture_globals.deathmatch; fixture_globals.shared.parm2 = fixture_globals.coop; fixture_globals.shared.parm3 = fixture_globals.teamplay; (void)bot_frame; }
static void fixture_client_flag(void *context, qc_entity_id_t self, uint32_t spectator)
{ (void)context; (void)self; (void)spectator; }
static uint32_t fixture_client_flag_result(void *context, qc_entity_id_t self, uint32_t value)
{ (void)context; (void)self; (void)value; return 0U; }
static uint32_t fixture_client_command(void *context, qc_entity_id_t self)
{ (void)context; (void)self; return 0U; }
static void fixture_client_think(void *context, qc_entity_id_t self, float time, float frametime, uint32_t spectator)
{ (void)context; (void)self; (void)time; (void)frametime; (void)spectator; }
static void fixture_spectator_think(void *context, qc_entity_id_t self, float time, float frametime)
{ (void)context; (void)self; (void)time; (void)frametime; }
static void fixture_client_kill(void *context, qc_entity_id_t self) { (void)context; (void)self; }
static void fixture_set_change_parms(void *context, qc_entity_id_t self, float out_parms[16])
{ (void)context; (void)self; (void)out_parms; }
static void fixture_set_new_parms(void *context, float out_parms[16]) { (void)context; (void)out_parms; }
static uint32_t fixture_console_command(void *context, qc_entity_id_t self, qc_entity_id_t other)
{ (void)context; (void)self; (void)other; return 0U; }
static void fixture_edict_event(void *context, qc_entity_id_t first, qc_entity_id_t second, float time, float frametime)
{ (void)context; (void)first; (void)second; (void)time; (void)frametime; }
static void fixture_edict_think(void *context, qc_entity_id_t self, float time, float frametime)
{ (void)context; (void)self; (void)time; (void)frametime; }
static uint32_t fixture_client_say(void *context, qc_entity_id_t self, uint32_t team, const uint8_t *text, qc_byte_count_t size)
{ (void)context; (void)self; (void)team; (void)text; (void)size; return 0U; }
static void fixture_paused_tic(void *context, uint32_t duration_msec) { (void)context; (void)duration_msec; }
static void fixture_clear_edict(void *context, qc_entity_id_t self) { (void)context; (void)self; }
static uint32_t fixture_edict_csqc_send(void *context, qc_entity_id_t self, qc_entity_id_t other, uint32_t flags)
{ (void)context; (void)self; (void)other; (void)flags; return 0U; }
static qc_plugin_status_t fixture_string_read(void *context, qc_object_scope_t scope, qc_entity_id_t entity, const uint8_t *name, qc_byte_count_t name_size, uint8_t *out, qc_byte_count_t capacity, qc_byte_count_t *required)
{
	(void)context;
	if (scope != QC_SCOPE_GLOBAL || entity != 0U || name_size != 7U
		|| memcmp(name, "mapname", 7U) != 0) return QC_PLUGIN_UNAVAILABLE;
	if (required != NULL) *required = fixture_mapname_size;
	if (capacity < fixture_mapname_size) return QC_PLUGIN_BUFFER_TOO_SMALL;
	if (fixture_mapname_size != 0U) memcpy(out, fixture_mapname, fixture_mapname_size);
	return QC_PLUGIN_OK;
}
static qc_plugin_status_t fixture_string_write(void *context, qc_object_scope_t scope, qc_entity_id_t entity, const uint8_t *name, qc_byte_count_t name_size, const uint8_t *bytes, qc_byte_count_t size)
{
	(void)context;
	if (scope != QC_SCOPE_GLOBAL || entity != 0U || name_size != 7U
		|| memcmp(name, "mapname", 7U) != 0 || size > sizeof(fixture_mapname)) return QC_PLUGIN_BAD_ARGUMENT;
	if (size != 0U) memcpy(fixture_mapname, bytes, size);
	fixture_mapname_size = size;
	return QC_PLUGIN_OK;
}
static qc_plugin_status_t fixture_legacy_string_read(void *context, int32_t token, uint8_t *out, qc_byte_count_t capacity, qc_byte_count_t *required)
{ (void)context; (void)token; (void)out; (void)capacity; if (required != NULL) *required = 0U; return QC_PLUGIN_OK; }
static qc_restore_status_t fixture_selection(void *context, const uint8_t *bitmap, qc_byte_count_t size)
{ (void)context; (void)bitmap; (void)size; return QC_RESTORE_OK; }
static qc_byte_count_t fixture_save(void *context, uint8_t *out, qc_byte_count_t capacity)
{ (void)context; (void)out; (void)capacity; return 0U; }
static qc_restore_status_t fixture_restore(void *context, const uint8_t *data, qc_byte_count_t size, const uint8_t *selection, qc_byte_count_t selection_size)
{ (void)context; (void)data; (void)size; (void)selection; (void)selection_size; return QC_RESTORE_OK; }
static qc_restore_status_t fixture_validate_restore(void *context, const uint8_t *data, qc_byte_count_t size)
{ (void)context; (void)data; (void)size; return QC_RESTORE_OK; }
static qc_plugin_status_t fixture_memory_view(void *context, qc_guest_address_t address, qc_byte_count_t size, uint32_t alignment, void **out_view)
{
	(void)context;
	if (out_view == NULL || alignment == 0U) return QC_PLUGIN_BAD_ARGUMENT;
	*out_view = NULL;
	if (address == (qc_guest_address_t)(uintptr_t)&fixture_global_memory
		&& size <= sizeof(fixture_global_memory)) {
		*out_view = &fixture_global_memory;
		return QC_PLUGIN_OK;
	}
	if (address == (qc_guest_address_t)(uintptr_t)&fixture_engine_fields
		&& size <= sizeof(fixture_engine_fields)) {
		*out_view = &fixture_engine_fields;
		return QC_PLUGIN_OK;
	}
	if (address == (qc_guest_address_t)(uintptr_t)fixture_global_fields
		&& size <= sizeof(fixture_global_fields)) {
		*out_view = fixture_global_fields;
		return QC_PLUGIN_OK;
	}
	if ((address == (qc_guest_address_t)(uintptr_t)fixture_deathmatch_name
		&& size <= sizeof(fixture_deathmatch_name) - 1U)
		|| (address == (qc_guest_address_t)(uintptr_t)fixture_coop_name
		&& size <= sizeof(fixture_coop_name) - 1U)
		|| (address == (qc_guest_address_t)(uintptr_t)fixture_teamplay_name
		&& size <= sizeof(fixture_teamplay_name) - 1U)) {
		*out_view = (void *)(uintptr_t)address;
		return QC_PLUGIN_OK;
	}
	const qc_guest_address_t globals_base = (qc_guest_address_t)(uintptr_t)&fixture_globals;
	if (address >= globals_base && address - globals_base <= sizeof(fixture_globals)
		&& size <= sizeof(fixture_globals) - (address - globals_base)) {
		*out_view = (uint8_t *)&fixture_globals + (address - globals_base);
		return QC_PLUGIN_OK;
	}
	return QC_PLUGIN_UNAVAILABLE;
}

qc_plugin_status_t qc_game_plugin_query_v1(const qc_host_api_v1_t *host,
	qc_game_api_v1_t *game)
{
	(void)host;
	if (game == NULL) return QC_PLUGIN_BAD_ARGUMENT;
	*game = (qc_game_api_v1_t){
		.abi_version =
#if defined(QC2CPP_FIXTURE_BAD_ABI)
			QC_PLUGIN_ABI_VERSION_V1 + 1U,
#else
			QC_PLUGIN_ABI_VERSION_V1,
#endif
		.struct_size = sizeof(*game),
		.init = fixture_init, .shutdown = fixture_shutdown,
		.engine_fields = fixture_engine_fields_address, .global_memory = fixture_global_memory_address,
		.load_entities = fixture_load_entities, .start_frame = fixture_start_frame,
		.client_connect = fixture_client_flag, .put_client_in_server = fixture_client_flag,
		.client_userinfo_changed = fixture_client_flag_result,
		.client_disconnect = fixture_client_flag, .client_command = fixture_client_command,
		.client_prethink = fixture_client_think, .client_postthink = fixture_client_think,
		.client_kill = fixture_client_kill, .spectator_think = fixture_spectator_think,
		.set_change_parms = fixture_set_change_parms, .set_new_parms = fixture_set_new_parms,
		.console_command = fixture_console_command, .edict_touch = fixture_edict_event,
		.edict_think = fixture_edict_think, .edict_blocked = fixture_edict_event,
		.client_say = fixture_client_say, .paused_tic = fixture_paused_tic,
		.clear_edict = fixture_clear_edict, .edict_csqc_send = fixture_edict_csqc_send,
		.string_read = fixture_string_read, .string_write = fixture_string_write,
		.legacy_string_read = fixture_legacy_string_read,
		.set_save_selection = fixture_selection, .save = fixture_save,
		.validate_restore = fixture_restore, .restore = fixture_validate_restore,
		.memory_view = fixture_memory_view,
	};
	return QC_PLUGIN_OK;
}
