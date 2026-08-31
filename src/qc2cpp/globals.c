#include "qc2cpp/globals.h"

#include "game/plugin_api.h"

#include <limits.h>
#include <string.h>

const qc_game_api_v1_t *QC_Game(void);

static qc_shared_global_state_v1_t *qc_globals;
static int qc_globals_available = 1;
static qc_guest_address_t qc_global_object_base;
static qc_byte_count_t qc_global_object_size;
static float *qc_deathmatch;
static float *qc_coop;
static float *qc_teamplay;

static qc_engine_type_id_t QC_EngineTypeId(const char *name)
{
	qc_engine_type_id_t result = UINT64_C(14695981039346656037);
	while (*name != '\0') {
		result ^= (uint8_t)*name++;
		result *= UINT64_C(1099511628211);
	}
	return result;
}

static float *QC_ResolveGlobalFloat(const qc_game_api_v1_t *game,
	const qc_engine_field_table_v1_t *table, const char *name)
{
	if (table->count == 0U || table->descriptors == 0U
		|| table->descriptor_stride < sizeof(qc_engine_field_descriptor_v1_t)
		|| table->descriptor_stride % _Alignof(qc_engine_field_descriptor_v1_t) != 0U) {
		return NULL;
	}
	const uint64_t last = (uint64_t)(table->count - 1U) * table->descriptor_stride;
	const uint64_t envelope = last + sizeof(qc_engine_field_descriptor_v1_t);
	if (last > UINT32_MAX || envelope > UINT32_MAX) {
		return NULL;
	}
	void *table_view = NULL;
	if (game->memory_view(game->context, table->descriptors, (qc_byte_count_t)envelope,
		_Alignof(qc_engine_field_descriptor_v1_t), &table_view) != QC_PLUGIN_OK
		|| table_view == NULL) {
		return NULL;
	}
	float *result = NULL;
	for (uint32_t index = 0U; index < table->count; ++index) {
		const qc_engine_field_descriptor_v1_t *descriptor =
			(const qc_engine_field_descriptor_v1_t *)((const uint8_t *)table_view
				+ (size_t)index * table->descriptor_stride);
		if (descriptor->name.data == 0U || descriptor->name.size == 0U
			|| descriptor->name.reserved0 != 0U || descriptor->type_id != QC_EngineTypeId("qc.f32")
			|| descriptor->size != sizeof(float) || descriptor->alignment != _Alignof(float)
			|| descriptor->offset % descriptor->alignment != 0U
			|| (descriptor->access_flags & QC_ENGINE_FIELD_HOST_WRITE) == 0U
			|| qc_global_object_size < descriptor->size
			|| descriptor->offset > qc_global_object_size - descriptor->size
			|| qc_global_object_base > UINT64_MAX - descriptor->offset) {
			continue;
		}
		void *name_view = NULL;
		if (game->memory_view(game->context, descriptor->name.data, descriptor->name.size,
			1U, &name_view) != QC_PLUGIN_OK || name_view == NULL
			|| strlen(name) != descriptor->name.size
			|| memcmp(name_view, name, descriptor->name.size) != 0) {
			continue;
		}
		void *field_view = NULL;
		if (game->memory_view(game->context, qc_global_object_base + descriptor->offset,
			descriptor->size, descriptor->alignment, &field_view) != QC_PLUGIN_OK
			|| field_view == NULL || result != NULL) {
			return NULL;
		}
		result = field_view;
	}
	return result;
}

qc_shared_global_state_v1_t *QC_Globals(void)
{
	if (!qc_globals_available) {
		return NULL;
	}
	if (qc_globals != NULL) {
		return qc_globals;
	}
	const qc_game_api_v1_t *game = QC_Game();
	if (game == NULL) {
		return NULL;
	}
	const qc_guest_address_t address = game->global_memory(game->context);
	qc_game_global_memory_v1_t *memory = NULL;
	if (address == 0U
		|| game->memory_view(game->context, address, sizeof(*memory),
			_Alignof(qc_game_global_memory_v1_t), (void **)&memory) != QC_PLUGIN_OK
		|| memory == NULL
		|| memory->abi_version != QC_GAME_GLOBAL_MEMORY_ABI_VERSION_V1
		|| memory->struct_size < sizeof(*memory)
		|| memory->shared_state_base != memory->global_object_base
		|| memory->shared_state_size < sizeof(*qc_globals)
		|| memory->global_object_size < memory->shared_state_size
		|| memory->shared_state_abi_version != QC_SHARED_GLOBAL_STATE_ABI_VERSION_V1) {
		return NULL;
	}
	qc_shared_global_state_v1_t *globals = NULL;
	if (game->memory_view(game->context, memory->shared_state_base, sizeof(*globals),
		_Alignof(qc_shared_global_state_v1_t), (void **)&globals) != QC_PLUGIN_OK
		|| globals == NULL) {
		return NULL;
	}
	qc_globals = globals;
	qc_global_object_base = memory->global_object_base;
	qc_global_object_size = memory->global_object_size;
	return qc_globals;
}

int QC_ConfigureGlobals(float deathmatch, float coop, float teamplay)
{
	qc_globals_available = 1;
	const qc_game_api_v1_t *game = QC_Game();
	if (game == NULL || QC_Globals() == NULL) {
		return 0;
	}
	const qc_guest_address_t address = game->engine_fields(game->context);
	qc_engine_field_exports_v1_t *fields = NULL;
	if (address == 0U
		|| game->memory_view(game->context, address, sizeof(*fields),
			_Alignof(qc_engine_field_exports_v1_t), (void **)&fields) != QC_PLUGIN_OK
		|| fields == NULL
		|| fields->abi_version != QC_ENGINE_FIELD_EXPORTS_ABI_VERSION_V1
		|| fields->struct_size < sizeof(*fields)
		|| fields->globals_base != qc_global_object_base
		|| fields->globals_size != qc_global_object_size) {
		return 0;
	}
	qc_deathmatch = QC_ResolveGlobalFloat(game, &fields->global_fields, "deathmatch");
	qc_coop = QC_ResolveGlobalFloat(game, &fields->global_fields, "coop");
	qc_teamplay = QC_ResolveGlobalFloat(game, &fields->global_fields, "teamplay");
	if (qc_deathmatch == NULL || qc_coop == NULL || qc_teamplay == NULL) {
		return 0;
	}
	*qc_deathmatch = deathmatch;
	*qc_coop = coop;
	*qc_teamplay = teamplay;
	return 1;
}

void QC_ClearGlobals(void)
{
	qc_globals = NULL;
	qc_globals_available = 0;
	qc_global_object_base = 0U;
	qc_global_object_size = 0U;
	qc_deathmatch = NULL;
	qc_coop = NULL;
	qc_teamplay = NULL;
}

int QC_SetMapName(const char *mapname)
{
	const qc_game_api_v1_t *game = QC_Game();
	static const uint8_t name[] = "mapname";
	if (game == NULL || mapname == NULL) {
		return 0;
	}
	return game->string_write(game->context, QC_SCOPE_GLOBAL, 0U, name,
		sizeof(name) - 1U, (const uint8_t *)mapname,
		(qc_byte_count_t)strlen(mapname)) == QC_PLUGIN_OK;
}
