#include "game/plugin_api.h"

static qc_guest_address_t fixture_init(void *context, int32_t level_msec,
	uint32_t random_seed) { (void)context; (void)level_msec; (void)random_seed; return 0U; }
static void fixture_shutdown(void *context) { (void)context; }
static qc_guest_address_t fixture_address(void *context) { (void)context; return 0U; }
static void fixture_load_entities(void *context, const uint8_t *data, qc_byte_count_t size)
{ (void)context; (void)data; (void)size; }
static void fixture_start_frame(void *context, float time, float frametime, uint32_t bot_frame)
{ (void)context; (void)time; (void)frametime; (void)bot_frame; }
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
{ (void)context; (void)scope; (void)entity; (void)name; (void)name_size; (void)out; (void)capacity; if (required != NULL) *required = 0U; return QC_PLUGIN_OK; }
static qc_plugin_status_t fixture_string_write(void *context, qc_object_scope_t scope, qc_entity_id_t entity, const uint8_t *name, qc_byte_count_t name_size, const uint8_t *bytes, qc_byte_count_t size)
{ (void)context; (void)scope; (void)entity; (void)name; (void)name_size; (void)bytes; (void)size; return QC_PLUGIN_OK; }
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
{ (void)context; (void)address; (void)size; (void)alignment; if (out_view != NULL) *out_view = NULL; return QC_PLUGIN_OK; }

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
		.engine_fields = fixture_address, .global_memory = fixture_address,
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
