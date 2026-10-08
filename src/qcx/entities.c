#include "qwsvdef.h"

#include "qcx/adapter.h"
#include "qcx/entities.h"
#include "qcx/engine_fields.h"
#include "qcx/layout_contract.h"
#include "qcx/strings.h"

#include <limits.h>
#include <string.h>

static qcx_game_entity_memory_v1_t *qcx_entity_memory;
static qcx_guest_address_t qcx_entity_base;
static uint32_t qcx_entity_stride;
static uint32_t qcx_entity_capacity;
static const char *qcx_entity_field_error;

typedef struct qcx_optional_field_spec_s {
	qcx_engine_field_spec_t field;
	int *offset;
} qcx_optional_field_spec_t;

int QCX_ConfigureEntities(qcx_guest_address_t publication_address)
{
	QCX_ClearEntities();
	const qcx_game_api_v1_t *game = QCX_Game();
	qcx_game_entity_memory_v1_t *memory = NULL;
	if (game == NULL || publication_address == 0U
		|| game->memory_view(game->context, publication_address, sizeof(*memory),
			_Alignof(qcx_game_entity_memory_v1_t), (void **)&memory) != QCX_PLUGIN_OK
		|| memory == NULL
		|| memory->abi_version != QCX_GAME_ENTITY_MEMORY_ABI_VERSION_V1
		|| memory->struct_size < sizeof(*memory)
		|| memory->shared_state_base == 0U
		|| memory->entity_object_base == 0U
		|| memory->shared_state_base != memory->entity_object_base
		|| memory->entity_stride < sizeof(entvars_t)
		|| memory->entity_stride % _Alignof(entvars_t) != 0U
		|| memory->max_entities == 0U
		|| memory->max_entities > MAX_EDICTS
		|| memory->shared_state_abi_version != QCX_SHARED_ENTITY_STATE_ABI_VERSION_V1
		|| memory->reserved0 != 0U) {
		return 0;
	}
	const uint64_t span = (uint64_t)memory->entity_stride * memory->max_entities;
	if (span > UINT32_MAX || memory->entity_object_base > UINT64_MAX - span) {
		return 0;
	}
	qcx_entity_memory = memory;
	qcx_entity_base = memory->entity_object_base;
	qcx_entity_stride = memory->entity_stride;
	qcx_entity_capacity = memory->max_entities;
	sv.max_edicts = (int)qcx_entity_capacity;
	return 1;
}

int QCX_BindEntities(void)
{
	const qcx_game_api_v1_t *const game = QCX_Game();
	const uint64_t span = (uint64_t)qcx_entity_stride * qcx_entity_capacity;
	void *storage = NULL;
	if (qcx_entity_memory == NULL || game == NULL || game->memory_view == NULL
		|| span > UINT32_MAX
		|| game->memory_view(game->context, qcx_entity_base, (qcx_byte_count_t)span,
			_Alignof(entvars_t), &storage) != QCX_PLUGIN_OK
		|| storage == NULL || sv.max_edicts <= 0
		|| (uint32_t)sv.max_edicts > qcx_entity_capacity) {
		return 0;
	}
	for (int slot = 0; slot < sv.max_edicts; ++slot) {
		entvars_t *const entity =
			(entvars_t *)((uint8_t *)storage
				+ (size_t)slot * qcx_entity_stride);
		sv.edicts[slot].v = entity;
	}
	return 1;
}

void QCX_ClearEntities(void)
{
	for (int slot = 0; slot < sv.max_edicts && slot < MAX_EDICTS; ++slot) {
		sv.edicts[slot].v = NULL;
	}
	qcx_entity_memory = NULL;
	qcx_entity_base = 0U;
	qcx_entity_stride = 0U;
	qcx_entity_capacity = 0U;
	qcx_entity_field_error = NULL;
}

entvars_t *QCX_Entity(qcx_entity_id_t slot)
{
	const qcx_game_api_v1_t *game = QCX_Game();
	if (qcx_entity_memory == NULL || game == NULL || slot >= qcx_entity_capacity) {
		return NULL;
	}
	const uint64_t offset = (uint64_t)slot * qcx_entity_stride;
	if (offset > UINT64_MAX - qcx_entity_base) {
		return NULL;
	}
	entvars_t *entity = NULL;
	if (game->memory_view(game->context, qcx_entity_base + offset, sizeof(*entity),
		_Alignof(entvars_t), (void **)&entity) != QCX_PLUGIN_OK) {
		return NULL;
	}
	return entity;
}

uint32_t QCX_EntityCapacity(void)
{
	return qcx_entity_capacity;
}

