#include "qwsvdef.h"

#include "qcx/adapter.h"
#include "qcx/entities.h"
#include "qcx/strings.h"

#include <limits.h>
#include <string.h>

static void QCX_ClearStringView(qcx_string_view_t *view)
{
	if (view != NULL) {
		view->data = 0U;
		view->size = 0U;
	}
}

int QCX_ReadStringHeader(qcx_guest_address_t header, qcx_string_view_t *view)
{
	qcx_string_view_t result = {0U, 0U};
	const qcx_transport_kind_t kind = QCX_GameTransportKind();
	if (view == NULL || header == 0U) {
		QCX_ClearStringView(view);
		return 0;
	}
	if (kind == QCX_TRANSPORT_NATIVE) {
		const uint8_t *native_header;
		char *data = NULL;
		if (header > UINTPTR_MAX) {
			QCX_ClearStringView(view);
			return 0;
		}
		native_header = (const uint8_t *)(uintptr_t)header;
		memcpy(&data, native_header, sizeof(data));
		memcpy(&result.size, native_header + sizeof(data), sizeof(result.size));
		result.data = (qcx_guest_address_t)(uintptr_t)data;
	} else if (kind == QCX_TRANSPORT_WASM) {
		const qcx_game_api_v1_t *const game = QCX_Game();
		void *header_view = NULL;
		if (game == NULL || game->memory_view == NULL
			|| game->memory_view(game->context, header,
				sizeof(uint32_t) * 2U, _Alignof(uint32_t), &header_view) != QCX_PLUGIN_OK
			|| header_view == NULL) {
			QCX_ClearStringView(view);
			return 0;
		}
		memcpy(&result.data, header_view, sizeof(uint32_t));
		memcpy(&result.size, (const uint8_t *)header_view + sizeof(uint32_t),
			sizeof(result.size));
	} else {
		QCX_ClearStringView(view);
		return 0;
	}
	if (result.size == UINT32_MAX || (result.size != 0U && result.data == 0U)) {
		QCX_ClearStringView(view);
		return 0;
	}
	*view = result;
	return 1;
}

const char *QCX_BorrowStringView(const qcx_string_view_t *view)
{
	const qcx_transport_kind_t kind = QCX_GameTransportKind();
	if (view == NULL || view->size == UINT32_MAX) {
		return NULL;
	}
	if (view->size == 0U) {
		return "";
	}
	if (view->data == 0U) {
		return NULL;
	}
	if (kind == QCX_TRANSPORT_NATIVE) {
		if (view->data > UINTPTR_MAX) {
			return NULL;
		}
		const char *const value = (const char *)(uintptr_t)view->data;
		return value[view->size] == '\0' ? value : NULL;
	}
	if (kind == QCX_TRANSPORT_WASM) {
		const qcx_game_api_v1_t *const game = QCX_Game();
		void *payload = NULL;
		if (game == NULL || game->memory_view == NULL
			|| game->memory_view(game->context, view->data, view->size + 1U,
				1U, &payload) != QCX_PLUGIN_OK
			|| payload == NULL || ((const char *)payload)[view->size] != '\0') {
			return NULL;
		}
		return payload;
	}
	return NULL;
}

int QCX_ReadLegacyString(qcx_legacy_string_ref_t ref, qcx_string_view_t *view)
{
	qcx_guest_address_t header;
	if (view == NULL || ref < 0) {
		QCX_ClearStringView(view);
		return 0;
	}
	if (ref == 0) {
		QCX_ClearStringView(view);
		return 1;
	}
	if (!QCX_EntityStringHeaderAddress(ref, &header)) {
		QCX_ClearStringView(view);
		return 0;
	}
	return QCX_ReadStringHeader(header, view);
}

const char *QCX_BorrowLegacyString(qcx_legacy_string_ref_t ref)
{
	qcx_string_view_t view;
	if (!QCX_ReadLegacyString(ref, &view)) {
		return NULL;
	}
	return QCX_BorrowStringView(&view);
}
