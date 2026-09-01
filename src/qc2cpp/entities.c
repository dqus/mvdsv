#include "qwsvdef.h"

#include "qc2cpp/adapter.h"
#include "qc2cpp/entities.h"

#include <limits.h>
#include <string.h>

static qc_game_entity_memory_v1_t *qc_entity_memory;
static qc_guest_address_t qc_entity_base;
static uint32_t qc_entity_stride;
static uint32_t qc_entity_capacity;

int QC_ConfigureEntities(qc_guest_address_t publication_address)
{
	QC_ClearEntities();
	const qc_game_api_v1_t *game = QC_Game();
	qc_game_entity_memory_v1_t *memory = NULL;
	if (game == NULL || publication_address == 0U
		|| game->memory_view(game->context, publication_address, sizeof(*memory),
			_Alignof(qc_game_entity_memory_v1_t), (void **)&memory) != QC_PLUGIN_OK
		|| memory == NULL
		|| memory->abi_version != QC_GAME_ENTITY_MEMORY_ABI_VERSION_V1
		|| memory->struct_size < sizeof(*memory)
		|| memory->shared_state_base == 0U
		|| memory->entity_object_base == 0U
		|| memory->shared_state_base != memory->entity_object_base
		|| memory->entity_stride < sizeof(qc_shared_entity_state_v1_t)
		|| memory->entity_stride % _Alignof(qc_shared_entity_state_v1_t) != 0U
		|| memory->max_entities == 0U
		|| memory->max_entities > MAX_EDICTS
		|| memory->shared_state_abi_version != QC_SHARED_ENTITY_STATE_ABI_VERSION_V1
		|| memory->reserved0 != 0U) {
		return 0;
	}
	const uint64_t span = (uint64_t)memory->entity_stride * memory->max_entities;
	if (memory->entity_object_base > UINT64_MAX - span) {
		return 0;
	}
	qc_entity_memory = memory;
	qc_entity_base = memory->shared_state_base;
	qc_entity_stride = memory->entity_stride;
	qc_entity_capacity = memory->max_entities;
	sv.max_edicts = (int)qc_entity_capacity;
	return 1;
}

int QC_BindEntities(void)
{
	if (qc_entity_memory == NULL || sv.max_edicts <= 0
		|| (uint32_t)sv.max_edicts > qc_entity_capacity) {
		return 0;
	}
	for (int slot = 0; slot < sv.max_edicts; ++slot) {
		qc_shared_entity_state_v1_t *const entity = QC_Entity((qc_entity_id_t)slot);
		if (entity == NULL) {
			return 0;
		}
		sv.edicts[slot].v = (entvars_t *)entity;
		sv.edicts[slot].e.entnum = slot;
	}
	return 1;
}

void QC_ClearEntities(void)
{
	for (int slot = 0; slot < sv.max_edicts && slot < MAX_EDICTS; ++slot) {
		sv.edicts[slot].v = NULL;
	}
	qc_entity_memory = NULL;
	qc_entity_base = 0U;
	qc_entity_stride = 0U;
	qc_entity_capacity = 0U;
}

qc_shared_entity_state_v1_t *QC_Entity(qc_entity_id_t slot)
{
	const qc_game_api_v1_t *game = QC_Game();
	if (qc_entity_memory == NULL || game == NULL || slot >= qc_entity_capacity) {
		return NULL;
	}
	const uint64_t offset = (uint64_t)slot * qc_entity_stride;
	if (offset > UINT64_MAX - qc_entity_base) {
		return NULL;
	}
	qc_shared_entity_state_v1_t *entity = NULL;
	if (game->memory_view(game->context, qc_entity_base + offset, sizeof(*entity),
		_Alignof(qc_shared_entity_state_v1_t), (void **)&entity) != QC_PLUGIN_OK) {
		return NULL;
	}
	return entity;
}

qc_entity_id_t QC_EdictToSlot(const edict_t *edict)
{
	const uintptr_t first = (uintptr_t)&sv.edicts[0];
	const uintptr_t address = (uintptr_t)edict;
	if (qc_entity_memory == NULL || edict == NULL || address < first
		|| (address - first) % sizeof(sv.edicts[0]) != 0U) {
		return QC_INVALID_ENTITY_ID;
	}
	const uintptr_t slot = (address - first) / sizeof(sv.edicts[0]);
	if (slot >= qc_entity_capacity || slot >= (uintptr_t)sv.max_edicts) {
		return QC_INVALID_ENTITY_ID;
	}
	return (qc_entity_id_t)slot;
}

edict_t *QC_SlotToEdict(qc_entity_id_t slot)
{
	if (qc_entity_memory == NULL || slot >= qc_entity_capacity
		|| slot >= (uint32_t)sv.max_edicts) {
		return NULL;
	}
	return &sv.edicts[slot];
}

void QC_ClearEdict(edict_t *edict)
{
	const qc_game_api_v1_t *game = QC_Game();
	const qc_entity_id_t slot = QC_EdictToSlot(edict);
	if (game != NULL && slot != QC_INVALID_ENTITY_ID) {
		game->clear_edict(game->context, slot);
	}
}

