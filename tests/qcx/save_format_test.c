#include "qcx/save_format.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
	qcms_version_v1 = 1,
	qcms_version_v2 = 2,
	metadata_section = 1,
	engine_section = 2,
	roster_section = 3,
	guest_section = 4,
	engine_version_v1 = 1,
	engine_version_v2 = 2,
	roster_version_v1 = 1,
	fixture_entity_capacity = 4,
	fixture_client_slot_capacity = 3
};

typedef struct save_bytes_s {
	uint8_t bytes[65536];
	uint32_t size;
} save_bytes_t;

typedef struct v2_offsets_s {
	uint32_t client_slot_capacity;
	uint32_t player_entity_flags;
	uint32_t saved_slot;
	uint32_t spawned;
	uint32_t role;
	uint32_t name_length;
	uint32_t name_bytes;
	uint32_t team_length;
	uint32_t spawn_parm;
	uint32_t second_section_id;
	uint32_t roster_section_id;
	uint32_t roster_payload_size;
	uint32_t second_player_entity_flags;
} v2_offsets_t;

static void append_bytes(save_bytes_t *out, const void *bytes, uint32_t size)
{
	assert(out->size + size <= sizeof(out->bytes));
	memcpy(out->bytes + out->size, bytes, size);
	out->size += size;
}

