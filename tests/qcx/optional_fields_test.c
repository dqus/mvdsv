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

typedef struct fixture_string_s {
	char *data;
	uint32_t size;
} fixture_string_t;

typedef struct fixture_entity_s {
	qcx_shared_entity_state_v1_t shared;
	fixture_string_t model_string;
} fixture_entity_t;

enum { optional_field_count = 10 };

server_t sv;
int fofs_items2, fofs_maxspeed, fofs_gravity, fofs_movement, fofs_vw_index;
int fofs_hideentity, fofs_trackent, fofs_visibility, fofs_hide_players, fofs_teleported;

static qcx_game_entity_memory_v1_t memory;
static qcx_engine_field_exports_v1_t exports;
static qcx_engine_field_descriptor_v1_t descriptors[optional_field_count];
static char names[optional_field_count][32];
static fixture_entity_t entities[2];
static int reject_whole_storage;
static qcx_guest_address_t rejected_address;

void SV_Error(char *error, ...)
{
	(void)error;
	assert(!"unexpected SV_Error");
}

const qcx_game_api_v1_t *QCX_Game(void);

qcx_transport_kind_t QCX_GameTransportKind(void)
{
	return QCX_TRANSPORT_NATIVE;
}

static qcx_plugin_status_t memory_view(void *context, qcx_guest_address_t address,
	qcx_byte_count_t size, uint32_t alignment, void **out)
{
	(void)context;
	if (address == 0U || size == 0U || alignment == 0U || address % alignment != 0U) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	if (address == rejected_address) {
		return QCX_PLUGIN_UNAVAILABLE;
	}
	if (reject_whole_storage
		&& address == (qcx_guest_address_t)(uintptr_t)entities
		&& size == sizeof(entities)) {
		return QCX_PLUGIN_UNAVAILABLE;
	}
	*out = (void *)(uintptr_t)address;
	return QCX_PLUGIN_OK;
}

static qcx_guest_address_t engine_fields(void *context)
{
	(void)context;
	return (qcx_guest_address_t)(uintptr_t)&exports;
}

static const qcx_game_api_v1_t game = {
	.abi_version = QCX_PLUGIN_ABI_VERSION_V1,
	.struct_size = sizeof(game),
	.memory_view = memory_view,
	.engine_fields = engine_fields,
};

const qcx_game_api_v1_t *QCX_Game(void)
{
	return &game;
}

static qcx_engine_type_id_t type_id(const char *name)
{
	qcx_engine_type_id_t value = UINT64_C(14695981039346656037);
	while (*name != '\0') {
		value ^= (uint8_t)*name++;
		value *= UINT64_C(1099511628211);
	}
	return value;
}

static void descriptor(uint32_t index, const char *name, const char *type,
	uint32_t offset, uint32_t size, uint32_t access)
{
	assert(snprintf(names[index], sizeof(names[index]), "%s", name) > 0);
	descriptors[index] = (qcx_engine_field_descriptor_v1_t){
		.name = {(qcx_guest_address_t)(uintptr_t)names[index],
			(uint32_t)strlen(names[index]), 0U},
		.type_id = type_id(type),
		.offset = offset,
		.size = size,
		.alignment = _Alignof(float),
		.access_flags = access,
	};
}

static void reset_offsets(void)
{
	fofs_items2 = fofs_maxspeed = fofs_gravity = fofs_movement = fofs_vw_index = 0;
	fofs_hideentity = fofs_trackent = fofs_visibility = fofs_hide_players = 0;
	fofs_teleported = 0;
}

