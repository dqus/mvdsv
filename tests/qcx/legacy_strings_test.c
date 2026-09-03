#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "game/plugin_api.h"

/* Task 9 contract: implemented by src/qcx/strings.c. */
const char *QCX_BorrowLegacyString(int32_t token);
void QCX_ClearLegacyStringBorrows(void);

static uint8_t values[23][64];
static qcx_byte_count_t value_sizes[23];
static uint32_t read_count;
static int nested_read;
static int grow_during_read;

uint32_t QCX_EntityCapacity(void)
{
	return 2U;
}

static qcx_plugin_status_t fixture_legacy_string_read(void *context, int32_t token,
	uint8_t *out, qcx_byte_count_t capacity, qcx_byte_count_t *required)
{
	(void)context;
	if (token <= 0 || token > 22 || token == 7) {
		return QCX_PLUGIN_UNAVAILABLE;
	}
	++read_count;
	if (nested_read && token == 1) {
		nested_read = 0;
		assert(strcmp(QCX_BorrowLegacyString(2), "second") == 0);
	}
	if (grow_during_read && token == 1 && out != NULL) {
		grow_during_read = 0;
		memcpy(values[1], "expanded", 8U);
		value_sizes[1] = 8U;
	}
	if (required != NULL) {
		*required = value_sizes[token];
	}
	if (capacity < value_sizes[token]) {
		return QCX_PLUGIN_BUFFER_TOO_SMALL;
	}
	if (value_sizes[token] != 0U && out != NULL) {
		memcpy(out, values[token], value_sizes[token]);
	}
	return QCX_PLUGIN_OK;
}

static const qcx_game_api_v1_t game = {
	.abi_version = QCX_PLUGIN_ABI_VERSION_V1,
	.struct_size = sizeof(game),
	.legacy_string_read = fixture_legacy_string_read,
};

const qcx_game_api_v1_t *QCX_Game(void)
{
	return &game;
}

static void set_value(int32_t token, const char *value)
{
	value_sizes[token] = (qcx_byte_count_t)strlen(value);
	memcpy(values[token], value, value_sizes[token]);
}

int main(void)
{
	set_value(1, "first");
	set_value(2, "second");
	set_value(22, "last");

	assert(strcmp(QCX_BorrowLegacyString(0), "") == 0);
	const char *const first = QCX_BorrowLegacyString(1);
	const char *const last = QCX_BorrowLegacyString(22);
	assert(first != last);
	assert(strcmp(first, "first") == 0);
	assert(strcmp(last, "last") == 0);
	assert(QCX_BorrowLegacyString(23) == NULL);
	assert(QCX_BorrowLegacyString(7) == NULL);

	const uint32_t before_mutation = read_count;
	set_value(1, "changed");
	assert(strcmp(QCX_BorrowLegacyString(1), "changed") == 0);
	assert(read_count > before_mutation);

	grow_during_read = 1;
	set_value(1, "short");
	assert(strcmp(QCX_BorrowLegacyString(1), "expanded") == 0);

	nested_read = 1;
	assert(strcmp(QCX_BorrowLegacyString(1), "expanded") == 0);

	QCX_ClearLegacyStringBorrows();
	set_value(1, "after-teardown");
	assert(strcmp(QCX_BorrowLegacyString(1), "after-teardown") == 0);
	QCX_ClearLegacyStringBorrows();
	return 0;
}
