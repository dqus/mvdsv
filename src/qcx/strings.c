#include "qwsvdef.h"

#include "qcx/strings.h"

#include "qcx/adapter.h"
#include "qcx/entities.h"

#include <limits.h>
#include <stdlib.h>

enum { legacy_string_field_count = 11 };

typedef struct qcx_legacy_string_borrow_s {
	uint8_t *bytes;
	qcx_byte_count_t capacity;
} qcx_legacy_string_borrow_t;

static qcx_legacy_string_borrow_t *legacy_string_borrows;
static uint32_t legacy_string_borrow_count;

static int QCX_EnsureLegacyStringBorrows(void)
{
	const uint32_t entities = QCX_EntityCapacity();
	if (entities == 0U || entities > UINT32_MAX / legacy_string_field_count) {
		return 0;
	}
	const uint32_t count = entities * legacy_string_field_count;
	if (legacy_string_borrows != NULL) {
		return legacy_string_borrow_count == count;
	}
	legacy_string_borrows = calloc(count, sizeof(*legacy_string_borrows));
	if (legacy_string_borrows == NULL) {
		return 0;
	}
	legacy_string_borrow_count = count;
	return 1;
}

static int QCX_ResizeLegacyStringBorrow(qcx_legacy_string_borrow_t *borrow,
	qcx_byte_count_t bytes)
{
	if (bytes == UINT32_MAX) {
		return 0;
	}
	const qcx_byte_count_t capacity = bytes + 1U;
	if (borrow->capacity >= capacity) {
		return 1;
	}
	uint8_t *resized = realloc(borrow->bytes, capacity);
	if (resized == NULL) {
		return 0;
	}
	borrow->bytes = resized;
	borrow->capacity = capacity;
	return 1;
}

const char *QCX_BorrowLegacyString(int32_t token)
{
	if (token == 0) {
		return "";
	}
	if (token < 0 || !QCX_EnsureLegacyStringBorrows()
		|| (uint32_t)token > legacy_string_borrow_count) {
		return NULL;
	}
	const qcx_game_api_v1_t *const game = QCX_Game();
	if (game == NULL || game->legacy_string_read == NULL) {
		return NULL;
	}
	qcx_legacy_string_borrow_t *const borrow = &legacy_string_borrows[(uint32_t)token - 1U];
	qcx_byte_count_t required = 0U;
	qcx_plugin_status_t status = game->legacy_string_read(game->context, token, NULL,
		0U, &required);
	if ((status != QCX_PLUGIN_OK && status != QCX_PLUGIN_BUFFER_TOO_SMALL)
		|| !QCX_ResizeLegacyStringBorrow(borrow, required)) {
		return NULL;
	}
	for (unsigned int attempt = 0U; attempt != 2U; ++attempt) {
		qcx_byte_count_t bytes = 0U;
		status = game->legacy_string_read(game->context, token, borrow->bytes,
			borrow->capacity - 1U, &bytes);
		if (status == QCX_PLUGIN_OK) {
			if (bytes >= borrow->capacity) {
				return NULL;
			}
			borrow->bytes[bytes] = '\0';
			return (const char *)borrow->bytes;
		}
		if (status != QCX_PLUGIN_BUFFER_TOO_SMALL || !QCX_ResizeLegacyStringBorrow(borrow, bytes)) {
			return NULL;
		}
	}
	return NULL;
}

void QCX_ClearLegacyStringBorrows(void)
{
	for (uint32_t index = 0U; index < legacy_string_borrow_count; ++index) {
		free(legacy_string_borrows[index].bytes);
	}
	free(legacy_string_borrows);
	legacy_string_borrows = NULL;
	legacy_string_borrow_count = 0U;
}
