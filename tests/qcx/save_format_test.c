#include "qcx/save_format.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
	metadata_section = 1,
	engine_section = 2,
	guest_section = 3,
	metadata_capacity_offset = 36,
	engine_offset = 52,
	engine_time_offset = engine_offset + 4,
	engine_precache_count_offset = engine_offset + 20,
	engine_slot_count_offset = engine_offset + 60,
	second_section_id_offset = 44,
	guest_section_length_offset = 120,
	fixture_capacity = 4
};

typedef struct save_bytes_s {
	uint8_t bytes[65536];
	uint32_t size;
} save_bytes_t;

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
	assert(offset + 4 <= out->size);
	out->bytes[offset] = (uint8_t)value;
	out->bytes[offset + 1] = (uint8_t)(value >> 8);
	out->bytes[offset + 2] = (uint8_t)(value >> 16);
	out->bytes[offset + 3] = (uint8_t)(value >> 24);
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

static void append_section(save_bytes_t *out, uint32_t id, const save_bytes_t *section)
{
	append_u32(out, id);
	append_u32(out, section->size);
	append_bytes(out, section->bytes, section->size);
}

static save_bytes_t make_valid_save(void)
{
	save_bytes_t out = {0};
	save_bytes_t metadata = {0};
	save_bytes_t engine = {0};
	save_bytes_t guest = {0};
	uint32_t slot;

	append_string(&metadata, "game");
	append_string(&metadata, "e1m1");
	append_u32(&metadata, UINT32_C(0x11223344));
	append_u32(&metadata, fixture_capacity);
	append_u32(&metadata, 0);

	append_u32(&engine, 1);
	append_f64(&engine, 1.25);
	append_u32(&engine, 7);
	append_u32(&engine, 0);
	append_u32(&engine, 0);
	append_u32(&engine, fixture_capacity);
	for (slot = 0; slot < fixture_capacity; ++slot) {
		append_u32(&engine, 0);
		append_f32(&engine, 0.0f);
	}
	append_u32(&engine, 0);

	append_bytes(&guest, "QC", 2);
	append_bytes(&out, "QCMS", 4);
	append_u32(&out, 1);
	append_section(&out, metadata_section, &metadata);
	append_section(&out, engine_section, &engine);
	append_section(&out, guest_section, &guest);
	assert(engine_offset == 52);
	assert(engine_slot_count_offset == 112);
	return out;
}

static void require_rejected(const uint8_t *bytes, uint32_t size)
{
	qcx_save_image_t *image = (qcx_save_image_t *)(uintptr_t)1;
	assert(QCX_SaveParse(bytes, size, &image) != QCX_PLUGIN_OK);
	assert(image == NULL);
}

static void test_parses_and_reencodes_a_transport_independent_image(void)
{
	const save_bytes_t input = make_valid_save();
	qcx_save_image_t *image = NULL;
	qcx_save_image_t *round_trip = NULL;
	uint8_t *encoded = NULL;
	uint32_t encoded_size = 0;

	assert(QCX_SaveParse(input.bytes, input.size, &image) == QCX_PLUGIN_OK);
	assert(strcmp(image->metadata.logical_game, "game") == 0);
	assert(strcmp(image->metadata.map_name, "e1m1") == 0);
	assert(image->metadata.map_bsp_checksum == UINT32_C(0x11223344));
	assert(image->metadata.entity_capacity == fixture_capacity);
	assert(image->engine_state.size == 64);
	assert(image->guest_payload.size == 2);
	assert(QCX_SaveEncode(image, &encoded, &encoded_size) == QCX_PLUGIN_OK);
	assert(encoded_size == input.size);
	assert(memcmp(encoded, input.bytes, input.size) == 0);
	assert(QCX_SaveParse(encoded, encoded_size, &round_trip) == QCX_PLUGIN_OK);
	assert(round_trip->metadata.entity_capacity == fixture_capacity);
	QCX_SaveImageFree(round_trip);
	free(encoded);
	QCX_SaveImageFree(image);
}

