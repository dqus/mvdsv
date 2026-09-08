#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "qwsvdef.h"

#include "qcx/strings.h"

typedef struct fixture_native_entity_s {
	qcx_shared_entity_state_v1_t shared;
	char *data[QCX_LEGACY_STRING_FIELD_COUNT];
	uint32_t length[QCX_LEGACY_STRING_FIELD_COUNT];
} fixture_native_entity_t;

typedef struct fixture_wasm_entity_s {
	qcx_shared_entity_state_v1_t shared;
	uint32_t data[QCX_LEGACY_STRING_FIELD_COUNT];
	uint32_t length[QCX_LEGACY_STRING_FIELD_COUNT];
} fixture_wasm_entity_t;

server_t sv;
static fixture_native_entity_t native_entities[2];
static fixture_wasm_entity_t wasm_entities[2];
static uint8_t wasm_memory[128];
static int wasm_memory_available;
static qcx_guest_address_t wasm_last_address;
static qcx_byte_count_t wasm_last_size;
static uint32_t legacy_copy_calls;

uint32_t QCX_EntityCapacity(void)
{
	return 2U;
}

edict_t *QCX_SlotToEdict(qcx_entity_id_t slot)
{
	return slot < QCX_EntityCapacity() ? &sv.edicts[slot] : NULL;
}

static qcx_plugin_status_t fixture_legacy_string_read(void *context, int32_t token,
	uint8_t *out, qcx_byte_count_t capacity, qcx_byte_count_t *required)
{
	(void)context;
	(void)token;
	(void)out;
	(void)capacity;
	(void)required;
	++legacy_copy_calls;
	assert(!"QCX_BorrowLegacyString must not execute legacy_string_read");
	return QCX_PLUGIN_UNAVAILABLE;
}

static qcx_plugin_status_t fixture_memory_view(void *context, qcx_guest_address_t address,
	qcx_byte_count_t size, uint32_t alignment, void **out)
{
	(void)context;
	assert(alignment == 1U);
	wasm_last_address = address;
	wasm_last_size = size;
	if (out == NULL || !wasm_memory_available || address == 0U
		|| address > sizeof(wasm_memory) || size > sizeof(wasm_memory) - address) {
		return QCX_PLUGIN_UNAVAILABLE;
	}
	*out = wasm_memory + address;
	return QCX_PLUGIN_OK;
}

static const qcx_game_api_v1_t game = {
	.abi_version = QCX_PLUGIN_ABI_VERSION_V1,
	.struct_size = sizeof(game),
	.legacy_string_read = fixture_legacy_string_read,
	.memory_view = fixture_memory_view,
};

const qcx_game_api_v1_t *QCX_Game(void)
{
	return &game;
}

static qcx_legacy_string_projection_t native_projections(void)
{
	return (qcx_legacy_string_projection_t){
		.data_offset = offsetof(fixture_native_entity_t, data),
		.length_offset = offsetof(fixture_native_entity_t, length),
	};
}

static qcx_legacy_string_projection_t wasm_projections(void)
{
	return (qcx_legacy_string_projection_t){
		.data_offset = offsetof(fixture_wasm_entity_t, data),
		.length_offset = offsetof(fixture_wasm_entity_t, length),
	};
}

static void set_native(unsigned int slot, unsigned int field, char *value)
{
	native_entities[slot].data[field] = value;
	native_entities[slot].length[field] = (uint32_t)strlen(value);
}

static void set_wasm(unsigned int slot, unsigned int field, uint32_t offset,
	const char *value)
{
	const uint32_t length = (uint32_t)strlen(value);
	memcpy(wasm_memory + offset, value, length);
	wasm_memory[offset + length] = '\0';
	wasm_entities[slot].data[field] = offset;
	wasm_entities[slot].length[field] = length;
}

static int32_t token(unsigned int slot, unsigned int field)
{
	return (int32_t)(1U + slot * QCX_LEGACY_STRING_FIELD_COUNT + field);
}

