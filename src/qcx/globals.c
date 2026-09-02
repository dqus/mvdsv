#include "qcx/globals.h"

#include "qwsvdef.h"
#include "game/plugin_api.h"

#include <limits.h>
#include <string.h>

const qcx_game_api_v1_t *QCX_Game(void);

static qcx_shared_global_state_v1_t *qcx_globals;
static int qcx_globals_available = 1;
static qcx_guest_address_t qcx_global_object_base;
static qcx_byte_count_t qcx_global_object_size;
static float *qcx_deathmatch;
static float *qcx_coop;
static float *qcx_teamplay;
static globalvars_t *qcx_previous_global_struct;
static float *qcx_previous_globals;
static qbool qcx_globals_bound;

static qcx_engine_type_id_t QCX_EngineTypeId(const char *name)
{
	qcx_engine_type_id_t result = UINT64_C(14695981039346656037);
	while (*name != '\0') {
		result ^= (uint8_t)*name++;
		result *= UINT64_C(1099511628211);
	}
	return result;
}

static float *QCX_ResolveGlobalFloat(const qcx_game_api_v1_t *game,
	const qcx_engine_field_table_v1_t *table, const char *name)
{
	if (table->count == 0U || table->descriptors == 0U
		|| table->descriptor_stride < sizeof(qcx_engine_field_descriptor_v1_t)
		|| table->descriptor_stride % _Alignof(qcx_engine_field_descriptor_v1_t) != 0U) {
		return NULL;
	}
	const uint64_t last = (uint64_t)(table->count - 1U) * table->descriptor_stride;
	const uint64_t envelope = last + sizeof(qcx_engine_field_descriptor_v1_t);
	if (last > UINT32_MAX || envelope > UINT32_MAX) {
		return NULL;
	}
	void *table_view = NULL;
	if (game->memory_view(game->context, table->descriptors, (qcx_byte_count_t)envelope,
		_Alignof(qcx_engine_field_descriptor_v1_t), &table_view) != QCX_PLUGIN_OK
		|| table_view == NULL) {
		return NULL;
	}
	float *result = NULL;
	for (uint32_t index = 0U; index < table->count; ++index) {
		const qcx_engine_field_descriptor_v1_t *descriptor =
			(const qcx_engine_field_descriptor_v1_t *)((const uint8_t *)table_view
				+ (size_t)index * table->descriptor_stride);
		if (descriptor->name.data == 0U || descriptor->name.size == 0U
			|| descriptor->name.reserved0 != 0U || descriptor->type_id != QCX_EngineTypeId("qc.f32")
			|| descriptor->size != sizeof(float) || descriptor->alignment != _Alignof(float)
			|| descriptor->offset % descriptor->alignment != 0U
			|| (descriptor->access_flags & QCX_ENGINE_FIELD_HOST_WRITE) == 0U
			|| qcx_global_object_size < descriptor->size
			|| descriptor->offset > qcx_global_object_size - descriptor->size
			|| qcx_global_object_base > UINT64_MAX - descriptor->offset) {
			continue;
		}
		void *name_view = NULL;
		if (game->memory_view(game->context, descriptor->name.data, descriptor->name.size,
			1U, &name_view) != QCX_PLUGIN_OK || name_view == NULL
			|| strlen(name) != descriptor->name.size
			|| memcmp(name_view, name, descriptor->name.size) != 0) {
			continue;
		}
		void *field_view = NULL;
		if (game->memory_view(game->context, qcx_global_object_base + descriptor->offset,
			descriptor->size, descriptor->alignment, &field_view) != QCX_PLUGIN_OK
			|| field_view == NULL || result != NULL) {
			return NULL;
		}
		result = field_view;
	}
	return result;
}

