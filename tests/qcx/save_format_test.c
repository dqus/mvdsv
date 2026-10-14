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
	engine_version_current = 5,
	roster_version_v1 = 1,
	fixture_entity_capacity = 4,
	fixture_client_slot_capacity = 3
};

typedef struct save_bytes_s {
	uint8_t bytes[65536];
	uint32_t size;
} save_bytes_t;

typedef struct v2_offsets_s {
	uint32_t world_entity_flags;
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

static void append_engine_v2(save_bytes_t *engine, v2_offsets_t *offsets,
	uint32_t lightstyle_count, const char *model, const char *sound)
{
	uint32_t entity;
	append_u32(engine, engine_version_current);
	append_f64(engine, 1.25);
	append_u32(engine, 7);
	append_u32(engine, lightstyle_count);
	for (entity = 0U; entity < lightstyle_count; ++entity) {
		append_string(engine, "m");
	}
	append_u32(engine, 2);
	append_string(engine, model);
	append_string(engine, sound);
	append_u32(engine, fixture_entity_capacity);
	for (entity = 0U; entity < fixture_entity_capacity; ++entity) {
		if (entity == 0U) offsets->world_entity_flags = engine->size;
		if (entity == 1U) offsets->player_entity_flags = engine->size;
		if (entity == 2U) offsets->second_player_entity_flags = engine->size;
		append_u32(engine, entity < 2U ? 1U : 0U);
		append_f32(engine, 0.0f);
		append_f32(engine, 0.0f); /* alpha */
		for (uint32_t axis = 0U; axis < 3U; ++axis) {
			append_f32(engine, 0.0f); /* colourmod */
		}
	}
	append_u32(engine, 0U); /* Static entities. */
	append_u32(engine, 2U); /* Signon coordinate width. */
	append_u32(engine, 0U); /* Signon buffers. */
	append_f32(engine, 0.0f); /* deathmatch */
	append_f32(engine, 1.0f); /* coop */
	append_f32(engine, 2.0f); /* teamplay */
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

static save_bytes_t make_v2_with_engine(const char *team, v2_offsets_t *offsets,
	uint32_t lightstyle_count, const char *model, const char *sound)
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
	append_engine_v2(&engine, offsets, lightstyle_count, model, sound);
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
	offsets->world_entity_flags += engine_payload;
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

static save_bytes_t make_valid_v2(const char *team, v2_offsets_t *offsets)
{
	return make_v2_with_engine(team, offsets, 64U, "M:1:progs/player.mdl",
		"S:1:misc/menu.wav");
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
	assert(image->engine.time == 1.25);
	assert(image->engine.serverflags == 7U);
	assert(strcmp(image->engine.lightstyles[63], "m") == 0);
	assert(image->engine.precache_count == 2U);
	assert(image->engine.precaches[0].kind == 'M');
	assert(image->engine.precaches[0].slot == 1U);
	assert(strcmp(image->engine.precaches[0].name, "progs/player.mdl") == 0);
	assert(image->engine.precaches[1].kind == 'S');
	assert(image->engine.precaches[1].slot == 1U);
	assert(image->engine.edicts[0].state == QCX_SAVE_ACTIVE_EDICT);
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

static void test_lightstyles_round_trip_as_text_not_resource_names(void)
{
	v2_offsets_t offsets;
	const save_bytes_t input = make_valid_v2("red", &offsets);
	qcx_save_image_t *image = NULL;
	qcx_save_image_t *restored = NULL;
	uint8_t *encoded = NULL;
	uint32_t encoded_size = 0U;
	assert(QCX_SaveParse(input.bytes, input.size, &image) == QCX_PLUGIN_OK);
	image->engine.lightstyles[0][0] = '\0';
	memset(image->engine.lightstyles[1], 'a', 255U);
	image->engine.lightstyles[1][255] = '\0';
	/* The builtin accepts NUL-free text, not just printable resource paths. */
	strcpy(image->engine.lightstyles[2], "a\\b\n\200");
	assert(QCX_SaveEncode(image, &encoded, &encoded_size) == QCX_PLUGIN_OK);
	assert(QCX_SaveParse(encoded, encoded_size, &restored) == QCX_PLUGIN_OK);
	assert(restored->engine.lightstyles[0][0] == '\0');
	assert(strlen(restored->engine.lightstyles[1]) == 255U);
	assert(memcmp(restored->engine.lightstyles[1], image->engine.lightstyles[1], 256U) == 0);
	assert(strcmp(restored->engine.lightstyles[2], "a\\b\n\200") == 0);
	free(encoded);
	QCX_SaveImageFree(restored);
	memset(image->engine.lightstyles[0], 'a', sizeof(image->engine.lightstyles[0]));
	assert(QCX_SaveEncode(image, &encoded, &encoded_size) == QCX_PLUGIN_BAD_ARGUMENT);
	assert(encoded == NULL && encoded_size == 0U);
	QCX_SaveImageFree(image);
}

static void test_lightstyle_wire_text_rejects_nul_and_oversize(void)
{
	v2_offsets_t offsets;
	save_bytes_t input = make_valid_v2("red", &offsets);
	/* The engine payload starts with version/time/serverflags/style count. */
	const uint32_t first_style_length = offsets.second_section_id + 8U + 20U;
	input.bytes[first_style_length + 4U] = '\0';
	require_rejected(input.bytes, input.size);
	input = make_valid_v2("red", &offsets);
	/* Keep the rest of the wire image valid: replace the original one-byte
	 * style with 256 nonzero bytes and grow the engine section accordingly. */
	const uint32_t first_style_bytes = first_style_length + 4U;
	const uint32_t tail = first_style_bytes + 1U;
	const uint32_t growth = 255U;
	assert(input.size + growth <= sizeof(input.bytes));
	memmove(input.bytes + tail + growth, input.bytes + tail, input.size - tail);
	memset(input.bytes + first_style_bytes, 'a', 256U);
	input.size += growth;
	overwrite_u32(&input, first_style_length, 256U);
	overwrite_u32(&input, offsets.second_section_id + 4U,
		offsets.roster_section_id - offsets.second_section_id - 8U + growth);
	require_rejected(input.bytes, input.size);
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

static void test_rejects_engine_errors_during_initial_parse(void)
{
	v2_offsets_t offsets;
	save_bytes_t input = make_v2_with_engine("red", &offsets, 0U,
		"M:1:progs/player.mdl", "S:1:misc/menu.wav");
	require_rejected(input.bytes, input.size);
	const char *invalid_resources[] = {
		"progs/player.mdl", "X:1:progs/player.mdl", "M:0:progs/player.mdl",
		"M:4096:progs/player.mdl", "S:256:misc/menu.wav", "M:1:"
	};
	for (uint32_t index = 0U; index < sizeof(invalid_resources) / sizeof(invalid_resources[0]); ++index) {
		input = make_v2_with_engine("red", &offsets, 64U, invalid_resources[index],
			"S:1:misc/menu.wav");
		require_rejected(input.bytes, input.size);
	}
	input = make_v2_with_engine("red", &offsets, 64U,
		"M:1:progs/player.mdl", "M:1:progs/other.mdl");
	require_rejected(input.bytes, input.size);
	input = make_valid_v2("red", &offsets);
	overwrite_u32(&input, offsets.world_entity_flags, 2U);
	require_rejected(input.bytes, input.size);
	input = make_valid_v2("red", &offsets);
	overwrite_u32(&input, offsets.second_player_entity_flags + 24U, 2U);
	require_rejected(input.bytes, input.size);
}

static void test_encode_validates_constructed_engine_state(void)
{
	v2_offsets_t offsets;
	const save_bytes_t input = make_valid_v2("red", &offsets);
	qcx_save_image_t *image = NULL;
	uint8_t *encoded = NULL;
	uint32_t encoded_size = 0U;
	assert(QCX_SaveParse(input.bytes, input.size, &image) == QCX_PLUGIN_OK);
	image->engine.precaches[1] = image->engine.precaches[0];
	assert(QCX_SaveEncode(image, &encoded, &encoded_size) == QCX_PLUGIN_BAD_ARGUMENT);
	assert(encoded == NULL && encoded_size == 0U);
	QCX_SaveImageFree(image);
}

static void test_rejects_truncated_engine_and_other_sections(void)
{
	v2_offsets_t offsets;
	const save_bytes_t input = make_valid_v2("red", &offsets);
	for (uint32_t size = 0U; size < input.size; ++size) {
		require_rejected(input.bytes, size);
	}
}

static void test_static_world_codec_and_bounds(void)
{
	v2_offsets_t offsets;
	const save_bytes_t input = make_valid_v2("red", &offsets);
	qcx_save_image_t *image = NULL;
	qcx_save_image_t *restored = NULL;
	uint8_t *encoded = NULL;
	uint32_t size = 0U;
	assert(QCX_SaveParse(input.bytes, input.size, &image) == QCX_PLUGIN_OK);
	/* Prespawn appends a command after copying a chunk into even the smallest
	 * accepted client reliable buffer (1350 bytes). Keep 64 bytes for it. */
	image->engine.signon_count = 1U;
	image->engine.signon_coord_bytes = 2U;
	image->engine.signon[0].size = 1287U;
	assert(!QCX_SaveImageValid(image));
	image->engine.static_entity_count = 1U;
	image->engine.static_entities[0] = (qcx_save_static_entity_t){
		.modelindex = 1U, .frame = -7, .colormap = 4, .skin = 5,
		.origin = {32, 64, 96}, .angles = {0, 90, 0},
		.trans = 127, .colourmod = {32, 64, 96}};
	image->engine.signon_count = QCX_SAVE_MAX_SIGNON_BUFFERS;
	image->engine.signon_coord_bytes = 4U;
	for (uint32_t index = 0U; index < image->engine.signon_count; ++index) {
		image->engine.signon[index].size = QCX_SAVE_MAX_SIGNON_BUFFER_BYTES;
		memset(image->engine.signon[index].data, (int)index,
			QCX_SAVE_MAX_SIGNON_BUFFER_BYTES);
	}
	assert(QCX_SaveEncode(image, &encoded, &size) == QCX_PLUGIN_OK);
	assert(QCX_SaveParse(encoded, size, &restored) == QCX_PLUGIN_OK);
	assert(restored->engine.static_entity_count == 1U);
	assert(restored->engine.static_entities[0].frame == -7);
	assert(restored->engine.static_entities[0].origin[2] == 96);
	assert(restored->engine.static_entities[0].angles[1] == 90);
	assert(restored->engine.static_entities[0].colourmod[2] == 96U);
	assert(restored->engine.static_entities[0].trans == 127U);
	assert(restored->engine.signon_count == 16U);
	assert(restored->engine.signon_coord_bytes == 4U);
	assert(restored->engine.signon[15].size == 1286U);
	assert(restored->engine.signon[15].data[1285] == 15U);
	assert(restored->engine.signon[0].data[0] == 0U);
	/* The new tail adds one 56-byte static record before the first chunk
	 * length. Corrupt that length with plenty of bytes still available, so
	 * rejection tests the protocol limit rather than simple truncation. */
	const uint32_t chunk_length_offset = offsets.roster_section_id - 12U + 56U;
	encoded[chunk_length_offset] = 7U; /* 1287, little-endian. */
	require_rejected(encoded, size);
	free(encoded);
	QCX_SaveImageFree(restored);
	image->engine.signon[15].size = 1287U;
	assert(!QCX_SaveImageValid(image));
	image->engine.signon[15].size = 1286U;
	image->engine.signon_count = QCX_SAVE_MAX_SIGNON_BUFFERS + 1U;
	assert(!QCX_SaveImageValid(image));
	image->engine.signon_count = 1U;
	image->engine.signon_coord_bytes = 3U;
	assert(!QCX_SaveImageValid(image));
	image->engine.signon_coord_bytes = 2U;
	image->engine.static_entities[0].modelindex = 2U; /* Not precached. */
	assert(!QCX_SaveImageValid(image));
	image->engine.static_entities[0].modelindex = 1U;
	image->engine.static_entities[0].origin[0] = NAN;
	assert(!QCX_SaveImageValid(image));
	image->engine.static_entity_count = QCX_SAVE_MAX_STATIC_ENTITIES + 1U;
	assert(!QCX_SaveImageValid(image));
	QCX_SaveImageFree(image);
	/* Malformed wire counts must be rejected before indexing fixed arrays. */
	save_bytes_t malformed = input;
	overwrite_u32(&malformed, offsets.roster_section_id - 24U,
		QCX_SAVE_MAX_STATIC_ENTITIES + 1U);
	require_rejected(malformed.bytes, malformed.size);
	malformed = input;
	overwrite_u32(&malformed, offsets.roster_section_id - 16U,
		QCX_SAVE_MAX_SIGNON_BUFFERS + 1U);
	require_rejected(malformed.bytes, malformed.size);
}

static void test_engine_render_metadata_codec(void)
{
	v2_offsets_t offsets;
	const save_bytes_t input = make_valid_v2("red", &offsets);
	qcx_save_image_t *image = NULL;
	qcx_save_image_t *restored = NULL;
	uint8_t *encoded = NULL;
	uint32_t size = 0U;
	assert(QCX_SaveParse(input.bytes, input.size, &image) == QCX_PLUGIN_OK);
	image->engine.edicts[0].alpha = 0.375f;
	image->engine.edicts[0].colourmod[0] = 0.25f;
	image->engine.edicts[0].colourmod[1] = 0.5f;
	image->engine.edicts[0].colourmod[2] = 2.0f; /* Overbright is supported. */
	image->engine.edicts[1].alpha = 1.0f;
	assert(QCX_SaveEncode(image, &encoded, &size) == QCX_PLUGIN_OK);
	assert(QCX_SaveParse(encoded, size, &restored) == QCX_PLUGIN_OK);
	assert(restored->engine.edicts[0].alpha == 0.375f);
	assert(restored->engine.edicts[0].colourmod[0] == 0.25f);
	assert(restored->engine.edicts[0].colourmod[1] == 0.5f);
	assert(restored->engine.edicts[0].colourmod[2] == 2.0f);
	assert(restored->engine.edicts[1].alpha == 1.0f);
	assert(restored->engine.edicts[2].alpha == 0.0f);
	free(encoded);
	QCX_SaveImageFree(restored);
	const float invalid_alpha[] = {NAN, INFINITY, -0.01f, 1.01f};
	for (size_t index = 0U; index < sizeof(invalid_alpha) / sizeof(invalid_alpha[0]); ++index) {
		image->engine.edicts[0].alpha = invalid_alpha[index];
		assert(!QCX_SaveImageValid(image));
	}
	image->engine.edicts[0].alpha = 0.0f;
	const float invalid_colour[] = {NAN, INFINITY, -0.01f};
	for (size_t index = 0U; index < sizeof(invalid_colour) / sizeof(invalid_colour[0]); ++index) {
		image->engine.edicts[1].colourmod[2] = invalid_colour[index];
		assert(!QCX_SaveImageValid(image));
	}
	QCX_SaveImageFree(image);
	save_bytes_t malformed = input;
	overwrite_u32(&malformed, offsets.world_entity_flags + 8U, UINT32_C(0x7fc00000));
	require_rejected(malformed.bytes, malformed.size);
	malformed = input;
	overwrite_u32(&malformed, offsets.player_entity_flags + 12U, UINT32_C(0xbf800000));
	require_rejected(malformed.bytes, malformed.size);
}

static void test_game_mode_codec_and_validation(void)
{
	v2_offsets_t offsets;
	const save_bytes_t input = make_valid_v2("red", &offsets);
	qcx_save_image_t *image = NULL;
	qcx_save_image_t *restored = NULL;
	uint8_t *encoded = NULL;
	uint32_t size = 0U;
	assert(QCX_SaveParse(input.bytes, input.size, &image) == QCX_PLUGIN_OK);
	assert(image->engine.deathmatch == 0.0f);
	assert(image->engine.coop == 1.0f);
	assert(image->engine.teamplay == 2.0f);
	image->engine.deathmatch = 4.0f;
	image->engine.coop = 0.0f;
	image->engine.teamplay = 0.5f;
	assert(QCX_SaveEncode(image, &encoded, &size) == QCX_PLUGIN_OK);
	assert(QCX_SaveParse(encoded, size, &restored) == QCX_PLUGIN_OK);
	assert(restored->engine.deathmatch == 4.0f);
	assert(restored->engine.coop == 0.0f);
	assert(restored->engine.teamplay == 0.5f);
	free(encoded);
	QCX_SaveImageFree(restored);
	float *const fields[] = {&image->engine.deathmatch, &image->engine.coop,
		&image->engine.teamplay};
	for (uint32_t index = 0U; index < 3U; ++index) {
		const float saved = *fields[index];
		const float invalid[] = {NAN, INFINITY, -INFINITY};
		for (uint32_t value = 0U; value < 3U; ++value) {
			*fields[index] = invalid[value];
			assert(QCX_SaveEncode(image, &encoded, &size) == QCX_PLUGIN_BAD_ARGUMENT);
			assert(encoded == NULL && size == 0U);
			save_bytes_t malformed = input;
			memcpy(malformed.bytes + offsets.roster_section_id - 12U + index * 4U,
				&invalid[value], sizeof(float));
			require_rejected(malformed.bytes, malformed.size);
		}
		*fields[index] = saved;
	}
	QCX_SaveImageFree(image);
	/* No compatibility parser for old engine sections. */
	save_bytes_t old = input;
	overwrite_u32(&old, offsets.second_section_id + 8U, 4U);
	require_rejected(old.bytes, old.size);
}

int main(void)
{
	test_game_mode_codec_and_validation();
	test_static_world_codec_and_bounds();
	test_engine_render_metadata_codec();
	test_lightstyles_round_trip_as_text_not_resource_names();
	test_lightstyle_wire_text_rejects_nul_and_oversize();
	test_rejects_engine_errors_during_initial_parse();
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
	test_encode_validates_constructed_engine_state();
	test_rejects_truncated_engine_and_other_sections();
	return 0;
}
