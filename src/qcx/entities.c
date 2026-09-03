#include "qwsvdef.h"

#include "qcx/adapter.h"
#include "qcx/entities.h"

#include <limits.h>
#include <string.h>

static qcx_game_entity_memory_v1_t *qcx_entity_memory;
static qcx_guest_address_t qcx_entity_base;
static uint32_t qcx_entity_stride;
static uint32_t qcx_entity_capacity;

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
	if (memory->entity_object_base > UINT64_MAX - span) {
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
	if (qcx_entity_memory == NULL || sv.max_edicts <= 0
		|| (uint32_t)sv.max_edicts > qcx_entity_capacity) {
		return 0;
	}
	for (int slot = 0; slot < sv.max_edicts; ++slot) {
		qcx_shared_entity_state_v1_t *const entity = QCX_Entity((qcx_entity_id_t)slot);
		if (entity == NULL) {
			return 0;
		}
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