int QCX_ResolveEntityFields(void)
{
	qcx_entity_field_error = "entity field table";
	const qcx_game_api_v1_t *const game = QCX_Game();
	if (qcx_entity_memory == NULL || game == NULL || game->engine_fields == NULL) {
		return 0;
	}
	qcx_engine_field_exports_v1_t exports;
	qcx_engine_field_table_view_t table;
	if (!QCX_ReadEngineFieldExports(game, &exports)
		|| !QCX_OpenEngineFieldTable(game, &exports.entity_fields, &table)) {
		return 0;
	}
	qcx_optional_field_spec_t specs[] = {
		{{"items2", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_READ}, &fofs_items2},
		{{"maxspeed", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE}, &fofs_maxspeed},
		{{"gravity", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE}, &fofs_gravity},
		{{"movement", "qc.vec3f", 3U * sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_WRITE}, &fofs_movement},
		{{"vw_index", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_READ}, &fofs_vw_index},
		{{"hideentity", "qc.entity", sizeof(uint32_t), _Alignof(uint32_t), QCX_ENGINE_FIELD_HOST_READ}, &fofs_hideentity},
		{{"trackent", "qc.entity", sizeof(uint32_t), _Alignof(uint32_t), QCX_ENGINE_FIELD_HOST_READ}, &fofs_trackent},
		{{"visclients", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE}, &fofs_visibility},
		{{"hideplayers", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_READ}, &fofs_hide_players},
		{{"teleported", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE}, &fofs_teleported},
	};
	int offsets[sizeof(specs) / sizeof(specs[0])] = {0};
	for (size_t spec_index = 0U; spec_index < sizeof(specs) / sizeof(specs[0]); ++spec_index) {
		qcx_optional_field_spec_t *const spec = &specs[spec_index];
		qcx_engine_field_binding_t binding;
		const qcx_engine_field_result_t result = QCX_ResolveEngineField(&table, &spec->field,
			qcx_entity_base, qcx_entity_stride, &binding);
		/* Offset zero is MVDSV's sentinel for an absent optional entity field. */
		if (result == QCX_ENGINE_FIELD_INVALID
			|| (result == QCX_ENGINE_FIELD_FOUND && (binding.offset == 0U || binding.offset > INT_MAX))) {
			qcx_entity_field_error = spec->field.name;
			return 0;
		}
		offsets[spec_index] = (int)binding.offset;
	}
	for (size_t index = 0U; index < sizeof(specs) / sizeof(specs[0]); ++index) {
		*specs[index].offset = offsets[index];
	}
	qcx_entity_field_error = NULL;
	return 1;
}

const char *QCX_EntityFieldError(void)
{
	return qcx_entity_field_error == NULL ? "entity field table" : qcx_entity_field_error;
}

int QCX_EntityStringHeaderAddress(qcx_legacy_string_ref_t ref,
	qcx_guest_address_t *header)
{
	const qcx_transport_kind_t kind = QCX_GameTransportKind();
	const uint32_t header_size = kind == QCX_TRANSPORT_NATIVE
		? sizeof(char *) + sizeof(uint32_t) : sizeof(uint32_t) * 2U;
	const uint64_t span = (uint64_t)qcx_entity_stride * qcx_entity_capacity;
	const uint64_t offset = ref > 0 ? (uint32_t)ref : 0U;
	if (header == NULL || ref <= 0 || qcx_entity_memory == NULL
		|| (kind != QCX_TRANSPORT_NATIVE && kind != QCX_TRANSPORT_WASM)
		|| offset > span || header_size > span - offset
		|| qcx_entity_base > UINT64_MAX - offset) {
		return 0;
	}
	*header = qcx_entity_base + offset;
	return 1;
}

qbool QCX_EntityHasModel(const edict_t *entity)
{
	qcx_string_view_t view;
	if (entity == NULL || entity->v == NULL
		|| !QCX_ReadLegacyString(entity->v->model, &view)) {
		return false;
	}
	return view.size != 0U;
}

qcx_entity_id_t QCX_EdictToSlot(const edict_t *edict)
{
	const uintptr_t first = (uintptr_t)&sv.edicts[0];
	const uintptr_t address = (uintptr_t)edict;
	if (qcx_entity_memory == NULL || edict == NULL || address < first
		|| (address - first) % sizeof(sv.edicts[0]) != 0U) {
		return QCX_INVALID_ENTITY_ID;
	}
	const uintptr_t slot = (address - first) / sizeof(sv.edicts[0]);
	if (slot >= qcx_entity_capacity || slot >= (uintptr_t)sv.max_edicts) {
		return QCX_INVALID_ENTITY_ID;
	}
	return (qcx_entity_id_t)slot;
}

edict_t *QCX_SlotToEdict(qcx_entity_id_t slot)
{
	if (qcx_entity_memory == NULL || slot >= qcx_entity_capacity
		|| slot >= (uint32_t)sv.max_edicts) {
		return NULL;
	}
	return &sv.edicts[slot];
}

void QCX_ClearEdict(edict_t *edict)
{
	const qcx_game_api_v1_t *game = QCX_Game();
	const qcx_entity_id_t slot = QCX_EdictToSlot(edict);
	if (game != NULL && slot != QCX_INVALID_ENTITY_ID) {
		game->clear_edict(game->context, slot);
	}
}

int QCX_SetEntityString(edict_t *edict, const char *field, const char *value)
{
	const qcx_game_api_v1_t *game = QCX_Game();
	const qcx_entity_id_t slot = QCX_EdictToSlot(edict);
	if (game == NULL || slot == QCX_INVALID_ENTITY_ID || field == NULL || value == NULL) {
		return 0;
	}
	const size_t field_size = strlen(field);
	const size_t value_size = strlen(value);
	if (field_size > UINT32_MAX || value_size > UINT32_MAX) {
		return 0;
	}
	return game->string_write(game->context, QCX_SCOPE_ENTITY, slot,
		(const uint8_t *)field, (qcx_byte_count_t)field_size,
		(const uint8_t *)value, (qcx_byte_count_t)value_size) == QCX_PLUGIN_OK;
}

const char *QCX_EntityStringFieldName(const edict_t *edict, const void *member)
{
	if (edict == NULL || edict->v == NULL || member == NULL) {
		return NULL;
	}
#define QCX_STRING_FIELD(name) if (member == &edict->v->name) return #name
	QCX_STRING_FIELD(classname);
	QCX_STRING_FIELD(model);
	QCX_STRING_FIELD(weaponmodel);
	QCX_STRING_FIELD(netname);
	QCX_STRING_FIELD(target);
	QCX_STRING_FIELD(targetname);
	QCX_STRING_FIELD(message);
	QCX_STRING_FIELD(noise);
	QCX_STRING_FIELD(noise1);
	QCX_STRING_FIELD(noise2);
	QCX_STRING_FIELD(noise3);
#undef QCX_STRING_FIELD
	return NULL;
}