static void test_reencodes_an_empty_guest_payload(void)
{
	save_bytes_t input = make_valid_save();
	qcx_save_image_t *image = NULL;
	uint8_t *encoded = NULL;
	uint32_t encoded_size = 0;

	overwrite_u32(&input, guest_section_length_offset, 0);
	input.size -= 2;
	assert(QCX_SaveParse(input.bytes, input.size, &image) == QCX_PLUGIN_OK);
	assert(image->guest_payload.data == NULL);
	assert(image->guest_payload.size == 0);
	assert(QCX_SaveEncode(image, &encoded, &encoded_size) == QCX_PLUGIN_OK);
	assert(encoded_size == input.size);
	assert(memcmp(encoded, input.bytes, input.size) == 0);
	free(encoded);
	QCX_SaveImageFree(image);
}

static void test_rejects_an_incomplete_header(void)
{
	const uint8_t bytes[] = {'Q', 'C', 'M', 'S', 1, 0, 0, 0, 255, 255, 255, 255};
	require_rejected(bytes, sizeof(bytes));
}

static void test_rejects_an_unknown_version(void)
{
	save_bytes_t input = make_valid_save();
	input.bytes[4] = 2;
	require_rejected(input.bytes, input.size);
}

static void test_rejects_a_duplicate_section(void)
{
	save_bytes_t input = make_valid_save();
	input.bytes[second_section_id_offset] = metadata_section;
	input.bytes[second_section_id_offset + 1] = 0;
	input.bytes[second_section_id_offset + 2] = 0;
	input.bytes[second_section_id_offset + 3] = 0;
	require_rejected(input.bytes, input.size);
}

static void test_rejects_trailing_bytes(void)
{
	save_bytes_t input = make_valid_save();
	input.bytes[input.size++] = 0;
	require_rejected(input.bytes, input.size);
}

static void test_rejects_a_section_length_past_input_end(void)
{
	save_bytes_t input = make_valid_save();
	overwrite_u32(&input, guest_section_length_offset, 3);
	require_rejected(input.bytes, input.size);
}

static void test_rejects_an_invalid_logical_game_name(void)
{
	save_bytes_t input = make_valid_save();
	input.bytes[20] = '/';
	require_rejected(input.bytes, input.size);
}

static void test_rejects_an_invalid_capacity(void)
{
	save_bytes_t input = make_valid_save();
	overwrite_u32(&input, metadata_capacity_offset, 0);
	require_rejected(input.bytes, input.size);
}

static void test_rejects_nonfinite_server_time(void)
{
	save_bytes_t input = make_valid_save();
	const double nan_time = NAN;
	memcpy(input.bytes + engine_time_offset, &nan_time, sizeof(nan_time));
	require_rejected(input.bytes, input.size);
}

static void test_rejects_an_overlong_precache_list(void)
{
	save_bytes_t input = make_valid_save();
	overwrite_u32(&input, engine_precache_count_offset, 4353);
	require_rejected(input.bytes, input.size);
}

static void test_accepts_model_and_sound_precache_capacity(void)
{
	save_bytes_t input = make_valid_save();
	qcx_save_image_t *image = NULL;
	uint32_t index;
	const uint32_t count = 4352;
	const uint32_t resources_size = count * 5U;
	assert(input.size + resources_size <= sizeof(input.bytes));
	memmove(input.bytes + engine_precache_count_offset + 4U + resources_size,
		input.bytes + engine_precache_count_offset + 4U,
		input.size - (engine_precache_count_offset + 4U));
	input.size += resources_size;
	overwrite_u32(&input, engine_precache_count_offset, count);
	for (index = 0U; index < count; ++index) {
		const uint32_t offset = engine_precache_count_offset + 4U + index * 5U;
		overwrite_u32(&input, offset, 1U);
		input.bytes[offset + 4U] = 'x';
	}
	overwrite_u32(&input, 48U, 64U + resources_size);
	assert(QCX_SaveParse(input.bytes, input.size, &image) == QCX_PLUGIN_OK);
	QCX_SaveImageFree(image);
}

