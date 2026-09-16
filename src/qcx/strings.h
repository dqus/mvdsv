#ifndef MVDSV_QCX_STRINGS_H
#define MVDSV_QCX_STRINGS_H

#include "game/shared_abi.h"
#include "game/shared_entity_state.h"

#include <stdint.h>

typedef struct qcx_string_view_s {
	qcx_guest_address_t data;
	uint32_t size;
} qcx_string_view_t;

int QCX_ReadStringHeader(qcx_guest_address_t header, qcx_string_view_t *view);
const char *QCX_BorrowStringView(const qcx_string_view_t *view);
int QCX_ReadLegacyString(qcx_legacy_string_ref_t ref, qcx_string_view_t *view);
const char *QCX_BorrowLegacyString(qcx_legacy_string_ref_t ref);

#endif
