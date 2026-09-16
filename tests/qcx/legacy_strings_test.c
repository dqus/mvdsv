#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "qwsvdef.h"

#include "qcx/adapter.h"
#include "qcx/entities.h"
#include "qcx/strings.h"

typedef struct fixture_native_string_s {
	char *data;
	uint32_t size;
} fixture_native_string_t;

static fixture_native_string_t native_headers[2];
static uint8_t wasm_memory[96];
static qcx_transport_kind_t transport_kind;
static int wasm_memory_available;
static qcx_guest_address_t wasm_last_address;
static qcx_byte_count_t wasm_last_size;

static qcx_plugin_status_t fixture_memory_view(void *context,
	qcx_guest_address_t address, qcx_byte_count_t size, uint32_t alignment, void **out)
{
	(void)context;
	wasm_last_address = address;
	wasm_last_size = size;
	if (out == NULL || !wasm_memory_available || alignment == 0U
		|| address > sizeof(wasm_memory) || size > sizeof(wasm_memory) - address) {
		return QCX_PLUGIN_UNAVAILABLE;
	}
	*out = wasm_memory + address;
	return QCX_PLUGIN_OK;
}

static const qcx_game_api_v1_t game = {
	.abi_version = QCX_PLUGIN_ABI_VERSION_V1,
	.struct_size = sizeof(game),
	.memory_view = fixture_memory_view,
};

const qcx_game_api_v1_t *QCX_Game(void)
{
	return &game;
}

qcx_transport_kind_t QCX_GameTransportKind(void)
{
	return transport_kind;
}

int QCX_EntityStringHeaderAddress(qcx_legacy_string_ref_t ref,
	qcx_guest_address_t *header)
{
	if (header == NULL || ref <= 0 || ref > 2) {
		return 0;
	}
	if (transport_kind == QCX_TRANSPORT_NATIVE) {
		*header = (qcx_guest_address_t)(uintptr_t)&native_headers[ref - 1];
	} else if (transport_kind == QCX_TRANSPORT_WASM) {
		*header = ref == 1 ? 16U : 24U;
	} else {
		return 0;
	}
	return 1;
}

static void set_wasm_header(uint32_t header, uint32_t data, uint32_t size)
{
	memcpy(wasm_memory + header, &data, sizeof(data));
	memcpy(wasm_memory + header + sizeof(data), &size, sizeof(size));
}

static void test_native_headers(void)
{
	char first[] = "first";
	char replacement[] = "fresh";
	char missing_nul[] = {'n', 'u', 'l', 'x'};
	qcx_string_view_t view;
	transport_kind = QCX_TRANSPORT_NATIVE;
	native_headers[0] = (fixture_native_string_t){first, 5U};
	native_headers[1] = (fixture_native_string_t){NULL, 0U};

	assert(QCX_ReadStringHeader((qcx_guest_address_t)(uintptr_t)&native_headers[0], &view));
	assert(view.data == (qcx_guest_address_t)(uintptr_t)first && view.size == 5U);
	assert(QCX_BorrowStringView(&view) == first);
	assert(QCX_ReadLegacyString(1, &view));
	assert(QCX_BorrowStringView(&view) == first);
	assert(QCX_ReadLegacyString(0, &view));
	assert(view.data == 0U && view.size == 0U);
	assert(strcmp(QCX_BorrowStringView(&view), "") == 0);
	assert(!QCX_ReadLegacyString(-1, &view));
	assert(!QCX_ReadLegacyString(3, &view));

	native_headers[0].data = replacement;
	native_headers[0].size = 5U;
	assert(QCX_ReadLegacyString(1, &view));
	assert(QCX_BorrowStringView(&view) == replacement);

	native_headers[0] = (fixture_native_string_t){NULL, 0U};
	assert(QCX_ReadStringHeader((qcx_guest_address_t)(uintptr_t)&native_headers[0], &view));
	assert(strcmp(QCX_BorrowStringView(&view), "") == 0);
	native_headers[0] = (fixture_native_string_t){NULL, 1U};
	assert(!QCX_ReadStringHeader((qcx_guest_address_t)(uintptr_t)&native_headers[0], &view));
	native_headers[0] = (fixture_native_string_t){missing_nul, 3U};
	assert(QCX_ReadStringHeader((qcx_guest_address_t)(uintptr_t)&native_headers[0], &view));
	assert(QCX_BorrowStringView(&view) == NULL);
	native_headers[0] = (fixture_native_string_t){first, UINT32_MAX};
	assert(!QCX_ReadStringHeader((qcx_guest_address_t)(uintptr_t)&native_headers[0], &view));
}

static void test_wasm_headers(void)
{
	qcx_string_view_t view;
	transport_kind = QCX_TRANSPORT_WASM;
	wasm_memory_available = 1;
	memset(wasm_memory, 0, sizeof(wasm_memory));
	memcpy(wasm_memory + 40U, "wasm", sizeof("wasm"));
	set_wasm_header(16U, 40U, 4U);

	assert(QCX_ReadStringHeader(16U, &view));
	assert(view.data == 40U && view.size == 4U);
	assert(QCX_BorrowStringView(&view) == (const char *)(wasm_memory + 40U));
	assert(wasm_last_address == 40U && wasm_last_size == 5U);
	assert(QCX_ReadLegacyString(1, &view));
	assert(QCX_BorrowStringView(&view) == (const char *)(wasm_memory + 40U));

	set_wasm_header(16U, 0U, 0U);
	assert(QCX_ReadStringHeader(16U, &view));
	assert(strcmp(QCX_BorrowStringView(&view), "") == 0);
	set_wasm_header(16U, 0U, 1U);
	assert(!QCX_ReadStringHeader(16U, &view));
	set_wasm_header(16U, 40U, UINT32_MAX);
	assert(!QCX_ReadStringHeader(16U, &view));
	set_wasm_header(16U, 40U, 4U);
	wasm_memory[44U] = 'x';
	assert(QCX_ReadStringHeader(16U, &view));
	assert(QCX_BorrowStringView(&view) == NULL);
	set_wasm_header(16U, sizeof(wasm_memory) - 2U, 2U);
	assert(QCX_ReadStringHeader(16U, &view));
	assert(QCX_BorrowStringView(&view) == NULL);
	assert(!QCX_ReadStringHeader(sizeof(wasm_memory) - 4U, &view));
	wasm_memory_available = 0;
	assert(!QCX_ReadStringHeader(16U, &view));
}

int main(void)
{
	test_native_headers();
	test_wasm_headers();
	return 0;
}
