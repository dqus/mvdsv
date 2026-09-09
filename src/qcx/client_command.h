#ifndef MVDSV_QC2CPP_CLIENT_COMMAND_H
#define MVDSV_QC2CPP_CLIENT_COMMAND_H

#include "qcx/transport.h"

#define QCX_CLIENT_COMMAND_PAYLOAD_V1_MAX_SIZE \
	(QCX_COMMAND_PAYLOAD_V1_HEADER_SIZE \
		+ QCX_COMMAND_MAX_ARGS * QCX_COMMAND_PAYLOAD_V1_ARG_SIZE_BYTES \
		+ QCX_COMMAND_MAX_RAW_ARGS_BYTES + QCX_COMMAND_MAX_ARGV_BYTES)

typedef struct qcx_client_command_payload_v1_s {
	uint8_t bytes[QCX_CLIENT_COMMAND_PAYLOAD_V1_MAX_SIZE];
	qcx_byte_count_t size;
} qcx_client_command_payload_v1_t;

/* Snapshot the engine's transient Cmd_* parser state into the fixed QCX ABI
 * record. The caller owns the result and may lend it only for its immediate
 * client-command entry call. */
int QCX_SnapshotClientCommand(qcx_client_command_payload_v1_t *out);

#endif
