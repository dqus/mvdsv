#include "qcx/replay_checkpoint.h"
#include <stdio.h>
#include <string.h>

static char error[192];
const char *QCX_ReplayCheckpointError(void) { return error; }
uint64_t QCX_ReplayHashBytes(uint64_t hash, const void *data, size_t size)
{
	const uint8_t *bytes = data;
	for (size_t i = 0; i < size; ++i) {
		hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
	}
	return hash;
}

typedef struct {
	const uint8_t *cursor, *end;
	uint64_t hash;
	qcx_replay_field_kind_t kind;
} reader_t;

static int Word(reader_t *r, uint32_t *word)
{
	if ((size_t)(r->end - r->cursor) < 4) {
		return 0;
	}
	*word = r->cursor[0] | (uint32_t)r->cursor[1]<<8
		| (uint32_t)r->cursor[2]<<16 | (uint32_t)r->cursor[3]<<24;
	r->cursor += 4;
	return 1;
}
static void HashWord(reader_t *r, uint32_t word)
{
	uint8_t bytes[4] = {word, word>>8, word>>16, word>>24};
	r->hash = QCX_ReplayHashBytes(r->hash, bytes, sizeof(bytes));
}
static int Text(reader_t *r, const uint8_t **value, uint32_t *size)
{
	if (!Word(r, size) || (size_t)(r->end - r->cursor) < *size
		|| memchr(r->cursor, 0, *size)) {
		return 0;
	}
	*value = r->cursor;
	r->cursor += *size;
	return 1;
}
static int Schema(reader_t *r, unsigned scope)
{
	uint32_t count;
	if (!Word(r, &count) || count > 4096) {
		return 0;
	}
	for (uint32_t i = 0; i < count; ++i) {
		const uint8_t *value;
		uint32_t length;
		char name[256];
		if (!Text(r, &value, &length) || !length || length >= sizeof(name)) {
			return 0;
		}
		memcpy(name, value, length);
		name[length] = 0;
		int kind = r->kind(scope, name);
		HashWord(r, scope);
		HashWord(r, length);
		r->hash = QCX_ReplayHashBytes(r->hash, name, length);
		HashWord(r, (uint32_t)kind);
		if (kind == QCX_REPLAY_F32 || kind == QCX_REPLAY_U32 || kind == QCX_REPLAY_VEC3) {
			unsigned words = kind == QCX_REPLAY_VEC3 ? 3 : 1;
			for (unsigned j = 0; j < words; ++j) {
				uint32_t word;
				if (!Word(r, &word)) {
					return 0;
				}
				HashWord(r, word);
			}
		}
		else if (kind == QCX_REPLAY_STRING || kind == QCX_REPLAY_SYMBOL) {
			uint32_t present = 1;
			if (kind == QCX_REPLAY_SYMBOL) {
				if (!Word(r, &present) || present > 1) {
					return 0;
				}
				HashWord(r, present);
			}
			if (present) {
				if (!Text(r, &value, &length)) {
					return 0;
				}
				HashWord(r, length);
				r->hash = QCX_ReplayHashBytes(r->hash, value, length);
			}
		}
		else {
			snprintf(error, sizeof(error), "unknown logical field %u:%s", scope, name);
			return 0;
		}
	}
	return 1;
}

int QCX_ReplayLogicalHash(const uint8_t *data, size_t size,
	qcx_replay_field_kind_t field_kind, uint64_t *hash)
{
	if (!data || size < 12 || !field_kind || memcmp(data, "QCGD\1\0\0\0", 8)) {
		return 0;
	}
	error[0] = 0;
	reader_t reader = {data + 8, data + size, UINT64_C(14695981039346656037), field_kind};
	uint32_t entities;
	if (!Schema(&reader, 1) || !Word(&reader, &entities) || entities > 2048) {
		return 0;
	}
	for (uint32_t i = 0; i < entities; ++i) {
		uint32_t slot;
		if (!Word(&reader, &slot) || slot >= 2048) {
			return 0;
		}
		HashWord(&reader, slot);
		if (!Schema(&reader, 2)) {
			return 0;
		}
	}
	if (reader.cursor != reader.end) {
		return 0;
	}
	*hash = reader.hash;
	return 1;
}
