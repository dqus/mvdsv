#include "qwsvdef.h"

#include "qcx/strings.h"

#include "qcx/adapter.h"
#include "qcx/entities.h"

#include <limits.h>
#include <string.h>

static qcx_legacy_string_projection_t
	legacy_string_projections[QCX_LEGACY_STRING_FIELD_COUNT];
static qcx_transport_kind_t legacy_string_transport;
static qbool legacy_string_projections_ready;

void QCX_SetLegacyStringProjections(
	const qcx_legacy_string_projection_t projections[QCX_LEGACY_STRING_FIELD_COUNT],
	qcx_transport_kind_t kind)
{
	if (projections == NULL || (kind != QCX_TRANSPORT_NATIVE
		&& kind != QCX_TRANSPORT_WASM)) {
		QCX_ClearLegacyStringProjections();
		return;
	}
	memcpy(legacy_string_projections, projections, sizeof(legacy_string_projections));
	legacy_string_transport = kind;
	legacy_string_projections_ready = true;
}

void QCX_ClearLegacyStringProjections(void)
{
	memset(legacy_string_projections, 0, sizeof(legacy_string_projections));
	legacy_string_transport = QCX_TRANSPORT_NONE;
	legacy_string_projections_ready = false;
}

static const char *QCX_MapProjectedStringData(const void *member, uint32_t length)
{
	if (member == NULL || length == 0U || length == UINT32_MAX) {
		return NULL;
	}
	if (legacy_string_transport == QCX_TRANSPORT_NATIVE) {
		char *data = NULL;
		memcpy(&data, member, sizeof(data));
		return data;
	}
	if (legacy_string_transport == QCX_TRANSPORT_WASM) {
		uint32_t offset = 0U;
		void *view = NULL;
		const qcx_game_api_v1_t *const game = QCX_Game();
		memcpy(&offset, member, sizeof(offset));
		if (offset == 0U || game == NULL || game->memory_view == NULL
			|| game->memory_view(game->context, offset, length + 1U, 1U, &view)
				!= QCX_PLUGIN_OK || view == NULL
			|| ((const char *)view)[length] != '\0') {
			return NULL;
		}
		return view;
	}
	return NULL;
}

const char *QCX_BorrowLegacyString(int32_t token)
{
	if (token == 0) {
		return "";
	}
	if (token < 0 || !legacy_string_projections_ready) {
		return NULL;
	}
	const uint32_t index = (uint32_t)token - 1U;
	const uint32_t slot = index / QCX_LEGACY_STRING_FIELD_COUNT;
	const uint32_t field = index % QCX_LEGACY_STRING_FIELD_COUNT;
	if (slot >= QCX_EntityCapacity()) {
		return NULL;
	}
	edict_t *const edict = QCX_SlotToEdict(slot);
	if (edict == NULL || edict->v == NULL) {
		return NULL;
	}
	const qcx_legacy_string_projection_t *const projection =
		&legacy_string_projections[field];
	const uint8_t *const base = (const uint8_t *)edict->v;
	uint32_t length = 0U;
	memcpy(&length, base + projection->length_offset, sizeof(length));
	if (length == 0U) {
		return "";
	}
	return QCX_MapProjectedStringData(base + projection->data_offset, length);
}