static save_bytes_t make_save_with_duplicate_client_slots(void)
{
	save_bytes_t input = make_valid_save();
	uint32_t i;
	assert(input.size == 126);
	overwrite_u32(&input, engine_slot_count_offset, 2);
	assert(input.size + 144 <= sizeof(input.bytes));
	memmove(input.bytes + 112 + 4 + 144, input.bytes + 112 + 4,
		input.size - (112 + 4));
	input.size += 144;
	for (i = 0; i < 2; ++i) {
		uint32_t parm;
		overwrite_u32(&input, 116 + i * 72, 1);
		overwrite_u32(&input, 120 + i * 72, 0);
		for (parm = 0; parm < 16; ++parm) {
			const float zero = 0.0f;
			memcpy(input.bytes + 124 + i * 72 + parm * 4, &zero, sizeof(zero));
		}
	}
	overwrite_u32(&input, 48, 64 + 144);
	return input;
}

static save_bytes_t make_save_with_one_client_slot(uint32_t slot)
{
	save_bytes_t input = make_valid_save();
	uint32_t parm;
	assert(input.size == 126);
	overwrite_u32(&input, engine_slot_count_offset, 1);
	assert(input.size + 72 <= sizeof(input.bytes));
	memmove(input.bytes + 112 + 4 + 72, input.bytes + 112 + 4,
		input.size - (112 + 4));
	input.size += 72;
	overwrite_u32(&input, 116, slot);
	overwrite_u32(&input, 120, 0);
	for (parm = 0; parm < 16; ++parm) {
		const float zero = 0.0f;
		memcpy(input.bytes + 124 + parm * 4, &zero, sizeof(zero));
	}
	overwrite_u32(&input, 48, 64 + 72);
	return input;
}

static void test_rejects_duplicate_client_slots(void)
{
	const save_bytes_t input = make_save_with_duplicate_client_slots();
	require_rejected(input.bytes, input.size);
}

static void test_rejects_a_client_slot_outside_entity_capacity(void)
{
	const save_bytes_t input = make_save_with_one_client_slot(fixture_capacity);
	require_rejected(input.bytes, input.size);
}

static void test_rejects_a_client_slot_without_a_player_entity(void)
{
	const save_bytes_t input = make_save_with_one_client_slot(fixture_capacity - 1U);
	require_rejected(input.bytes, input.size);
}

static void test_accepts_connected_spawned_client_flags(void)
{
	save_bytes_t input = make_save_with_one_client_slot(1U);
	qcx_save_image_t *image = NULL;

	overwrite_u32(&input, 40U, 1U);
	overwrite_u32(&input, 120U, 3U);
	assert(QCX_SaveParse(input.bytes, input.size, &image) == QCX_PLUGIN_OK);
	assert(image->metadata.contains_connected_clients == 1U);
	QCX_SaveImageFree(image);
}

int main(void)
{
	test_parses_and_reencodes_a_transport_independent_image();
	test_reencodes_an_empty_guest_payload();
	test_rejects_an_incomplete_header();
	test_rejects_an_unknown_version();
	test_rejects_a_duplicate_section();
	test_rejects_trailing_bytes();
	test_rejects_a_section_length_past_input_end();
	test_rejects_an_invalid_logical_game_name();
	test_rejects_an_invalid_capacity();
	test_rejects_nonfinite_server_time();
	test_rejects_an_overlong_precache_list();
	test_accepts_model_and_sound_precache_capacity();
	test_rejects_duplicate_client_slots();
	test_rejects_a_client_slot_outside_entity_capacity();
	test_rejects_a_client_slot_without_a_player_entity();
	test_accepts_connected_spawned_client_flags();
	return 0;
}
