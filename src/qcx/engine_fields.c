#include "qcx/engine_fields.h"

#include <string.h>

static qcx_engine_type_id_t QCX_EngineTypeId(const char *name)
{
	qcx_engine_type_id_t result = UINT64_C(14695981039346656037);
	while (*name != '\0') {
		result ^= (uint8_t)*name++;
		result *= UINT64_C(1099511628211);
	}
	return result;
}

int QCX_ReadEngineFieldExports(const qcx_game_api_v1_t *game,
	qcx_engine_field_exports_v1_t *exports)
{
	if (game == NULL || game->engine_fields == NULL || game->memory_view == NULL) {
		return 0;
	}
	const qcx_guest_address_t address = game->engine_fields(game->context);
	void *memory = NULL;
	if (address == 0U || address > UINT64_MAX - sizeof(*exports)
		|| game->memory_view(game->context, address, sizeof(*exports),
			_Alignof(qcx_engine_field_exports_v1_t), &memory) != QCX_PLUGIN_OK
		|| memory == NULL) {
		return 0;
	}
	memcpy(exports, memory, sizeof(*exports));
	return exports->abi_version == QCX_ENGINE_FIELD_EXPORTS_ABI_VERSION_V1
		&& exports->struct_size == sizeof(*exports) && exports->reserved0 == 0U;
}

int QCX_OpenEngineFieldTable(const qcx_game_api_v1_t *game,
	const qcx_engine_field_table_v1_t *table, qcx_engine_field_table_view_t *view)
{
	*view = (qcx_engine_field_table_view_t){0};
	if (game == NULL || game->memory_view == NULL) {
		return 0;
	}
	if (table->count == 0U
		&& (table->descriptors != 0U || table->descriptor_stride != 0U)) {
		return 0;
	}
	void *memory = NULL;
	if (table->count != 0U) {
		const uint64_t bytes = (uint64_t)table->count * sizeof(qcx_engine_field_descriptor_v1_t);
		if (table->descriptors == 0U
			|| table->descriptor_stride != sizeof(qcx_engine_field_descriptor_v1_t)
			|| bytes > UINT32_MAX || table->descriptors > UINT64_MAX - bytes
			|| game->memory_view(game->context, table->descriptors, (qcx_byte_count_t)bytes,
				_Alignof(qcx_engine_field_descriptor_v1_t), &memory) != QCX_PLUGIN_OK
			|| memory == NULL) {
			return 0;
		}
	}
	*view = (qcx_engine_field_table_view_t){game, memory, table->count};
	return 1;
}

qcx_engine_field_result_t QCX_ResolveEngineField(const qcx_engine_field_table_view_t *view,
	const qcx_engine_field_spec_t *spec, qcx_guest_address_t object_base,
	qcx_byte_count_t object_size, qcx_engine_field_binding_t *binding)
{
	*binding = (qcx_engine_field_binding_t){0};
	const qcx_game_api_v1_t *const game = view->game;
	const size_t name_size = strlen(spec->name);
	const qcx_engine_field_descriptor_v1_t *matched = NULL;
	for (uint32_t index = 0U; index < view->count; ++index) {
		const qcx_engine_field_descriptor_v1_t *const descriptor = &view->descriptors[index];
		const qcx_abi_string_ref_v1_t *const name = &descriptor->name;
		void *name_view = NULL;
		if (name->data == 0U || name->size == 0U || name->reserved0 != 0U
			|| name->data > UINT64_MAX - name->size
			|| game->memory_view(game->context, name->data, name->size, 1U, &name_view)
				!= QCX_PLUGIN_OK || name_view == NULL
			|| memchr(name_view, '\0', name->size) != NULL) {
			return QCX_ENGINE_FIELD_INVALID;
		}
		if (name->size != name_size || memcmp(name_view, spec->name, name_size) != 0) {
			continue;
		}
		if (matched != NULL) {
			return QCX_ENGINE_FIELD_INVALID;
		}
		matched = descriptor;
	}
	if (matched == NULL) {
		return QCX_ENGINE_FIELD_ABSENT;
	}
	if (matched->type_id != QCX_EngineTypeId(spec->type_name)
		|| matched->size != spec->size || matched->alignment != spec->alignment
		|| matched->alignment == 0U || matched->offset % matched->alignment != 0U
		|| (matched->access_flags & ~(QCX_ENGINE_FIELD_HOST_READ | QCX_ENGINE_FIELD_HOST_WRITE)) != 0U
		|| (matched->access_flags & spec->required_access) != spec->required_access
		|| object_base == 0U || object_base > UINT64_MAX - object_size
		|| object_size < matched->size || matched->offset > object_size - matched->size) {
		return QCX_ENGINE_FIELD_INVALID;
	}
	void *field_view = NULL;
	if (game->memory_view(game->context, object_base + matched->offset,
		matched->size, matched->alignment, &field_view) != QCX_PLUGIN_OK || field_view == NULL) {
		return QCX_ENGINE_FIELD_INVALID;
	}
	*binding = (qcx_engine_field_binding_t){matched->offset, field_view};
	return QCX_ENGINE_FIELD_FOUND;
}
