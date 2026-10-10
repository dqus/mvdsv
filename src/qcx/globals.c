#include "qcx/globals.h"
#include "qcx/engine_fields.h"
#include "qcx/layout_contract.h"

#include "qwsvdef.h"
#include "game/plugin_api.h"

#include <string.h>

const qcx_game_api_v1_t *QCX_Game(void);

static globalvars_t *qcx_globals;
static int qcx_globals_available = 1;
static qcx_guest_address_t qcx_global_object_base;
static qcx_byte_count_t qcx_global_object_size;
static globalvars_t *qcx_previous_global_struct;
static float *qcx_previous_globals;
static qbool qcx_globals_bound;

globalvars_t *QCX_Globals(void)
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
	globalvars_t *globals = NULL;
	if (game->memory_view(game->context, memory->shared_state_base, sizeof(*globals),
		_Alignof(globalvars_t), (void **)&globals) != QCX_PLUGIN_OK
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
	qcx_engine_field_exports_v1_t fields;
	qcx_engine_field_table_view_t table;
	if (!QCX_ReadEngineFieldExports(game, &fields)
		|| fields.globals_base != qcx_global_object_base
		|| fields.globals_size != qcx_global_object_size
		|| !QCX_OpenEngineFieldTable(game, &fields.global_fields, &table)) {
		return 0;
	}
	const qcx_engine_field_spec_t specs[] = {
		{"deathmatch", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_WRITE},
		{"coop", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_WRITE},
		{"teamplay", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_WRITE},
	};
	qcx_engine_field_binding_t bindings[3];
	for (size_t index = 0U; index < sizeof(specs) / sizeof(specs[0]); ++index) {
		const qcx_engine_field_result_t result = QCX_ResolveEngineField(&table, &specs[index],
			qcx_global_object_base, qcx_global_object_size, &bindings[index]);
		if (result == QCX_ENGINE_FIELD_INVALID
			|| (result == QCX_ENGINE_FIELD_ABSENT && index != 1U)) {
			return 0;
		}
	}
	float *const deathmatch_global = bindings[0].address;
	float *const coop_global = bindings[1].address;
	float *const teamplay_global = bindings[2].address;
	*deathmatch_global = deathmatch;
	if (coop_global != NULL) {
		*coop_global = coop;
	}
	*teamplay_global = teamplay;
	if (!qcx_globals_bound) {
		qcx_previous_global_struct = pr_global_struct;
		qcx_previous_globals = pr_globals;
		pr_global_struct = qcx_globals;
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