int main(void)
{
	char first[] = "first";
	char replacement[] = "fresh";
	char last[] = "last";
	qcx_legacy_string_projection_t projections[QCX_LEGACY_STRING_FIELD_COUNT];
	memset(&sv, 0, sizeof(sv));
	memset(native_entities, 0, sizeof(native_entities));
	for (unsigned int field = 0U; field < QCX_LEGACY_STRING_FIELD_COUNT; ++field) {
		projections[field] = native_projections();
		projections[field].data_offset += field * sizeof(native_entities[0].data[0]);
		projections[field].length_offset += field * sizeof(native_entities[0].length[0]);
	}
	sv.edicts[0].v = (entvars_t *)&native_entities[0];
	sv.edicts[1].v = (entvars_t *)&native_entities[1];
	set_native(0U, 0U, first);
	set_native(1U, QCX_LEGACY_STRING_FIELD_COUNT - 1U, last);

	assert(strcmp(QCX_BorrowLegacyString(0), "") == 0);
	assert(QCX_BorrowLegacyString(token(0U, 0U)) == NULL);
	QCX_SetLegacyStringProjections(projections, QCX_TRANSPORT_NATIVE);
	assert(QCX_BorrowLegacyString(token(0U, 0U)) == first);
	assert(QCX_BorrowLegacyString(token(1U, QCX_LEGACY_STRING_FIELD_COUNT - 1U)) == last);
	assert(QCX_BorrowLegacyString(-1) == NULL);
	assert(QCX_BorrowLegacyString(token(2U, 0U)) == NULL);
	assert(legacy_copy_calls == 0U);

	set_native(0U, 0U, replacement);
	assert(QCX_BorrowLegacyString(token(0U, 0U)) == replacement);
	native_entities[0].length[0] = 0U;
	assert(strcmp(QCX_BorrowLegacyString(token(0U, 0U)), "") == 0);
	assert(legacy_copy_calls == 0U);
	QCX_ClearLegacyStringProjections();
	assert(QCX_BorrowLegacyString(token(0U, 0U)) == NULL);

	memset(wasm_entities, 0, sizeof(wasm_entities));
	memset(wasm_memory, 0, sizeof(wasm_memory));
	for (unsigned int field = 0U; field < QCX_LEGACY_STRING_FIELD_COUNT; ++field) {
		projections[field] = wasm_projections();
		projections[field].data_offset += field * sizeof(wasm_entities[0].data[0]);
		projections[field].length_offset += field * sizeof(wasm_entities[0].length[0]);
	}
	sv.edicts[0].v = (entvars_t *)&wasm_entities[0];
	sv.edicts[1].v = (entvars_t *)&wasm_entities[1];
	set_wasm(0U, QCX_LEGACY_MODEL_FIELD_INDEX, 11U, "wasm-model");
	wasm_memory_available = 1;
	QCX_SetLegacyStringProjections(projections, QCX_TRANSPORT_WASM);
	assert(QCX_BorrowLegacyString(token(0U, QCX_LEGACY_MODEL_FIELD_INDEX))
		== (const char *)(wasm_memory + 11U));
	assert(wasm_last_address == 11U && wasm_last_size == sizeof("wasm-model"));
	assert(legacy_copy_calls == 0U);

	wasm_entities[0].data[QCX_LEGACY_MODEL_FIELD_INDEX] = 0U;
	assert(QCX_BorrowLegacyString(token(0U, QCX_LEGACY_MODEL_FIELD_INDEX)) == NULL);
	set_wasm(0U, QCX_LEGACY_MODEL_FIELD_INDEX, 33U, "no-nul");
	wasm_memory[33U + strlen("no-nul")] = 'x';
	assert(QCX_BorrowLegacyString(token(0U, QCX_LEGACY_MODEL_FIELD_INDEX)) == NULL);
	wasm_memory_available = 0;
	assert(QCX_BorrowLegacyString(token(0U, QCX_LEGACY_MODEL_FIELD_INDEX)) == NULL);
	wasm_memory_available = 1;
	wasm_entities[0].length[QCX_LEGACY_MODEL_FIELD_INDEX] = UINT32_MAX;
	assert(QCX_BorrowLegacyString(token(0U, QCX_LEGACY_MODEL_FIELD_INDEX)) == NULL);
	QCX_ClearLegacyStringProjections();
	assert(legacy_copy_calls == 0U);
	return 0;
}
