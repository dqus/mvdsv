#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "qwsvdef.h"
#include "qcx/entities.h"
#include "qcx/transport.h"

server_t sv;
int fofs_items2, fofs_maxspeed, fofs_gravity, fofs_movement, fofs_vw_index;
int fofs_hideentity, fofs_trackent, fofs_visibility, fofs_hide_players, fofs_teleported;

static qcx_game_entity_memory_v1_t memory;
static qcx_engine_field_exports_v1_t exports;

enum {
	optional_field_count = 10,
	legacy_string_field_count = 11,
	legacy_model_field_index = 1,
	legacy_string_descriptor_count = legacy_string_field_count * 2,
	first_legacy_string_descriptor = optional_field_count,
	total_descriptor_count = optional_field_count + legacy_string_descriptor_count,
	model_length_descriptor = first_legacy_string_descriptor + 2 * 1 + 1
};

static qcx_engine_field_descriptor_v1_t descriptors[total_descriptor_count];
static char names[total_descriptor_count][32];
static const char *const legacy_string_names[legacy_string_field_count] = {
	"classname", "model", "weaponmodel", "netname", "target", "targetname",
	"message", "noise", "noise1", "noise2", "noise3"
};
typedef struct fixture_entity_s {
	qcx_shared_entity_state_v1_t shared;
	uint32_t string_data[legacy_string_field_count];
	uint32_t string_length[legacy_string_field_count];
} fixture_entity_t;
static fixture_entity_t entities[2];
static int reject_whole_storage;

void SV_Error(char *error, ...) { (void)error; assert(!"unexpected SV_Error"); }
const qcx_game_api_v1_t *QCX_Game(void);

qcx_transport_kind_t QCX_GameTransportKind(void)
{
	return QCX_TRANSPORT_WASM;
}

static qcx_plugin_status_t memory_view(void *context, qcx_guest_address_t address,
	qcx_byte_count_t size, uint32_t alignment, void **out)
{
	(void)context;
	if (address == 0U || size == 0U || alignment == 0U) return QCX_PLUGIN_BAD_ARGUMENT;
	if (reject_whole_storage
		&& address == (qcx_guest_address_t)(uintptr_t)entities
		&& size == sizeof(entities)) return QCX_PLUGIN_UNAVAILABLE;
	*out = (void *)(uintptr_t)address;
	return QCX_PLUGIN_OK;
}

static qcx_guest_address_t engine_fields(void *context)
{ (void)context; return (qcx_guest_address_t)(uintptr_t)&exports; }

static const qcx_game_api_v1_t game = {
	.abi_version = QCX_PLUGIN_ABI_VERSION_V1,
	.struct_size = sizeof(game),
	.memory_view = memory_view,
	.engine_fields = engine_fields,
};
const qcx_game_api_v1_t *QCX_Game(void) { return &game; }

static qcx_engine_type_id_t type_id(const char *name)
{
	qcx_engine_type_id_t value = UINT64_C(14695981039346656037);
	while (*name) { value ^= (uint8_t)*name++; value *= UINT64_C(1099511628211); }
	return value;
}

static void descriptor(uint32_t index, const char *name, const char *type, uint32_t offset,
	uint32_t size, uint32_t access)
{
	assert(snprintf(names[index], sizeof(names[index]), "%s", name) > 0);
	descriptors[index] = (qcx_engine_field_descriptor_v1_t){
		.name = {(qcx_guest_address_t)(uintptr_t)names[index],
			(uint32_t)strlen(names[index]), 0U},
		.type_id = type_id(type), .offset = offset, .size = size,
		.alignment = _Alignof(float), .access_flags = access};
}

static void reset_offsets(void)
{
	fofs_items2 = fofs_maxspeed = fofs_gravity = fofs_movement = fofs_vw_index = 0;
	fofs_hideentity = fofs_trackent = fofs_visibility = fofs_hide_players = fofs_teleported = 0;
}