int QC_SetEntityString(edict_t *edict, const char *field, const char *value)
{
	const qc_game_api_v1_t *game = QC_Game();
	const qc_entity_id_t slot = QC_EdictToSlot(edict);
	if (game == NULL || slot == QC_INVALID_ENTITY_ID || field == NULL || value == NULL) {
		return 0;
	}
	const size_t field_size = strlen(field);
	const size_t value_size = strlen(value);
	if (field_size > UINT32_MAX || value_size > UINT32_MAX) {
		return 0;
	}
	return game->string_write(game->context, QC_SCOPE_ENTITY, slot,
		(const uint8_t *)field, (qc_byte_count_t)field_size,
		(const uint8_t *)value, (qc_byte_count_t)value_size) == QC_PLUGIN_OK;
}

const char *QC_EntityStringFieldName(const edict_t *edict, const void *member)
{
	if (edict == NULL || edict->v == NULL || member == NULL) {
		return NULL;
	}
#define QC_STRING_FIELD(name) if (member == &edict->v->name) return #name
	QC_STRING_FIELD(classname);
	QC_STRING_FIELD(model);
	QC_STRING_FIELD(weaponmodel);
	QC_STRING_FIELD(netname);
	QC_STRING_FIELD(target);
	QC_STRING_FIELD(targetname);
	QC_STRING_FIELD(message);
	QC_STRING_FIELD(noise);
	QC_STRING_FIELD(noise1);
	QC_STRING_FIELD(noise2);
	QC_STRING_FIELD(noise3);
#undef QC_STRING_FIELD
	return NULL;
}

qc_plugin_status_t QC_CopyEntityString(const edict_t *edict, const char *field,
	char *out, uint32_t capacity, uint32_t *required)
{
	const qc_game_api_v1_t *game = QC_Game();
	const qc_entity_id_t slot = QC_EdictToSlot(edict);
	qc_byte_count_t bytes = 0U;
	if (required != NULL) {
		*required = 0U;
	}
	if (game == NULL || slot == QC_INVALID_ENTITY_ID || field == NULL
		|| out == NULL || capacity == 0U) {
		return QC_PLUGIN_BAD_ARGUMENT;
	}
	const size_t field_size = strlen(field);
	if (field_size > UINT32_MAX) {
		return QC_PLUGIN_BAD_ARGUMENT;
	}
	qc_plugin_status_t status = game->string_read(game->context, QC_SCOPE_ENTITY, slot,
		(const uint8_t *)field, (qc_byte_count_t)field_size, NULL, 0U, &bytes);
	if (status != QC_PLUGIN_OK && status != QC_PLUGIN_BUFFER_TOO_SMALL) {
		return status;
	}
	if (bytes == UINT32_MAX) {
		return QC_PLUGIN_BAD_ARGUMENT;
	}
	const uint32_t needed = bytes + 1U;
	if (required != NULL) {
		*required = needed;
	}
	if (capacity < needed) {
		return QC_PLUGIN_BUFFER_TOO_SMALL;
	}
	if (bytes != 0U) {
		const qc_byte_count_t expected = bytes;
		status = game->string_read(game->context, QC_SCOPE_ENTITY, slot,
			(const uint8_t *)field, (qc_byte_count_t)field_size, (uint8_t *)out,
			bytes, &bytes);
		if (status != QC_PLUGIN_OK) {
			return status;
		}
		if (bytes != expected || bytes == UINT32_MAX) {
			if (required != NULL) {
				*required = bytes == UINT32_MAX ? UINT32_MAX : bytes + 1U;
			}
			return QC_PLUGIN_BUFFER_TOO_SMALL;
		}
	}
	out[bytes] = '\0';
	return QC_PLUGIN_OK;
}

qc_plugin_status_t QC_CopyLegacyString(int32_t token, char *out,
	uint32_t capacity, uint32_t *required)
{
	const qc_game_api_v1_t *game = QC_Game();
	qc_byte_count_t bytes = 0U;
	if (required != NULL) {
		*required = 0U;
	}
	if (game == NULL || out == NULL || capacity == 0U) {
		return QC_PLUGIN_BAD_ARGUMENT;
	}
	qc_plugin_status_t status = game->legacy_string_read(game->context, token, NULL,
		0U, &bytes);
	if (status != QC_PLUGIN_OK && status != QC_PLUGIN_BUFFER_TOO_SMALL) {
		return status;
	}
	if (bytes == UINT32_MAX) {
		return QC_PLUGIN_BAD_ARGUMENT;
	}
	const uint32_t needed = bytes + 1U;
	if (required != NULL) {
		*required = needed;
	}
	if (capacity < needed) {
		return QC_PLUGIN_BUFFER_TOO_SMALL;
	}
	if (bytes != 0U) {
		const qc_byte_count_t expected = bytes;
		status = game->legacy_string_read(game->context, token, (uint8_t *)out,
			bytes, &bytes);
		if (status != QC_PLUGIN_OK) {
			return status;
		}
		if (bytes != expected || bytes == UINT32_MAX) {
			if (required != NULL) {
				*required = bytes == UINT32_MAX ? UINT32_MAX : bytes + 1U;
			}
			return QC_PLUGIN_BUFFER_TOO_SMALL;
		}
	}
	out[bytes] = '\0';
	return QC_PLUGIN_OK;
}
