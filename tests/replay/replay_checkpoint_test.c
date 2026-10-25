#include "qcx/replay_checkpoint.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t data[2048]; size_t size; } buffer_t;
static size_t Word(buffer_t *buffer, uint32_t value)
{
	size_t offset = buffer->size;
	assert(offset + 4 <= sizeof(buffer->data));
	for (unsigned i = 0; i < 4; ++i) buffer->data[buffer->size++] = value >> (8*i);
	return offset;
}
static size_t Text(buffer_t *buffer, const char *text)
{
	size_t size = strlen(text);
	Word(buffer, size);
	size_t offset = buffer->size;
	assert(offset + size <= sizeof(buffer->data));
	memcpy(buffer->data + offset, text, size);
	buffer->size += size;
	return offset;
}
static int Kind(unsigned scope, const char *name)
{
	if (!strcmp(name, "health")) return QCX_REPLAY_F32;
	if (!strcmp(name, "origin")) return QCX_REPLAY_VEC3;
	if (!strcmp(name, "think") || !strcmp(name, "field")) return QCX_REPLAY_SYMBOL;
	if (!strcmp(name, "name")) return QCX_REPLAY_STRING;
	if (scope == 2 && !strcmp(name, "enemy")) return QCX_REPLAY_U32;
	return 0;
}
static void Changed(const buffer_t *source, size_t offset, uint64_t original)
{
	buffer_t changed = *source;
	uint64_t hash;
	changed.data[offset] ^= 1;
	assert(QCX_ReplayLogicalHash(changed.data, changed.size, Kind, &hash));
	assert(hash != original);
}
static void Rejected(const buffer_t *source, size_t offset, uint8_t value)
{
	buffer_t changed = *source;
	uint64_t hash = 123;
	changed.data[offset] = value;
	assert(!QCX_ReplayLogicalHash(changed.data, changed.size, Kind, &hash));
	assert(hash == 123 && QCX_ReplayCheckpointError()[0]);
}
int main(void)
{
	/* QCGD v2: two descriptions, then globals and repeated entity values only. */
	buffer_t logical = {{'Q','C','G','D'}, 4};
	Word(&logical, 2);
	Word(&logical, 1); Text(&logical, "name"); Word(&logical, 5); /* String global. */
	size_t entity_schema = Word(&logical, 6);
	size_t health_name = Text(&logical, "health");
	size_t health_type = Word(&logical, 1);
	Text(&logical, "origin"); Word(&logical, 4);
	Text(&logical, "think"); Word(&logical, 7); /* void(float, entity) */
	size_t result_type = Word(&logical, 0);
	Word(&logical, 2); Word(&logical, 1); Word(&logical, 6);
	Text(&logical, "name"); Word(&logical, 5);
	Text(&logical, "enemy"); Word(&logical, 6);
	Text(&logical, "field"); Word(&logical, 8); Word(&logical, 1); /* Field<float> */
	size_t global_name = Text(&logical, "arena");
	size_t count = Word(&logical, 2);
	size_t health = 0, origin = 0, presence = 0, callback = 0, field = 0, last_slot = 0;
	for (unsigned i = 0; i < 2; ++i) {
		last_slot = Word(&logical, i ? 7 : 0);
		size_t value = Word(&logical, 0x42c80000U); /* health 100 */
		if (!i) health = value;
		value = Word(&logical, 0x3f800000U); Word(&logical, 0); Word(&logical, 0);
		if (!i) origin = value;
		value = Word(&logical, !i); /* Present first callback, absent second. */
		if (!i) { presence = value; callback = Text(&logical, "idle"); }
		Text(&logical, i ? "" : "Bob");
		Word(&logical, i ? 0 : 7); /* Entity slot value, not a native pointer. */
		Word(&logical, !i);
		if (!i) field = Text(&logical, "health");
	}
	uint64_t a, b;
	assert(QCX_ReplayLogicalHash(logical.data, logical.size, Kind, &a));
	buffer_t copy = logical;
	assert(QCX_ReplayLogicalHash(copy.data, copy.size, Kind, &b) && a == b);
	Changed(&logical, global_name, a);
	Changed(&logical, health, a);
	Changed(&logical, origin, a);
	Changed(&logical, callback, a);
	Changed(&logical, field, a);
	Changed(&logical, result_type, a); /* Signature metadata contributes too. */
	Rejected(&logical, 4, 1); /* Old format has no backward compatibility. */
	Rejected(&logical, entity_schema + 1, 255); /* Oversized description. */
	Rejected(&logical, health_name, 'x'); /* Unknown inventory member. */
	Rejected(&logical, health_type, 5); /* Serialized type disagrees with inventory. */
	Rejected(&logical, health_type, 255);
	Rejected(&logical, presence, 2);
	Rejected(&logical, callback, 0);
	Rejected(&logical, count + 1, 255);
	Rejected(&logical, last_slot + 1, 255);
	Rejected(&logical, last_slot, 0); /* Duplicate slot. */
	for (size_t size = 0; size < logical.size; ++size) {
		assert(!QCX_ReplayLogicalHash(logical.data, size, Kind, &b));
	}
	copy.data[copy.size++] = 0;
	assert(!QCX_ReplayLogicalHash(copy.data, copy.size, Kind, &b));
	assert(!QCX_ReplayLogicalHash(logical.data, logical.size, NULL, &b));
	assert(!QCX_ReplayLogicalHash(logical.data, logical.size, Kind, NULL));
	uint64_t counts[] = {100, 50};
	a = QCX_ReplayHashBytes(1, counts, sizeof(counts));
	++counts[0]; assert(a != QCX_ReplayHashBytes(1, counts, sizeof(counts)));
	--counts[0]; ++counts[1]; assert(a != QCX_ReplayHashBytes(1, counts, sizeof(counts)));
	puts("checkpoint: typed descriptions and logical values, strict v2 parsing");
}
