#include "qwsvdef.h"

#include "qcx/adapter.h"
#include "qcx/entities.h"
#include "qcx/strings.h"

#include <limits.h>
#include <string.h>

static qcx_game_entity_memory_v1_t *qcx_entity_memory;
static qcx_guest_address_t qcx_entity_base;
static uint32_t qcx_entity_stride;
static uint32_t qcx_entity_capacity;
static uint32_t qcx_model_length_offset = UINT32_MAX;
static const char *qcx_entity_field_error;

static const char *const qcx_legacy_string_data_names[QCX_LEGACY_STRING_FIELD_COUNT] = {
	"qcx.classname.data", "qcx.model.data", "qcx.weaponmodel.data",
	"qcx.netname.data", "qcx.target.data", "qcx.targetname.data",
	"qcx.message.data", "qcx.noise.data", "qcx.noise1.data", "qcx.noise2.data",
	"qcx.noise3.data"
};
static const char *const qcx_legacy_string_length_names[QCX_LEGACY_STRING_FIELD_COUNT] = {
	"qcx.classname.length", "qcx.model.length", "qcx.weaponmodel.length",
	"qcx.netname.length", "qcx.target.length", "qcx.targetname.length",
	"qcx.message.length", "qcx.noise.length", "qcx.noise1.length",
	"qcx.noise2.length", "qcx.noise3.length"
};

typedef struct qcx_optional_field_spec_s {
	const char *name;
	const char *type_name;
	uint32_t size;
	uint32_t alignment;
	uint32_t required_access;
	int *offset;
} qcx_optional_field_spec_t;

static qcx_engine_type_id_t QCX_EngineTypeId(const char *name)
{
	qcx_engine_type_id_t result = UINT64_C(14695981039346656037);
	while (*name != '\0') {
		result ^= (uint8_t)*name++;
		result *= UINT64_C(1099511628211);
	}
	return result;
}

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
		|| memory->entity_stride < sizeof(qcx_shared_entity_state_v1_t)
		|| memory->entity_stride % _Alignof(qcx_shared_entity_state_v1_t) != 0U
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
	qcx_entity_base = memory->shared_state_base;
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
			_Alignof(qcx_shared_entity_state_v1_t), &storage) != QCX_PLUGIN_OK
		|| storage == NULL || sv.max_edicts <= 0
		|| (uint32_t)sv.max_edicts > qcx_entity_capacity) {
		return 0;
	}
	for (int slot = 0; slot < sv.max_edicts; ++slot) {
		qcx_shared_entity_state_v1_t *const entity =
			(qcx_shared_entity_state_v1_t *)((uint8_t *)storage
				+ (size_t)slot * qcx_entity_stride);
		sv.edicts[slot].v = (entvars_t *)entity;
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
	qcx_model_length_offset = UINT32_MAX;
	qcx_entity_field_error = NULL;
	QCX_ClearLegacyStringProjections();
}

qcx_shared_entity_state_v1_t *QCX_Entity(qcx_entity_id_t slot)
{
	const qcx_game_api_v1_t *game = QCX_Game();
	if (qcx_entity_memory == NULL || game == NULL || slot >= qcx_entity_capacity) {
		return NULL;
	}
	const uint64_t offset = (uint64_t)slot * qcx_entity_stride;
	if (offset > UINT64_MAX - qcx_entity_base) {
		return NULL;
	}
	qcx_shared_entity_state_v1_t *entity = NULL;
	if (game->memory_view(game->context, qcx_entity_base + offset, sizeof(*entity),
		_Alignof(qcx_shared_entity_state_v1_t), (void **)&entity) != QCX_PLUGIN_OK) {
		return NULL;
	}
	return entity;
}

uint32_t QCX_EntityCapacity(void)
{
	return qcx_entity_capacity;
}

static int QCX_FieldNameEquals(const qcx_game_api_v1_t *game,
	const qcx_engine_field_descriptor_v1_t *descriptor, const char *name)
{
	if (descriptor->name.data == 0U || descriptor->name.size == 0U
		|| descriptor->name.reserved0 != 0U || strlen(name) != descriptor->name.size) {
		return 0;
	}
	void *view = NULL;
	return game->memory_view(game->context, descriptor->name.data, descriptor->name.size,
		1U, &view) == QCX_PLUGIN_OK && view != NULL
		&& memcmp(view, name, descriptor->name.size) == 0;
}

