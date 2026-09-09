#include "qcx/client_command.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static int source_argc;
static const char *source_argv[QCX_COMMAND_MAX_ARGS];
static const char *source_raw_args;
static int argc_calls;
static int argv_calls;
static int args_calls;

int Cmd_Argc(void)
{
	++argc_calls;
	return source_argc;
}

char *Cmd_Argv(int index)
{
	++argv_calls;
	return (char *)source_argv[index];
}

char *Cmd_Args(void)
{
	++args_calls;
	return (char *)source_raw_args;
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U)
		| ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static void reset_source(void)
{
	source_argc = 0;
	memset(source_argv, 0, sizeof(source_argv));
	source_raw_args = "";
	argc_calls = 0;
	argv_calls = 0;
	args_calls = 0;
}

static void test_exact_qw_snapshot(void)
{
	qcx_client_command_payload_v1_t payload;
	reset_source();
	source_argc = 3;
	source_argv[0] = "give";
	source_argv[1] = "shells";
	source_argv[2] = "25";
	source_raw_args = "  \"shells\" \t25";

	assert(QCX_SnapshotClientCommand(&payload));
	assert(argc_calls == 1 && args_calls == 1 && argv_calls == 3);
	assert(read_u32_le(payload.bytes) == 3U);
	assert(read_u32_le(payload.bytes + 4U) == strlen(source_raw_args));
	assert(read_u32_le(payload.bytes + 8U) == 12U);
	assert(read_u32_le(payload.bytes + 12U) == 4U);
	assert(read_u32_le(payload.bytes + 16U) == 6U);
	assert(read_u32_le(payload.bytes + 20U) == 2U);
	const size_t raw_offset = QCX_COMMAND_PAYLOAD_V1_HEADER_SIZE + 3U * 4U;
	assert(!memcmp(payload.bytes + raw_offset, source_raw_args,
		strlen(source_raw_args)));
	const size_t argv_offset = raw_offset + strlen(source_raw_args);
	assert(!memcmp(payload.bytes + argv_offset, "giveshells25", 12U));
	assert(payload.size == argv_offset + 12U);

	const qcx_byte_count_t saved_size = payload.size;
	const uint8_t saved_first = payload.bytes[argv_offset];
	source_argv[0] = "mutated";
	source_raw_args = "mutated";
	assert(argc_calls == 1 && args_calls == 1 && argv_calls == 3);
	assert(payload.size == saved_size && payload.bytes[argv_offset] == saved_first);
}

static void test_empty_argv_and_zero_argument_snapshots(void)
{
	qcx_client_command_payload_v1_t payload;
	reset_source();
	source_argc = 3;
	source_argv[0] = "command";
	source_argv[1] = "";
	source_argv[2] = "x";
	source_raw_args = " \"\" x";
	assert(QCX_SnapshotClientCommand(&payload));
	assert(read_u32_le(payload.bytes) == 3U);
	assert(read_u32_le(payload.bytes + 12U) == 7U);
	assert(read_u32_le(payload.bytes + 16U) == 0U);
	assert(read_u32_le(payload.bytes + 20U) == 1U);
	assert(read_u32_le(payload.bytes + 8U) == 8U);

	reset_source();
	assert(QCX_SnapshotClientCommand(&payload));
	assert(argc_calls == 1 && args_calls == 1 && argv_calls == 0);
	assert(payload.size == QCX_COMMAND_PAYLOAD_V1_HEADER_SIZE);
	assert(read_u32_le(payload.bytes) == 0U);
	assert(read_u32_le(payload.bytes + 4U) == 0U);
	assert(read_u32_le(payload.bytes + 8U) == 0U);
}

static void test_limits_and_invalid_parser_state(void)
{
	qcx_client_command_payload_v1_t payload;
	char max_raw[QCX_COMMAND_MAX_RAW_ARGS_BYTES + 1U];
	memset(max_raw, 'r', sizeof(max_raw) - 1U);
	max_raw[sizeof(max_raw) - 1U] = '\0';
	reset_source();
	source_raw_args = max_raw;
	assert(QCX_SnapshotClientCommand(&payload));
	assert(read_u32_le(payload.bytes + 4U) == QCX_COMMAND_MAX_RAW_ARGS_BYTES);
	assert(payload.size == QCX_COMMAND_PAYLOAD_V1_HEADER_SIZE
		+ QCX_COMMAND_MAX_RAW_ARGS_BYTES);

	char max_argv[QCX_COMMAND_MAX_ARGV_BYTES + 1U];
	memset(max_argv, 'a', sizeof(max_argv) - 1U);
	max_argv[sizeof(max_argv) - 1U] = '\0';
	reset_source();
	source_argc = 1;
	source_argv[0] = max_argv;
	assert(QCX_SnapshotClientCommand(&payload));
	assert(read_u32_le(payload.bytes + 8U) == QCX_COMMAND_MAX_ARGV_BYTES);

	reset_source();
	source_argc = QCX_COMMAND_MAX_ARGS;
	for (uint32_t index = 0U; index < QCX_COMMAND_MAX_ARGS; ++index) {
		source_argv[index] = "";
	}
	assert(QCX_SnapshotClientCommand(&payload));
	assert(argv_calls == (int)QCX_COMMAND_MAX_ARGS);
	assert(payload.size == QCX_COMMAND_PAYLOAD_V1_HEADER_SIZE
		+ QCX_COMMAND_MAX_ARGS * QCX_COMMAND_PAYLOAD_V1_ARG_SIZE_BYTES);

	char too_long_raw[QCX_COMMAND_MAX_RAW_ARGS_BYTES + 2U];
	memset(too_long_raw, 'r', sizeof(too_long_raw) - 1U);
	too_long_raw[sizeof(too_long_raw) - 1U] = '\0';
	reset_source();
	source_raw_args = too_long_raw;
	assert(!QCX_SnapshotClientCommand(&payload));
	assert(argc_calls == 1 && args_calls == 1 && argv_calls == 0);

	char too_long_argv[QCX_COMMAND_MAX_ARGV_BYTES + 2U];
	memset(too_long_argv, 'a', sizeof(too_long_argv) - 1U);
	too_long_argv[sizeof(too_long_argv) - 1U] = '\0';
	reset_source();
	source_argc = 1;
	source_argv[0] = too_long_argv;
	assert(!QCX_SnapshotClientCommand(&payload));
	assert(argc_calls == 1 && args_calls == 1 && argv_calls == 1);

	reset_source();
	source_argc = 1;
	source_argv[0] = NULL;
	assert(!QCX_SnapshotClientCommand(&payload));
	assert(argc_calls == 1 && args_calls == 1 && argv_calls == 1);

	reset_source();
	source_raw_args = NULL;
	assert(!QCX_SnapshotClientCommand(&payload));
	assert(argc_calls == 1 && args_calls == 1 && argv_calls == 0);

	reset_source();
	source_argc = -1;
	assert(!QCX_SnapshotClientCommand(&payload));
	assert(argc_calls == 1 && args_calls == 0 && argv_calls == 0);

	reset_source();
	source_argc = (int)QCX_COMMAND_MAX_ARGS + 1;
	assert(!QCX_SnapshotClientCommand(&payload));
	assert(argc_calls == 1 && args_calls == 0 && argv_calls == 0);
}

int main(void)
{
	test_exact_qw_snapshot();
	test_empty_argv_and_zero_argument_snapshots();
	test_limits_and_invalid_parser_state();
	return 0;
}
