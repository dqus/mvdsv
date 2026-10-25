#ifndef MVDSV_QCX_REPLAY_CHECKPOINT_H
#define MVDSV_QCX_REPLAY_CHECKPOINT_H
#include <stddef.h>
#include <stdint.h>

enum qcx_replay_value_kind { QCX_REPLAY_F32=1, QCX_REPLAY_U32, QCX_REPLAY_VEC3,
	QCX_REPLAY_STRING, QCX_REPLAY_SYMBOL };
typedef int (*qcx_replay_field_kind_t)(unsigned scope, const char *name);
/* Decode strict QCGD v2 descriptions/values against the retained QWSP inventory.
 * No native layouts, handles, callback payloads or QCMS host bytes enter the hash. */
int QCX_ReplayLogicalHash(const uint8_t *data, size_t size,
	qcx_replay_field_kind_t field_kind, uint64_t *hash);
uint64_t QCX_ReplayHashBytes(uint64_t hash, const void *data, size_t size);
const char *QCX_ReplayCheckpointError(void);
#endif