static const qcx_engine_field_descriptor_v1_t *QCX_FindUniqueEntityField(
	const qcx_game_api_v1_t *game, const qcx_engine_field_table_v1_t *table,
	const void *table_view, const char *name)
{
	const qcx_engine_field_descriptor_v1_t *result = NULL;
	for (uint32_t index = 0U; index < table->count; ++index) {
		const qcx_engine_field_descriptor_v1_t *const descriptor =
			(const qcx_engine_field_descriptor_v1_t *)((const uint8_t *)table_view
				+ (size_t)index * table->descriptor_stride);
		if (!QCX_FieldNameEquals(game, descriptor, name)) continue;
		if (result != NULL) return NULL;
		result = descriptor;
	}
	return result;
}

static int QCX_ValidLegacyStringProjection(
	const qcx_engine_field_descriptor_v1_t *descriptor, qcx_engine_type_id_t type,
	uint32_t size, uint32_t alignment)
{
	return descriptor != NULL && descriptor->type_id == type
		&& descriptor->size == size && descriptor->alignment == alignment
		&& descriptor->access_flags == QCX_ENGINE_FIELD_HOST_READ
		&& descriptor->offset % descriptor->alignment == 0U
		&& qcx_entity_stride >= descriptor->size
		&& descriptor->offset <= qcx_entity_stride - descriptor->size;
}