qcx_shared_global_state_v1_t *QCX_Globals(void)
{
	if (!qcx_globals_available) {
		return NULL;
	}
	if (qcx_globals != NULL) {
		return qcx_globals;
	}
	const qcx_game_api_v1_t *game = QCX_Game();
	if (game == NULL) {
		return NULL;
	}
	const qcx_guest_address_t address = game->global_memory(game->context);
	qcx_game_global_memory_v1_t *memory = NULL;
	if (address == 0U
		|| game->memory_view(game->context, address, sizeof(*memory),
			_Alignof(qcx_game_global_memory_v1_t), (void **)&memory) != QCX_PLUGIN_OK
		|| memory == NULL
		|| memory->abi_version != QCX_GAME_GLOBAL_MEMORY_ABI_VERSION_V1
		|| memory->struct_size < sizeof(*memory)
		|| memory->shared_state_base != memory->global_object_base
		|| memory->shared_state_size < sizeof(*qcx_globals)
		|| memory->global_object_size < memory->shared_state_size
		|| memory->shared_state_abi_version != QCX_SHARED_GLOBAL_STATE_ABI_VERSION_V1) {
		return NULL;
	}
	qcx_shared_global_state_v1_t *globals = NULL;
	if (game->memory_view(game->context, memory->shared_state_base, sizeof(*globals),
		_Alignof(qcx_shared_global_state_v1_t), (void **)&globals) != QCX_PLUGIN_OK
		|| globals == NULL) {
		return NULL;
	}
	qcx_globals = globals;
	qcx_global_object_base = memory->global_object_base;
	qcx_global_object_size = memory->global_object_size;
	return qcx_globals;
}

int QCX_ConfigureGlobals(float deathmatch, float coop, float teamplay)
{
	qcx_globals_available = 1;
	const qcx_game_api_v1_t *game = QCX_Game();
	if (game == NULL || QCX_Globals() == NULL) {
		return 0;
	}
	const qcx_guest_address_t address = game->engine_fields(game->context);
	qcx_engine_field_exports_v1_t *fields = NULL;
	if (address == 0U
		|| game->memory_view(game->context, address, sizeof(*fields),
			_Alignof(qcx_engine_field_exports_v1_t), (void **)&fields) != QCX_PLUGIN_OK
		|| fields == NULL
		|| fields->abi_version != QCX_ENGINE_FIELD_EXPORTS_ABI_VERSION_V1
		|| fields->struct_size < sizeof(*fields)
		|| fields->globals_base != qcx_global_object_base
		|| fields->globals_size != qcx_global_object_size) {
		return 0;
	}
	qcx_deathmatch = QCX_ResolveGlobalFloat(game, &fields->global_fields, "deathmatch");
	qcx_coop = QCX_ResolveGlobalFloat(game, &fields->global_fields, "coop");
	qcx_teamplay = QCX_ResolveGlobalFloat(game, &fields->global_fields, "teamplay");
	if (qcx_deathmatch == NULL || qcx_teamplay == NULL) {
		return 0;
	}
	*qcx_deathmatch = deathmatch;
	if (qcx_coop != NULL) {
		*qcx_coop = coop;
	}
	*qcx_teamplay = teamplay;
	if (!qcx_globals_bound) {
		qcx_previous_global_struct = pr_global_struct;
		qcx_previous_globals = pr_globals;
		pr_global_struct = (globalvars_t *)qcx_globals;
		pr_globals = (float *)qcx_globals;
		qcx_globals_bound = true;
	}
	return 1;
}

void QCX_ClearGlobals(void)
{
	if (qcx_globals_bound) {
		pr_global_struct = qcx_previous_global_struct;
		pr_globals = qcx_previous_globals;
		qcx_previous_global_struct = NULL;
		qcx_previous_globals = NULL;
		qcx_globals_bound = false;
	}
	qcx_globals = NULL;
	qcx_globals_available = 0;
	qcx_global_object_base = 0U;
	qcx_global_object_size = 0U;
	qcx_deathmatch = NULL;
	qcx_coop = NULL;
	qcx_teamplay = NULL;
}

int QCX_SetMapName(const char *mapname)
{
	const qcx_game_api_v1_t *game = QCX_Game();
	static const uint8_t name[] = "mapname";
	if (game == NULL || mapname == NULL) {
		return 0;
	}
	return game->string_write(game->context, QCX_SCOPE_GLOBAL, 0U, name,
		sizeof(name) - 1U, (const uint8_t *)mapname,
		(qcx_byte_count_t)strlen(mapname)) == QCX_PLUGIN_OK;
}