static void append_u32(save_bytes_t *out, uint32_t value)
{
	const uint8_t bytes[4] = {
		(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
		(uint8_t)(value >> 24)};
	append_bytes(out, bytes, sizeof(bytes));
}

static void overwrite_u32(save_bytes_t *out, uint32_t offset, uint32_t value)
{
	assert(offset + 4U <= out->size);
	out->bytes[offset] = (uint8_t)value;
	out->bytes[offset + 1U] = (uint8_t)(value >> 8);
	out->bytes[offset + 2U] = (uint8_t)(value >> 16);
	out->bytes[offset + 3U] = (uint8_t)(value >> 24);
}

static void append_f32(save_bytes_t *out, float value)
{
	append_bytes(out, &value, sizeof(value));
}

static void append_f64(save_bytes_t *out, double value)
{
	append_bytes(out, &value, sizeof(value));
}

static void append_string(save_bytes_t *out, const char *value)
{
	const uint32_t size = (uint32_t)strlen(value);
	append_u32(out, size);
	append_bytes(out, value, size);
}

static void append_section(save_bytes_t *out, uint32_t id, const save_bytes_t *section,
	uint32_t *payload_offset)
{
	append_u32(out, id);
	append_u32(out, section->size);
	if (payload_offset != NULL) *payload_offset = out->size;
	append_bytes(out, section->bytes, section->size);
}

static void append_metadata_v2(save_bytes_t *metadata, v2_offsets_t *offsets)
{
	append_string(metadata, "game");
	append_string(metadata, "e1m1");
	append_u32(metadata, UINT32_C(0x11223344));
	append_u32(metadata, fixture_entity_capacity);
	offsets->client_slot_capacity = metadata->size;
	append_u32(metadata, fixture_client_slot_capacity);
}

static void append_engine_v2(save_bytes_t *engine, v2_offsets_t *offsets)
{
	uint32_t entity;
	append_u32(engine, engine_version_v2);
	append_f64(engine, 1.25);
	append_u32(engine, 7);
	append_u32(engine, 0);
	append_u32(engine, 0);
	append_u32(engine, fixture_entity_capacity);
	for (entity = 0U; entity < fixture_entity_capacity; ++entity) {
		if (entity == 1U) offsets->player_entity_flags = engine->size;
		if (entity == 2U) offsets->second_player_entity_flags = engine->size;
		append_u32(engine, entity < 2U ? 1U : 0U);
		append_f32(engine, 0.0f);
	}
}

static void append_roster_v1(save_bytes_t *roster, v2_offsets_t *offsets,
	const char *team)
{
	uint32_t parm;
	append_u32(roster, roster_version_v1);
	append_u32(roster, 1);
	offsets->saved_slot = roster->size;
	append_u32(roster, 0);
	offsets->spawned = roster->size;
	append_u32(roster, 1);
	offsets->role = roster->size;
	append_u32(roster, 0);
	offsets->name_length = roster->size;
	append_string(roster, "Alice");
	offsets->name_bytes = offsets->name_length + 4U;
	offsets->team_length = roster->size;
	append_string(roster, team);
	for (parm = 0U; parm < 16U; ++parm) {
		if (parm == 0U) offsets->spawn_parm = roster->size;
		append_f32(roster, (float)parm + 0.5f);
	}
}

static save_bytes_t make_valid_v2(const char *team, v2_offsets_t *offsets)
{
	save_bytes_t out = {0};
	save_bytes_t metadata = {0};
	save_bytes_t engine = {0};
	save_bytes_t roster = {0};
	save_bytes_t guest = {0};
	uint32_t metadata_payload;
	uint32_t engine_payload;
	uint32_t roster_payload;
	memset(offsets, 0, sizeof(*offsets));
	append_metadata_v2(&metadata, offsets);
	append_engine_v2(&engine, offsets);
	append_roster_v1(&roster, offsets, team);
	append_bytes(&guest, "QC", 2);
	append_bytes(&out, "QCMS", 4);
	append_u32(&out, qcms_version_v2);
	append_section(&out, metadata_section, &metadata, &metadata_payload);
	offsets->second_section_id = out.size;
	append_section(&out, engine_section, &engine, &engine_payload);
	offsets->roster_section_id = out.size;
	append_section(&out, roster_section, &roster, &roster_payload);
	append_section(&out, guest_section, &guest, NULL);
	offsets->client_slot_capacity += metadata_payload;
	offsets->player_entity_flags += engine_payload;
	offsets->saved_slot += roster_payload;
	offsets->spawned += roster_payload;
	offsets->role += roster_payload;
	offsets->name_length += roster_payload;
	offsets->name_bytes += roster_payload;
	offsets->team_length += roster_payload;
	offsets->spawn_parm += roster_payload;
	offsets->roster_payload_size = roster.size;
	offsets->second_player_entity_flags += engine_payload;
	return out;
}

static void append_roster_record(save_bytes_t *record, uint32_t saved_slot,
	const char *name)
{
	uint32_t parm;
	append_u32(record, saved_slot);
	append_u32(record, 1U);
	append_u32(record, 0U);
	append_string(record, name);
	append_string(record, "red");
	for (parm = 0U; parm < 16U; ++parm) append_f32(record, (float)parm);
}

static void append_second_roster_record(save_bytes_t *save, const v2_offsets_t *offsets,
	uint32_t saved_slot, const char *name)
{
	save_bytes_t record = {0};
	const uint32_t insert_offset = offsets->roster_section_id + 8U
		+ offsets->roster_payload_size;
	append_roster_record(&record, saved_slot, name);
	assert(save->size + record.size <= sizeof(save->bytes));
	memmove(save->bytes + insert_offset + record.size, save->bytes + insert_offset,
		save->size - insert_offset);
	memcpy(save->bytes + insert_offset, record.bytes, record.size);
	save->size += record.size;
	overwrite_u32(save, offsets->roster_section_id + 4U,
		offsets->roster_payload_size + record.size);
	overwrite_u32(save, offsets->roster_section_id + 8U + 4U, 2U);
}

static save_bytes_t make_valid_v1(void)
{
	save_bytes_t out = {0};
	save_bytes_t metadata = {0};
	save_bytes_t engine = {0};
	save_bytes_t guest = {0};
	uint32_t entity;
	append_string(&metadata, "game");
	append_string(&metadata, "e1m1");
	append_u32(&metadata, UINT32_C(0x11223344));
	append_u32(&metadata, fixture_entity_capacity);
	append_u32(&metadata, 0);
	append_u32(&engine, engine_version_v1);
	append_f64(&engine, 1.25);
	append_u32(&engine, 7);
	append_u32(&engine, 0);
	append_u32(&engine, 0);
	append_u32(&engine, fixture_entity_capacity);
	for (entity = 0U; entity < fixture_entity_capacity; ++entity) {
		append_u32(&engine, 0);
		append_f32(&engine, 0.0f);
	}
	append_u32(&engine, 0);
	append_bytes(&guest, "QC", 2);
	append_bytes(&out, "QCMS", 4);
	append_u32(&out, qcms_version_v1);
	append_section(&out, metadata_section, &metadata, NULL);
	append_section(&out, engine_section, &engine, NULL);
	append_section(&out, 3, &guest, NULL);
	return out;
}

static void require_rejected(const uint8_t *bytes, uint32_t size)
{
	qcx_save_image_t *image = (qcx_save_image_t *)(uintptr_t)1;
	assert(QCX_SaveParse(bytes, size, &image) != QCX_PLUGIN_OK);
	assert(image == NULL);
}

static void test_parses_and_reencodes_v2_with_a_roster(void)
{
	v2_offsets_t offsets;
	const save_bytes_t input = make_valid_v2("red", &offsets);
	qcx_save_image_t *image = NULL;
	qcx_save_image_t *round_trip = NULL;
	uint8_t *encoded = NULL;
	uint32_t encoded_size = 0U;
	assert(QCX_SaveParse(input.bytes, input.size, &image) == QCX_PLUGIN_OK);
	assert(strcmp(image->metadata.logical_game, "game") == 0);
	assert(strcmp(image->metadata.map_name, "e1m1") == 0);
	assert(image->metadata.map_bsp_checksum == UINT32_C(0x11223344));
	assert(image->metadata.entity_capacity == fixture_entity_capacity);
	assert(image->metadata.client_slot_capacity == fixture_client_slot_capacity);
	assert(image->engine_state.size > 0U);
	assert(image->roster_count == 1U);
	assert(image->roster[0].saved_slot == 0U);
	assert(image->roster[0].spawned == 1U);
	assert(image->roster[0].role == QCX_SAVE_ROLE_PLAYER);
	assert(strcmp(image->roster[0].name, "Alice") == 0);
	assert(strcmp(image->roster[0].team, "red") == 0);
	assert(image->roster[0].spawn_parms[15] == 15.5f);
	assert(image->guest_payload.size == 2U);
	assert(QCX_SaveEncode(image, &encoded, &encoded_size) == QCX_PLUGIN_OK);
	assert(encoded_size == input.size);
	assert(memcmp(encoded, input.bytes, input.size) == 0);
	assert(QCX_SaveParse(encoded, encoded_size, &round_trip) == QCX_PLUGIN_OK);
	QCX_SaveImageFree(round_trip);
	free(encoded);
	QCX_SaveImageFree(image);
}

static void test_accepts_an_empty_roster_team(void)
{
	v2_offsets_t offsets;
	const save_bytes_t input = make_valid_v2("", &offsets);
	qcx_save_image_t *image = NULL;
	assert(QCX_SaveParse(input.bytes, input.size, &image) == QCX_PLUGIN_OK);
	QCX_SaveImageFree(image);
}

static void test_rejects_a_complete_v1_container(void)
{
	const save_bytes_t input = make_valid_v1();
	require_rejected(input.bytes, input.size);
}

static void test_rejects_wrong_section_order(void)
{
	v2_offsets_t offsets;
	save_bytes_t input = make_valid_v2("red", &offsets);
	overwrite_u32(&input, offsets.second_section_id, roster_section);
	require_rejected(input.bytes, input.size);
}

static void test_rejects_invalid_client_slot_capacity(void)
{
	v2_offsets_t offsets;
	save_bytes_t input = make_valid_v2("red", &offsets);
	overwrite_u32(&input, offsets.client_slot_capacity, 0U);
	require_rejected(input.bytes, input.size);
	input = make_valid_v2("red", &offsets);
	overwrite_u32(&input, offsets.client_slot_capacity, fixture_entity_capacity);
	require_rejected(input.bytes, input.size);
	input = make_valid_v2("red", &offsets);
	overwrite_u32(&input, offsets.client_slot_capacity, QCX_SAVE_MAX_CLIENTS + 1U);
	require_rejected(input.bytes, input.size);
}

static void test_rejects_duplicate_slots_and_canonical_names(void)
{
	v2_offsets_t offsets;
	save_bytes_t input = make_valid_v2("red", &offsets);
	append_second_roster_record(&input, &offsets, 0U, "Bob");
	require_rejected(input.bytes, input.size);
	input = make_valid_v2("red", &offsets);
	overwrite_u32(&input, offsets.second_player_entity_flags, 1U);
	append_second_roster_record(&input, &offsets, 1U, "alice");
	require_rejected(input.bytes, input.size);
	input = make_valid_v2("red", &offsets);
	overwrite_u32(&input, offsets.second_player_entity_flags, 1U);
	append_second_roster_record(&input, &offsets, 1U, "alice");
	input.bytes[offsets.name_bytes] |= 0x80U;
	require_rejected(input.bytes, input.size);
}

static void test_compares_names_like_quake(void)
{
	const char colored_alice[] = {(char)0xc1, 'l', 'i', 'c', 'e', '\0'};
	assert(QCX_SaveClientNameEqual("Alice", "alice"));
	assert(QCX_SaveClientNameEqual(colored_alice, "alice"));
	assert(!QCX_SaveClientNameEqual("Alice", "Bob"));
}

static void test_rejects_a_roster_slot_outside_its_bounds(void)
{
	v2_offsets_t offsets;
	save_bytes_t input = make_valid_v2("red", &offsets);
	overwrite_u32(&input, offsets.saved_slot, fixture_client_slot_capacity);
	require_rejected(input.bytes, input.size);
}

static void test_rejects_a_roster_player_without_an_active_entity(void)
{
	v2_offsets_t offsets;
	save_bytes_t input = make_valid_v2("red", &offsets);
	overwrite_u32(&input, offsets.player_entity_flags, 2U);
	require_rejected(input.bytes, input.size);
}

static void test_rejects_invalid_roster_record_values(void)
{
	v2_offsets_t offsets;
	save_bytes_t input = make_valid_v2("red", &offsets);
	overwrite_u32(&input, offsets.spawned, 2U);
	require_rejected(input.bytes, input.size);
	input = make_valid_v2("red", &offsets);
	overwrite_u32(&input, offsets.role, 2U);
	require_rejected(input.bytes, input.size);
	input = make_valid_v2("red", &offsets);
	overwrite_u32(&input, offsets.name_length, 0U);
	require_rejected(input.bytes, input.size);
	input = make_valid_v2("red", &offsets);
	{
		const float nan_value = NAN;
		memcpy(input.bytes + offsets.spawn_parm, &nan_value, sizeof(nan_value));
	}
	require_rejected(input.bytes, input.size);
	input = make_valid_v2("red", &offsets);
	input.bytes[offsets.name_bytes] = 0x80U;
	require_rejected(input.bytes, input.size);
	input = make_valid_v2("red", &offsets);
	input.bytes[offsets.name_bytes + 1U] = '\t';
	require_rejected(input.bytes, input.size);
}

static void test_rejects_trailing_bytes(void)
{
	v2_offsets_t offsets;
	save_bytes_t input = make_valid_v2("red", &offsets);
	input.bytes[input.size++] = 0U;
	require_rejected(input.bytes, input.size);
}

int main(void)
{
	test_parses_and_reencodes_v2_with_a_roster();
	test_accepts_an_empty_roster_team();
	test_rejects_a_complete_v1_container();
	test_rejects_wrong_section_order();
	test_rejects_invalid_client_slot_capacity();
	test_rejects_a_roster_slot_outside_its_bounds();
	test_rejects_a_roster_player_without_an_active_entity();
	test_rejects_invalid_roster_record_values();
	test_rejects_duplicate_slots_and_canonical_names();
	test_compares_names_like_quake();
	test_rejects_trailing_bytes();
	return 0;
}