int QCX_ResolveEntityFields(void)
{
	qcx_model_length_offset = UINT32_MAX;
	qcx_entity_field_error = "entity field table";
	QCX_ClearLegacyStringProjections();
	const qcx_game_api_v1_t *const game = QCX_Game();
	if (qcx_entity_memory == NULL || game == NULL || game->engine_fields == NULL) {
		return 0;
	}
	const qcx_guest_address_t exports_address = game->engine_fields(game->context);
	qcx_engine_field_exports_v1_t *exports = NULL;
	if (exports_address == 0U || game->memory_view(game->context, exports_address,
		sizeof(*exports), _Alignof(qcx_engine_field_exports_v1_t), (void **)&exports)
		!= QCX_PLUGIN_OK || exports == NULL
		|| exports->abi_version != QCX_ENGINE_FIELD_EXPORTS_ABI_VERSION_V1
		|| exports->struct_size < sizeof(*exports)
		|| exports->reserved0 != 0U) {
		return 0;
	}
	const qcx_engine_field_table_v1_t *const table = &exports->entity_fields;
	if (table->count == 0U) {
		return 0;
	}
	if (table->descriptors == 0U
		|| table->descriptor_stride < sizeof(qcx_engine_field_descriptor_v1_t)
		|| table->descriptor_stride % _Alignof(qcx_engine_field_descriptor_v1_t) != 0U) {
		return 0;
	}
	const uint64_t last = (uint64_t)(table->count - 1U) * table->descriptor_stride;
	const uint64_t envelope = last + sizeof(qcx_engine_field_descriptor_v1_t);
	if (last > UINT32_MAX || envelope > UINT32_MAX) {
		return 0;
	}
	void *table_view = NULL;
	if (game->memory_view(game->context, table->descriptors, (qcx_byte_count_t)envelope,
		_Alignof(qcx_engine_field_descriptor_v1_t), &table_view) != QCX_PLUGIN_OK
		|| table_view == NULL) {
		return 0;
	}
	const qcx_transport_kind_t kind = QCX_GameTransportKind();
	if (kind != QCX_TRANSPORT_NATIVE && kind != QCX_TRANSPORT_WASM) {
		return 0;
	}
	const qcx_engine_type_id_t data_type = QCX_EngineTypeId(
		kind == QCX_TRANSPORT_NATIVE ? "qc.string.data.native" : "qc.string.data.wasm32");
	const uint32_t data_size = kind == QCX_TRANSPORT_NATIVE ? sizeof(char *)
		: sizeof(uint32_t);
	const uint32_t data_alignment = kind == QCX_TRANSPORT_NATIVE ? _Alignof(char *)
		: _Alignof(uint32_t);
	qcx_legacy_string_projection_t projections[QCX_LEGACY_STRING_FIELD_COUNT];
	for (uint32_t field = 0U; field < QCX_LEGACY_STRING_FIELD_COUNT; ++field) {
		const qcx_engine_field_descriptor_v1_t *const data = QCX_FindUniqueEntityField(
			game, table, table_view, qcx_legacy_string_data_names[field]);
		const qcx_engine_field_descriptor_v1_t *const length = QCX_FindUniqueEntityField(
			game, table, table_view, qcx_legacy_string_length_names[field]);
		if (!QCX_ValidLegacyStringProjection(data, data_type, data_size, data_alignment)) {
			qcx_entity_field_error = qcx_legacy_string_data_names[field];
			return 0;
		}
		if (!QCX_ValidLegacyStringProjection(length, QCX_EngineTypeId("qc.u32"),
			sizeof(uint32_t), _Alignof(uint32_t))) {
			qcx_entity_field_error = qcx_legacy_string_length_names[field];
			return 0;
		}
		projections[field] = (qcx_legacy_string_projection_t){
			.data_offset = data->offset,
			.length_offset = length->offset,
		};
	}
	QCX_SetLegacyStringProjections(projections, kind);
	qcx_model_length_offset = projections[QCX_LEGACY_MODEL_FIELD_INDEX].length_offset;
	qcx_optional_field_spec_t specs[] = {
		{"items2", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_READ, &fofs_items2},
		{"maxspeed", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE, &fofs_maxspeed},
		{"gravity", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE, &fofs_gravity},
		{"movement", "qc.vec3f", 3U * sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_WRITE, &fofs_movement},
		{"vw_index", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_READ, &fofs_vw_index},
		{"hideentity", "qc.entity", sizeof(uint32_t), _Alignof(uint32_t), QCX_ENGINE_FIELD_HOST_READ, &fofs_hideentity},
		{"trackent", "qc.entity", sizeof(uint32_t), _Alignof(uint32_t), QCX_ENGINE_FIELD_HOST_READ, &fofs_trackent},
		{"visclients", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE, &fofs_visibility},
		{"hideplayers", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_READ, &fofs_hide_players},
		{"teleported", "qc.f32", sizeof(float), _Alignof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE, &fofs_teleported},
	};
	for (size_t spec_index = 0U; spec_index < sizeof(specs) / sizeof(specs[0]); ++spec_index) {
		qcx_optional_field_spec_t *const spec = &specs[spec_index];
		const qcx_engine_field_descriptor_v1_t *matched = NULL;
		int duplicate = 0;
		for (uint32_t index = 0U; index < table->count; ++index) {
			const qcx_engine_field_descriptor_v1_t *const descriptor =
				(const qcx_engine_field_descriptor_v1_t *)((const uint8_t *)table_view
					+ (size_t)index * table->descriptor_stride);
			if (!QCX_FieldNameEquals(game, descriptor, spec->name)) continue;
			if (matched != NULL) {
				duplicate = 1;
				break;
			}
			matched = descriptor;
		}
		if (matched == NULL || duplicate) continue;
		if (matched->type_id != QCX_EngineTypeId(spec->type_name)
			|| matched->size != spec->size || matched->alignment != spec->alignment
			|| matched->offset == 0U || matched->offset % matched->alignment != 0U
			|| (matched->access_flags & ~(QCX_ENGINE_FIELD_HOST_READ
				| QCX_ENGINE_FIELD_HOST_WRITE)) != 0U
			|| (matched->access_flags & spec->required_access) != spec->required_access
			|| qcx_entity_stride < matched->size
			|| matched->offset > qcx_entity_stride - matched->size
			|| qcx_entity_base > UINT64_MAX - matched->offset) {
			continue;
		}
		void *field_view = NULL;
		if (game->memory_view(game->context, qcx_entity_base + matched->offset,
			matched->size, matched->alignment, &field_view) != QCX_PLUGIN_OK || field_view == NULL) {
			continue;
		}
		*spec->offset = (int)matched->offset;
	}
	return 1;
}

