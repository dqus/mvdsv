#ifndef MVDSV_QCX_STRINGS_H
#define MVDSV_QCX_STRINGS_H

#include "qcx/transport.h"

#include <stdint.h>

enum {
	QCX_LEGACY_STRING_FIELD_COUNT = 11,
	QCX_LEGACY_MODEL_FIELD_INDEX = 1
};

typedef struct qcx_legacy_string_projection_s {
	uint32_t data_offset;
	uint32_t length_offset;
} qcx_legacy_string_projection_t;

const char *QCX_BorrowLegacyString(int32_t token);
void QCX_SetLegacyStringProjections(
	const qcx_legacy_string_projection_t projections[QCX_LEGACY_STRING_FIELD_COUNT],
	qcx_transport_kind_t kind);
void QCX_ClearLegacyStringProjections(void);

#endif
