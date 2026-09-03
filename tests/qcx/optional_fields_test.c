#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "qwsvdef.h"
#include "qcx/entities.h"

server_t sv;
int fofs_items2, fofs_maxspeed, fofs_gravity, fofs_movement, fofs_vw_index;
int fofs_hideentity, fofs_trackent, fofs_visibility, fofs_hide_players, fofs_teleported;

static qcx_game_entity_memory_v1_t memory;
static qcx_engine_field_exports_v1_t exports;
static qcx_engine_field_descriptor_v1_t descriptors[10];
static char names[][16] = {"items2", "maxspeed", "gravity", "movement", "vw_index",
	"hideentity", "trackent", "visclients", "hideplayers", "teleported"};
static qcx_shared_entity_state_v1_t entities[2];

void SV_Error(char *error, ...) { (void)error; assert(!"unexpected SV_Error"); }
const qcx_game_api_v1_t *QCX_Game(void);

static qcx_plugin_status_t memory_view(void *context, qcx_guest_address_t address,
	qcx_byte_count_t size, uint32_t alignment, void **out)
{
	(void)context;
	if (address == 0U || size == 0U || alignment == 0U) return QCX_PLUGIN_BAD_ARGUMENT;
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

static void descriptor(uint32_t index, const char *type, uint32_t offset,
	uint32_t size, uint32_t access)
{
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
	descriptor(0U, "qc.f32", 4U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(1U, "qc.f32", 8U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(2U, "qc.f32", 12U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(3U, "qc.vec3f", 16U, 3U * sizeof(float), QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(4U, "qc.f32", 28U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ);
	descriptor(5U, "qc.entity", 32U, sizeof(uint32_t), QCX_ENGINE_FIELD_HOST_READ);
	descriptor(6U, "qc.entity", 36U, sizeof(uint32_t), QCX_ENGINE_FIELD_HOST_READ);
	descriptor(7U, "qc.f32", 40U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(8U, "qc.f32", 44U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ);
	descriptor(9U, "qc.f32", 48U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	exports = (qcx_engine_field_exports_v1_t){
		.abi_version = QCX_ENGINE_FIELD_EXPORTS_ABI_VERSION_V1, .struct_size = sizeof(exports),
		.entity_fields = {(qcx_guest_address_t)(uintptr_t)descriptors, 10U, sizeof(descriptors[0])},
	};
	assert(QCX_ConfigureEntities((qcx_guest_address_t)(uintptr_t)&memory));
	assert(QCX_ResolveOptionalEntityFields());
	assert(fofs_items2 == 4 && fofs_maxspeed == 8 && fofs_gravity == 12);
	assert(fofs_movement == 16 && fofs_vw_index == 28 && fofs_hideentity == 32);
	assert(fofs_trackent == 36 && fofs_visibility == 40 && fofs_hide_players == 44);
	assert(fofs_teleported == 48);
	reset_offsets();
	descriptors[1].offset = 0U;
	assert(QCX_ResolveOptionalEntityFields());
	assert(fofs_maxspeed == 0 && fofs_items2 == 4 && fofs_hideentity == 32);
	descriptors[1].offset = 8U;

	/* Every invalid matrix property disables only that row. */
	reset_offsets(); descriptors[1].type_id = type_id("qc.entity");
	assert(QCX_ResolveOptionalEntityFields() && fofs_maxspeed == 0 && fofs_gravity == 12);
	descriptors[1].type_id = type_id("qc.f32");
	reset_offsets(); descriptors[1].size = 8U;
	assert(QCX_ResolveOptionalEntityFields() && fofs_maxspeed == 0 && fofs_hideentity == 32);
	descriptors[1].size = sizeof(float);
	reset_offsets(); descriptors[1].alignment = 8U;
	assert(QCX_ResolveOptionalEntityFields() && fofs_maxspeed == 0 && fofs_trackent == 36);
	descriptors[1].alignment = _Alignof(float);
	reset_offsets(); descriptors[1].access_flags = QCX_ENGINE_FIELD_HOST_READ;
	assert(QCX_ResolveOptionalEntityFields() && fofs_maxspeed == 0 && fofs_items2 == 4);
	descriptors[1].access_flags = QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE;
	reset_offsets(); descriptors[1].offset = memory.entity_stride;
	assert(QCX_ResolveOptionalEntityFields() && fofs_maxspeed == 0 && fofs_hideentity == 32);
	descriptors[1].offset = 8U;
	reset_offsets(); descriptors[1].access_flags |= UINT32_C(4);
	assert(QCX_ResolveOptionalEntityFields() && fofs_maxspeed == 0 && fofs_gravity == 12);
	descriptors[1].access_flags = QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE;
	reset_offsets(); descriptors[9].name = descriptors[1].name;
	assert(QCX_ResolveOptionalEntityFields() && fofs_maxspeed == 0 && fofs_hideentity == 32);
	descriptors[9].name = (qcx_abi_string_ref_v1_t){
		(qcx_guest_address_t)(uintptr_t)names[9], (uint32_t)strlen(names[9]), 0U};
	exports.entity_fields.count = 0U;
	assert(!QCX_ResolveOptionalEntityFields());
	exports.entity_fields.count = 10U;
	QCX_ClearEntities();
	return 0;
}
