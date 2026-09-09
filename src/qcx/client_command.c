#include <stddef.h>

#include "bothdefs.h"
#include "qcx/client_command.h"
#include "cmd.h"

#include <string.h>

static int QCX_BoundedStringLength(const char *value, size_t limit, size_t *out_size)
{
	if (value == NULL || out_size == NULL) {
		return 0;
	}
	for (size_t index = 0U; index <= limit; ++index) {
		if (value[index] == '\0') {
			*out_size = index;
			return 1;
		}
	}
	return 0;
}

static void QCX_WriteU32LE(uint8_t *out, uint32_t value)
{
	out[0] = (uint8_t)value;
	out[1] = (uint8_t)(value >> 8U);
	out[2] = (uint8_t)(value >> 16U);
	out[3] = (uint8_t)(value >> 24U);
}

int QCX_SnapshotClientCommand(qcx_client_command_payload_v1_t *out)
{
	if (out == NULL) {
		return 0;
	}
	memset(out, 0, sizeof(*out));

	const int argc = Cmd_Argc();
	if (argc < 0 || (uint32_t)argc > QCX_COMMAND_MAX_ARGS) {
		return 0;
	}

	const char *const raw_args = Cmd_Args();
	size_t raw_args_size;
	if (!QCX_BoundedStringLength(raw_args, QCX_COMMAND_MAX_RAW_ARGS_BYTES,
			&raw_args_size)) {
		return 0;
	}

	const size_t argc_u = (size_t)argc;
	const size_t raw_args_offset = QCX_COMMAND_PAYLOAD_V1_HEADER_SIZE
		+ argc_u * QCX_COMMAND_PAYLOAD_V1_ARG_SIZE_BYTES;
	size_t argv_offset = raw_args_offset + raw_args_size;
	size_t argv_bytes_size = 0U;
	if (raw_args_size != 0U) {
		memcpy(out->bytes + raw_args_offset, raw_args, raw_args_size);
	}

	for (size_t index = 0U; index < argc_u; ++index) {
		const char *const argument = Cmd_Argv((int)index);
		size_t argument_size;
		if (!QCX_BoundedStringLength(argument,
				QCX_COMMAND_MAX_ARGV_BYTES - argv_bytes_size, &argument_size)) {
			return 0;
		}
		QCX_WriteU32LE(out->bytes + QCX_COMMAND_PAYLOAD_V1_HEADER_SIZE
			+ index * QCX_COMMAND_PAYLOAD_V1_ARG_SIZE_BYTES,
			(uint32_t)argument_size);
		if (argument_size != 0U) {
			memcpy(out->bytes + argv_offset, argument, argument_size);
		}
		argv_offset += argument_size;
		argv_bytes_size += argument_size;
	}

	QCX_WriteU32LE(out->bytes, (uint32_t)argc_u);
	QCX_WriteU32LE(out->bytes + 4U, (uint32_t)raw_args_size);
	QCX_WriteU32LE(out->bytes + 8U, (uint32_t)argv_bytes_size);
	out->size = (qcx_byte_count_t)argv_offset;
	return 1;
}