int main(void)
{
	char model[] = "progs/player.mdl";
	memory = (qcx_game_entity_memory_v1_t){
		.abi_version = QCX_GAME_ENTITY_MEMORY_ABI_VERSION_V1,
		.struct_size = sizeof(memory),
		.shared_state_base = (qcx_guest_address_t)(uintptr_t)entities,
		.entity_object_base = (qcx_guest_address_t)(uintptr_t)entities,
		.entity_stride = sizeof(entities[0]),
		.max_entities = 2U,
		.shared_state_abi_version = QCX_SHARED_ENTITY_STATE_ABI_VERSION_V1,
	};
	descriptor(0U, "items2", "qc.f32", 4U, sizeof(float),
		QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(1U, "maxspeed", "qc.f32", 8U, sizeof(float),
		QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(2U, "gravity", "qc.f32", 12U, sizeof(float),
		QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(3U, "movement", "qc.vec3f", 16U, 3U * sizeof(float),
		QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(4U, "vw_index", "qc.f32", 28U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ);
	descriptor(5U, "hideentity", "qc.entity", 32U, sizeof(uint32_t),
		QCX_ENGINE_FIELD_HOST_READ);
	descriptor(6U, "trackent", "qc.entity", 36U, sizeof(uint32_t),
		QCX_ENGINE_FIELD_HOST_READ);
	descriptor(7U, "visclients", "qc.f32", 40U, sizeof(float),
		QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	descriptor(8U, "hideplayers", "qc.f32", 44U, sizeof(float), QCX_ENGINE_FIELD_HOST_READ);
	descriptor(9U, "teleported", "qc.f32", 48U, sizeof(float),
		QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	exports = (qcx_engine_field_exports_v1_t){
		.abi_version = QCX_ENGINE_FIELD_EXPORTS_ABI_VERSION_V1,
		.struct_size = sizeof(exports),
		.entity_fields = {(qcx_guest_address_t)(uintptr_t)descriptors,
			optional_field_count, sizeof(descriptors[0])},
	};
	entities[1].shared.model = (qcx_legacy_string_ref_t)(sizeof(entities[0])
		+ offsetof(fixture_entity_t, model_string));
	entities[1].model_string = (fixture_string_t){model, (uint32_t)strlen(model)};
	assert(QCX_ConfigureEntities((qcx_guest_address_t)(uintptr_t)&memory));
	assert(QCX_ResolveEntityFields());
	assert(QCX_BindEntities());
	qcx_guest_address_t header = 0U;
	assert(QCX_EntityStringHeaderAddress(entities[1].shared.model, &header));
	assert(header == memory.entity_object_base + (uint32_t)entities[1].shared.model);
	assert(!QCX_EntityStringHeaderAddress(0, &header));
	assert(!QCX_EntityStringHeaderAddress(-1, &header));
	assert(!QCX_EntityStringHeaderAddress((qcx_legacy_string_ref_t)(
		memory.entity_stride * memory.max_entities - 4U), &header));
	assert(QCX_EntityHasModel(&sv.edicts[1]));
	entities[1].model_string.size = 0U;
	assert(!QCX_EntityHasModel(&sv.edicts[1]));
	assert(fofs_items2 == 4 && fofs_maxspeed == 8 && fofs_gravity == 12);
	assert(fofs_movement == 16 && fofs_vw_index == 28 && fofs_hideentity == 32);
	assert(fofs_trackent == 36 && fofs_visibility == 40 && fofs_hide_players == 44);
	assert(fofs_teleported == 48);

	reset_offsets();
	descriptors[1].offset = 0U;
	assert(!QCX_ResolveEntityFields());
	assert(strcmp(QCX_EntityFieldError(), "maxspeed") == 0);
	descriptors[1].offset = 8U;

	reset_offsets();
	descriptors[1].type_id = type_id("qc.entity");
	assert(!QCX_ResolveEntityFields());
	descriptors[1].type_id = type_id("qc.f32");
	reset_offsets();
	descriptors[1].size = 8U;
	assert(!QCX_ResolveEntityFields());
	descriptors[1].size = sizeof(float);
	reset_offsets();
	descriptors[1].alignment = 8U;
	assert(!QCX_ResolveEntityFields());
	descriptors[1].alignment = _Alignof(float);
	reset_offsets();
	descriptors[1].access_flags = QCX_ENGINE_FIELD_HOST_READ;
	assert(!QCX_ResolveEntityFields());
	descriptors[1].access_flags = QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE;
	reset_offsets();
	descriptors[1].offset = memory.entity_stride;
	assert(!QCX_ResolveEntityFields());
	descriptors[1].offset = 8U;
	reset_offsets();
	descriptors[1].access_flags |= UINT32_C(4);
	assert(!QCX_ResolveEntityFields());
	descriptors[1].access_flags = QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE;
	reset_offsets();
	descriptors[9].name = descriptors[1].name;
	assert(!QCX_ResolveEntityFields());
	/* An invalid duplicate must not turn into an absent optional field. */
	descriptors[9].type_id = 0U;
	assert(!QCX_ResolveEntityFields());
	descriptors[9].type_id = type_id("qc.f32");
	descriptors[9].name = (qcx_abi_string_ref_v1_t){
		(qcx_guest_address_t)(uintptr_t)names[9], (uint32_t)strlen(names[9]), 0U};

	descriptors[1].name.reserved0 = 1U;
	assert(!QCX_ResolveEntityFields());
	descriptors[1].name.reserved0 = 0U;
	names[1][3] = '\0';
	assert(!QCX_ResolveEntityFields());
	names[1][3] = 's';
	exports.reserved0 = 1U;
	assert(!QCX_ResolveEntityFields());
	exports.reserved0 = 0U;
	exports.struct_size += 8U;
	assert(!QCX_ResolveEntityFields());
	exports.struct_size = sizeof(exports);
	exports.entity_fields.descriptor_stride = sizeof(descriptors[0]) - 1U;
	assert(!QCX_ResolveEntityFields());
	exports.entity_fields.descriptor_stride = sizeof(descriptors[0]);
	exports.entity_fields.descriptor_stride += 8U;
	assert(!QCX_ResolveEntityFields());
	exports.entity_fields.descriptor_stride = sizeof(descriptors[0]);
	exports.entity_fields.count = UINT32_MAX;
	assert(!QCX_ResolveEntityFields());
	exports.entity_fields.count = optional_field_count;

	rejected_address = exports.entity_fields.descriptors;
	assert(!QCX_ResolveEntityFields());
	rejected_address = descriptors[1].name.data;
	assert(!QCX_ResolveEntityFields());
	rejected_address = memory.entity_object_base + descriptors[1].offset;
	assert(!QCX_ResolveEntityFields());
	rejected_address = 0U;

	/* Unknown names do not need to match one of MVDSV's field types. */
	descriptor(9U, "custom_mask", "qc.custom", 48U, sizeof(float),
		QCX_ENGINE_FIELD_HOST_READ);
	reset_offsets();
	assert(QCX_ResolveEntityFields());
	assert(fofs_maxspeed == 8 && fofs_teleported == 0);
	descriptor(9U, "teleported", "qc.f32", 48U, sizeof(float),
		QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE);
	assert(QCX_ResolveEntityFields());
	assert(fofs_teleported == 48);
	/* A bad final field must not partially replace the published offsets. */
	descriptors[1].offset = 52U;
	descriptors[9].access_flags = QCX_ENGINE_FIELD_HOST_READ;
	assert(!QCX_ResolveEntityFields());
	assert(fofs_maxspeed == 8 && fofs_teleported == 48);
	descriptors[1].offset = 8U;
	descriptors[9].access_flags = QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE;

	reject_whole_storage = 1;
	assert(!QCX_BindEntities());
	reject_whole_storage = 0;
	assert(QCX_BindEntities());
	exports.entity_fields = (qcx_engine_field_table_v1_t){.descriptor_stride = 1U};
	assert(!QCX_ResolveEntityFields());
	exports.entity_fields = (qcx_engine_field_table_v1_t){
		.descriptors = (qcx_guest_address_t)(uintptr_t)descriptors};
	assert(!QCX_ResolveEntityFields());
	exports.entity_fields = (qcx_engine_field_table_v1_t){0};
	reset_offsets();
	assert(QCX_ResolveEntityFields());
	assert(fofs_maxspeed == 0 && fofs_teleported == 0);
	QCX_ClearEntities();
	return 0;
}
