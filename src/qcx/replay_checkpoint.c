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
/* QCGD v2 wire tags from qc/schema.hpp, independent of native C++ types. */
enum { WIRE_VOID, WIRE_FLOAT, WIRE_INT32, WIRE_UINT32, WIRE_VECTOR,
	WIRE_STRING, WIRE_ENTITY, WIRE_FUNCTION, WIRE_FIELD, WIRE_ARRAY };
typedef struct {
	uint32_t count;
	uint8_t types[4096]; /* Root wire type of each retained QWSP member. */
} schema_t;

/* Skip a bounded type description, including function and field signatures. */
static int Type(reader_t *r, unsigned depth, uint32_t *tag)
{
	if (depth > 32 || !Word(r, tag)) {
		return 0;
	}
	if (*tag <= WIRE_ENTITY) {
		return 1;
	}
	uint32_t nested, count;
	if (*tag == WIRE_FUNCTION) {
		if (!Type(r, depth + 1, &nested) || !Word(r, &count) || count > 4096) {
			return 0;
		}
		for (uint32_t i = 0; i < count; ++i) {
			if (!Type(r, depth + 1, &nested) || nested == WIRE_VOID) {
				return 0;
			}
		}
		return 1;
	}
	if (*tag == WIRE_ARRAY && (!Word(r, &count) || !count)) {
		return 0;
	}
	return (*tag == WIRE_FIELD || *tag == WIRE_ARRAY)
		&& Type(r, depth + 1, &nested) && nested != WIRE_VOID;
}

/* Check names/types once against the retained inventory, before reading values. */
static int Description(reader_t *r, unsigned scope, schema_t *schema,
	qcx_replay_field_kind_t kind)
{
	if (!Word(r, &schema->count) || schema->count > 4096) {
		return 0;
	}
	for (uint32_t i = 0; i < schema->count; ++i) {
		const uint8_t *value;
		uint32_t length, tag;
		char name[256];
		if (!Text(r, &value, &length) || !length || length >= sizeof(name)
			|| !Type(r, 0, &tag)) {
			return 0;
		}
		memcpy(name, value, length);
		name[length] = 0;
		const int kinds[] = {0, QCX_REPLAY_F32, QCX_REPLAY_U32, QCX_REPLAY_U32,
			QCX_REPLAY_VEC3, QCX_REPLAY_STRING, QCX_REPLAY_U32,
			QCX_REPLAY_SYMBOL, QCX_REPLAY_SYMBOL, 0};
		/* Arrays remain outside this scalar QWSP inventory, as before. */
		if (!kinds[tag] || kind(scope, name) != kinds[tag]) {
			snprintf(error, sizeof(error), "unsupported logical field/type %u:%s", scope, name);
			return 0;
		}
		schema->types[i] = (uint8_t)tag;
	}
	return 1;
}

static int Values(reader_t *r, const schema_t *schema)
{
	for (uint32_t i = 0; i < schema->count; ++i) {
		uint32_t tag = schema->types[i], word;
		if (tag == WIRE_FLOAT || tag == WIRE_INT32 || tag == WIRE_UINT32
			|| tag == WIRE_VECTOR || tag == WIRE_ENTITY) {
			unsigned words = tag == WIRE_VECTOR ? 3 : 1;
			for (unsigned j = 0; j < words; ++j) {
				if (!Word(r, &word) || (tag == WIRE_ENTITY && word >= 2048)) {
					return 0;
				}
			}
		}
		else {
			if (tag == WIRE_FUNCTION || tag == WIRE_FIELD) {
				if (!Word(r, &word) || word > 1) {
					return 0;
				}
				if (!word) {
					continue;
				}
			}
			const uint8_t *value;
			if (!Text(r, &value, &word)
				|| (tag != WIRE_STRING && !word)) {
				return 0;
			}
		}
	}
	return 1;
}

int QCX_ReplayLogicalHash(const uint8_t *data, size_t size,
	qcx_replay_field_kind_t field_kind, uint64_t *hash)
{
	snprintf(error, sizeof(error), "malformed QCGD v2 checkpoint");
	if (!data || size < 8 || !field_kind || !hash
		|| memcmp(data, "QCGD\2\0\0\0", 8)) {
		return 0;
	}
	reader_t reader = {data + 8, data + size};
	schema_t globals, entity;
	uint32_t entities, previous = 0;
	if (!Description(&reader, 1, &globals, field_kind)
		|| !Description(&reader, 2, &entity, field_kind)
		|| !Values(&reader, &globals)
		|| !Word(&reader, &entities) || entities > 2048) {
		return 0;
	}
	for (uint32_t i = 0; i < entities; ++i) {
		uint32_t slot;
		if (!Word(&reader, &slot) || slot >= 2048 || (i && slot <= previous)
			|| !Values(&reader, &entity)) {
			return 0;
		}
		previous = slot;
	}
	if (reader.cursor != reader.end) {
		return 0;
	}
	/* The strict logical stream contains names/types and canonical values only;
	 * no addresses, struct padding or native callback payloads enter this hash. */
	*hash = QCX_ReplayHashBytes(UINT64_C(14695981039346656037), data, size);
	error[0] = 0;
	return 1;
}