int main(void)
{
	memory = (qcx_game_entity_memory_v1_t){
		.abi_version = QCX_GAME_ENTITY_MEMORY_ABI_VERSION_V1,
		.struct_size = sizeof(memory),
		.shared_state_base = (qcx_guest_address_t)(uintptr_t)entities,
		.entity_object_base = (qcx_guest_address_t)(uintptr_t)entities,
		.entity_stride = sizeof(entities[0]), .max_entities = 2U,
		.shared_state_abi_version = QCX_SHARED_ENTITY_STATE_ABI_VERSION_V1,
	};
	/* Read-only requirements accept an additional known host-write bit. */
	descriptor(0U, "items2", "qc.f32", 4U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(1U, "maxspeed", "qc.f32", 8U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(2U, "gravity", "qc.f32", 12U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(3U, "movement", "qc.vec3f", 16U, 3U * sizeof(float), QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(4U, "vw_index", "qc.f32", 28U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ);
	descriptor(5U, "hideentity", "qc.entity", 32U, sizeof(uint32_t), QCX_ENGINE_FIELD_HOST_READ);
	descriptor(6U, "trackent", "qc.entity", 36U, sizeof(uint32_t), QCX_ENGINE_FIELD_HOST_READ);
	descriptor(7U, "visclients", "qc.f32", 40U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(8U, "hideplayers", "qc.f32", 44U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ);
	descriptor(9U, "teleported", "qc.f32", 48U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	for (uint32_t field = 0U; field < legacy_string_field_count; ++field) {
		const uint32_t data = first_legacy_string_descriptor + field * 2U;
		char data_name[32];
		char length_name[32];
		assert(snprintf(data_name, sizeof(data_name), "qcx.%s.data", legacy_string_names[field]) > 0);
		assert(snprintf(length_name, sizeof(length_name), "qcx.%s.length", legacy_string_names[field]) > 0);
		descriptor(data, data_name, "qc.string.data.wasm32",
			offsetof(fixture_entity_t, string_data) + field * sizeof(uint32_t),
			sizeof(uint32_t), QCX_ENGINE_FIELD_HOST_READ);
		descriptor(data + 1U, length_name, "qc.u32",
			offsetof(fixture_entity_t, string_length) + field * sizeof(uint32_t),
			sizeof(uint32_t), QCX_ENGINE_FIELD_HOST_READ);
		descriptors[data].alignment = _Alignof(uint32_t);
		descriptors[data + 1U].alignment = _Alignof(uint32_t);
	}
	exports = (qcx_engine_field_exports_v1_t){
		.abi_version = QCX_ENGINE_FIELD_EXPORTS_ABI_VERSION_V1, .struct_size = sizeof(exports),
		.entity_fields = {(qcx_guest_address_t)(uintptr_t)descriptors, total_descriptor_count, sizeof(descriptors[0])},
	};
	assert(QCX_ConfigureEntities((qcx_guest_address_t)(uintptr_t)&memory));
	assert(QCX_ResolveEntityFields());
	assert(QCX_BindEntities());
	entities[1].string_length[legacy_model_field_index] = 17U;
	assert(QCX_EntityHasModel(&sv.edicts[1]));
	entities[1].string_length[legacy_model_field_index] = 0U;
	assert(!QCX_EntityHasModel(&sv.edicts[1]));
	assert(fofs_items2 == 4 && fofs_maxspeed == 8 && fofs_gravity == 12);
	assert(fofs_movement == 16 && fofs_vw_index == 28 && fofs_hideentity == 32);
	assert(fofs_trackent == 36 && fofs_visibility == 40 && fofs_hide_players == 44);
	assert(fofs_teleported == 48);
	reset_offsets();
	descriptors[1].offset = 0U;
	assert(QCX_ResolveEntityFields());
	assert(fofs_maxspeed == 0 && fofs_items2 == 4 && fofs_hideentity == 32);
	descriptors[1].offset = 8U;

	/* Every invalid matrix property disables only that row. */
	reset_offsets(); descriptors[1].type_id = type_id("qc.entity");
	assert(QCX_ResolveEntityFields() && fofs_maxspeed == 0 && fofs_gravity == 12);
	descriptors[1].type_id = type_id("qc.f32");
	reset_offsets(); descriptors[1].size = 8U;
	assert(QCX_ResolveEntityFields() && fofs_maxspeed == 0 && fofs_hideentity == 32);
	descriptors[1].size = sizeof(float);
	reset_offsets(); descriptors[1].alignment = 8U;
	assert(QCX_ResolveEntityFields() && fofs_maxspeed == 0 && fofs_trackent == 36);
	descriptors[1].alignment = _Alignof(float);
	reset_offsets(); descriptors[1].access_flags = QCX_ENGINE_FIELD_HOST_READ;
	assert(QCX_ResolveEntityFields() && fofs_maxspeed == 0 && fofs_items2 == 4);
	descriptors[1].access_flags = QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE;
	reset_offsets(); descriptors[1].offset = memory.entity_stride;
	assert(QCX_ResolveEntityFields() && fofs_maxspeed == 0 && fofs_hideentity == 32);
	descriptors[1].offset = 8U;
	reset_offsets(); descriptors[1].access_flags |= UINT32_C(4);
	assert(QCX_ResolveEntityFields() && fofs_maxspeed == 0 && fofs_gravity == 12);
	descriptors[1].access_flags = QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE;
	reset_offsets(); descriptors[9].name = descriptors[1].name;
	assert(QCX_ResolveEntityFields() && fofs_maxspeed == 0 && fofs_hideentity == 32);
	descriptors[9].name = (qcx_abi_string_ref_v1_t){
		(qcx_guest_address_t)(uintptr_t)names[9], (uint32_t)strlen(names[9]), 0U};

	/* Every direct string projection is mandatory, exact and unique. */
	for (uint32_t index = first_legacy_string_descriptor;
		index < total_descriptor_count; ++index) {
		const qcx_abi_string_ref_v1_t saved = descriptors[index].name;
		descriptors[index].name.data = 0U;
		assert(!QCX_ResolveEntityFields());
		descriptors[index].name = saved;
		assert(QCX_ResolveEntityFields());
	}
	for (uint32_t field = 0U; field < legacy_string_field_count; ++field) {
		const uint32_t data = first_legacy_string_descriptor + field * 2U;
		const qcx_abi_string_ref_v1_t saved_data = descriptors[data].name;
		const qcx_abi_string_ref_v1_t saved_length = descriptors[data + 1U].name;
		descriptors[data].name.data = 0U;
		descriptors[data + 1U].name.data = 0U;
		assert(!QCX_ResolveEntityFields());
		descriptors[data].name = saved_data;
		descriptors[data + 1U].name = saved_length;
		assert(QCX_ResolveEntityFields());
	}
	descriptors[total_descriptor_count - 1U].name =
		descriptors[first_legacy_string_descriptor].name;
	assert(!QCX_ResolveEntityFields());
	descriptors[total_descriptor_count - 1U].name = (qcx_abi_string_ref_v1_t){
		(qcx_guest_address_t)(uintptr_t)names[total_descriptor_count - 1U],
		(uint32_t)strlen(names[total_descriptor_count - 1U]), 0U};
	descriptors[model_length_descriptor].type_id = type_id("qc.f32");
	assert(!QCX_ResolveEntityFields());
	descriptors[model_length_descriptor].type_id = type_id("qc.u32");
	descriptors[model_length_descriptor].size = 8U;
	assert(!QCX_ResolveEntityFields());
	descriptors[model_length_descriptor].size = sizeof(uint32_t);
	descriptors[model_length_descriptor].alignment = 8U;
	assert(!QCX_ResolveEntityFields());
	descriptors[model_length_descriptor].alignment = _Alignof(uint32_t);
	descriptors[model_length_descriptor].access_flags = QCX_ENGINE_FIELD_HOST_READ
		| QCX_ENGINE_FIELD_HOST_WRITE;
	assert(!QCX_ResolveEntityFields());
	descriptors[model_length_descriptor].access_flags = QCX_ENGINE_FIELD_HOST_READ;
	descriptors[model_length_descriptor].offset = memory.entity_stride;
	assert(!QCX_ResolveEntityFields());
	descriptors[model_length_descriptor].offset = offsetof(fixture_entity_t, string_length)
		+ legacy_model_field_index * sizeof(uint32_t);
	assert(QCX_ResolveEntityFields());
	entities[1].string_length[legacy_model_field_index] = 17U;
	assert(QCX_EntityHasModel(&sv.edicts[1]));

	reject_whole_storage = 1;
	assert(!QCX_BindEntities());
	reject_whole_storage = 0;
	assert(QCX_BindEntities());
	exports.entity_fields.count = 0U;
	assert(!QCX_ResolveEntityFields());
	exports.entity_fields.count = total_descriptor_count;
	QCX_ClearEntities();
	return 0;
}