const char *QCX_EntityFieldError(void)
{
	return qcx_entity_field_error == NULL ? "entity field table" : qcx_entity_field_error;
}

qbool QCX_EntityHasModel(const edict_t *entity)
{
	uint32_t length = 0U;
	const qcx_entity_id_t slot = QCX_EdictToSlot(entity);
	if (slot == QCX_INVALID_ENTITY_ID || entity->v == NULL
		|| qcx_model_length_offset == UINT32_MAX) {
		return false;
	}
	const byte *const base = (const byte *)entity->v;
	memcpy(&length, base + qcx_model_length_offset, sizeof(length));
	return length != 0U;
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

qcx_plugin_status_t QCX_CopyEntityString(const edict_t *edict, const char *field,
	char *out, uint32_t capacity, uint32_t *required)
{
	const qcx_game_api_v1_t *game = QCX_Game();
	const qcx_entity_id_t slot = QCX_EdictToSlot(edict);
	qcx_byte_count_t bytes = 0U;
	if (required != NULL) {
		*required = 0U;
	}
	if (game == NULL || slot == QCX_INVALID_ENTITY_ID || field == NULL
		|| out == NULL || capacity == 0U) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	const size_t field_size = strlen(field);
	if (field_size > UINT32_MAX) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	qcx_plugin_status_t status = game->string_read(game->context, QCX_SCOPE_ENTITY, slot,
		(const uint8_t *)field, (qcx_byte_count_t)field_size, NULL, 0U, &bytes);
	if (status != QCX_PLUGIN_OK && status != QCX_PLUGIN_BUFFER_TOO_SMALL) {
		return status;
	}
	if (bytes == UINT32_MAX) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	const uint32_t needed = bytes + 1U;
	if (required != NULL) {
		*required = needed;
	}
	if (capacity < needed) {
		return QCX_PLUGIN_BUFFER_TOO_SMALL;
	}
	if (bytes != 0U) {
		const qcx_byte_count_t expected = bytes;
		status = game->string_read(game->context, QCX_SCOPE_ENTITY, slot,
			(const uint8_t *)field, (qcx_byte_count_t)field_size, (uint8_t *)out,
			bytes, &bytes);
		if (status != QCX_PLUGIN_OK) {
			return status;
		}
		if (bytes != expected || bytes == UINT32_MAX) {
			if (required != NULL) {
				*required = bytes == UINT32_MAX ? UINT32_MAX : bytes + 1U;
			}
			return QCX_PLUGIN_BUFFER_TOO_SMALL;
		}
	}
	out[bytes] = '\0';
	return QCX_PLUGIN_OK;
}

qcx_plugin_status_t QCX_CopyLegacyString(int32_t token, char *out,
	uint32_t capacity, uint32_t *required)
{
	const qcx_game_api_v1_t *game = QCX_Game();
	qcx_byte_count_t bytes = 0U;
	if (required != NULL) {
		*required = 0U;
	}
	if (game == NULL || out == NULL || capacity == 0U) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	qcx_plugin_status_t status = game->legacy_string_read(game->context, token, NULL,
		0U, &bytes);
	if (status != QCX_PLUGIN_OK && status != QCX_PLUGIN_BUFFER_TOO_SMALL) {
		return status;
	}
	if (bytes == UINT32_MAX) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	const uint32_t needed = bytes + 1U;
	if (required != NULL) {
		*required = needed;
	}
	if (capacity < needed) {
		return QCX_PLUGIN_BUFFER_TOO_SMALL;
	}
	if (bytes != 0U) {
		const qcx_byte_count_t expected = bytes;
		status = game->legacy_string_read(game->context, token, (uint8_t *)out,
			bytes, &bytes);
		if (status != QCX_PLUGIN_OK) {
			return status;
		}
		if (bytes != expected || bytes == UINT32_MAX) {
			if (required != NULL) {
				*required = bytes == UINT32_MAX ? UINT32_MAX : bytes + 1U;
			}
			return QCX_PLUGIN_BUFFER_TOO_SMALL;
		}
	}
	out[bytes] = '\0';
	return QCX_PLUGIN_OK;
}
