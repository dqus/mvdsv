#ifndef MVDSV_QCX_ENGINE_FIELDS_H
#define MVDSV_QCX_ENGINE_FIELDS_H

#include "game/plugin_api.h"

typedef struct qcx_engine_field_table_view_s {
	const qcx_game_api_v1_t *game;
	const qcx_engine_field_descriptor_v1_t *descriptors;
	uint32_t count;
} qcx_engine_field_table_view_t;

typedef struct qcx_engine_field_spec_s {
	const char *name;
	const char *type_name;
	uint32_t size;
	uint32_t alignment;
	uint32_t required_access;
} qcx_engine_field_spec_t;

typedef struct qcx_engine_field_binding_s {
	uint32_t offset;
	void *address;
} qcx_engine_field_binding_t;

typedef enum qcx_engine_field_result_e {
	QCX_ENGINE_FIELD_INVALID = -1,
	QCX_ENGINE_FIELD_ABSENT = 0,
	QCX_ENGINE_FIELD_FOUND = 1,
} qcx_engine_field_result_t;

int QCX_ReadEngineFieldExports(const qcx_game_api_v1_t *game,
	qcx_engine_field_exports_v1_t *exports);
/* Current layout only; an empty table has all three members zero. */
int QCX_OpenEngineFieldTable(const qcx_game_api_v1_t *game,
	const qcx_engine_field_table_v1_t *table, qcx_engine_field_table_view_t *view);
/* Names must be readable, nonempty and NUL-free. Unknown fields are not
 * type-checked; a matching name must be unique and valid. Views/bindings are
 * borrowed and must not survive a game call that can invalidate guest memory. */
qcx_engine_field_result_t QCX_ResolveEngineField(const qcx_engine_field_table_view_t *view,
	const qcx_engine_field_spec_t *spec, qcx_guest_address_t object_base,
	qcx_byte_count_t object_size, qcx_engine_field_binding_t *binding);

#endif
