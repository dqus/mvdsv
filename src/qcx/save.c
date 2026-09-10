#include "qcx/save.h"

#include "qcx/adapter.h"
#include "qcx/entities.h"
#if defined(QCX_TESTS)
#include "qcx/test_observer.h"
#endif

#include <limits.h>
#include <math.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	QCX_SAVE_ENGINE_VERSION = 2U,
	QCX_SAVE_MAX_GUEST_BYTES = 8U * 1024U * 1024U,
	QCX_SAVE_MAX_RESOURCE_BYTES = 255U,
	QCX_SAVE_ACTIVE_EDICT = 1U,
	QCX_SAVE_FREE_EDICT = 2U
};

typedef struct qcx_save_writer_s {
	uint8_t *cursor;
	const uint8_t *end;
} qcx_save_writer_t;

typedef struct qcx_connected_snapshot_s {
	uint64_t hash;
	uint32_t size;
	qbool valid;
} qcx_connected_snapshot_t;

static qcx_connected_snapshot_t qcx_connected_snapshot;
static qcx_save_image_t *qcx_prepared_image;

static uint64_t QCX_SaveFingerprint(const uint8_t *bytes, uint32_t size)
{
	uint64_t hash = UINT64_C(14695981039346656037);
	uint32_t index;
	for (index = 0U; index < size; ++index) {
		hash ^= bytes[index];
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

void QCX_SaveInvalidateConnectedSnapshot(void)
{
	qcx_connected_snapshot.valid = false;
}

typedef struct qcx_save_reader_s {
	const uint8_t *cursor;
	const uint8_t *end;
} qcx_save_reader_t;

typedef struct qcx_restore_plan_s {
	const qcx_save_image_t *image;
	double time;
	uint32_t serverflags;
	uint32_t num_edicts;
	uint8_t selection[(QCX_SAVE_MAX_ENTITY_CAPACITY + 7U) / 8U];
	uint32_t edict_flags[QCX_SAVE_MAX_ENTITY_CAPACITY];
	float freetimes[QCX_SAVE_MAX_ENTITY_CAPACITY];
	char lightstyles[MAX_LIGHTSTYLES][QCX_SAVE_MAX_RESOURCE_BYTES + 1U];
} qcx_restore_plan_t;

static qcx_restore_plan_t qcx_restore_plan;
static char qcx_restored_lightstyles[MAX_LIGHTSTYLES][QCX_SAVE_MAX_RESOURCE_BYTES + 1U];

_Static_assert(NUM_SPAWN_PARMS == QCX_SAVE_SPAWN_PARM_COUNT,
	"QCMS spawn parameter count");

static qbool QCX_SaveRead(qcx_save_reader_t *reader, void *out, uint32_t size)
{
	if ((size_t)(reader->end - reader->cursor) < size) return false;
	if (size != 0U) { memcpy(out, reader->cursor, size); reader->cursor += size; }
	return true;
}

static qbool QCX_SaveReadU32(qcx_save_reader_t *reader, uint32_t *out)
{
	uint8_t bytes[4];
	if (!QCX_SaveRead(reader, bytes, sizeof(bytes))) return false;
	*out = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16)
		| ((uint32_t)bytes[3] << 24);
	return true;
}

static qbool QCX_SaveReadF32(qcx_save_reader_t *reader, float *out)
{
	uint32_t bits;
	if (!QCX_SaveReadU32(reader, &bits)) return false;
	memcpy(out, &bits, sizeof(bits));
	return isfinite(*out);
}

static qbool QCX_SaveReadF64(qcx_save_reader_t *reader, double *out)
{
	uint8_t bytes[8];
	uint64_t bits = 0U;
	uint32_t index;
	if (!QCX_SaveRead(reader, bytes, sizeof(bytes))) return false;
	for (index = 0U; index < sizeof(bytes); ++index) bits |= (uint64_t)bytes[index] << (8U * index);
	memcpy(out, &bits, sizeof(bits));
	return isfinite(*out);
}

static qbool QCX_SaveReadResource(qcx_save_reader_t *reader, char *out, uint32_t capacity)
{
	uint32_t size;
	uint32_t index;
	if (!QCX_SaveReadU32(reader, &size) || size == 0U || size >= capacity
		|| (size_t)(reader->end - reader->cursor) < size) return false;
	for (index = 0U; index < size; ++index) {
		const uint8_t byte = reader->cursor[index];
		if (byte < 32U || byte > 126U || byte == '\\') return false;
	}
	memcpy(out, reader->cursor, size);
	out[size] = '\0';
	reader->cursor += size;
	return true;
}

static qbool QCX_SaveWrite(qcx_save_writer_t *writer, const void *bytes, uint32_t size)
{
	if ((size_t)(writer->end - writer->cursor) < size) return false;
	if (size != 0U) { memcpy(writer->cursor, bytes, size); writer->cursor += size; }
	return true;
}

static qbool QCX_SaveWriteU32(qcx_save_writer_t *writer, uint32_t value)
{
	const uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8),
		(uint8_t)(value >> 16), (uint8_t)(value >> 24)};
	return QCX_SaveWrite(writer, bytes, sizeof(bytes));
}

static qbool QCX_SaveWriteF32(qcx_save_writer_t *writer, float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return QCX_SaveWriteU32(writer, bits);
}

static qbool QCX_SaveWriteF64(qcx_save_writer_t *writer, double value)
{
	uint64_t bits;
	uint8_t bytes[8];
	uint32_t index;
	memcpy(&bits, &value, sizeof(bits));
	for (index = 0U; index < sizeof(bytes); ++index) bytes[index] = (uint8_t)(bits >> (8U * index));
	return QCX_SaveWrite(writer, bytes, sizeof(bytes));
}

static uint32_t QCX_SaveBoundedStringLength(const char *value, uint32_t limit)
{
	uint32_t length;
	if (value == NULL) return 0U;
	for (length = 0U; length <= limit; ++length) if (value[length] == '\0') return length;
	return limit + 1U;
}

static qbool QCX_SaveWriteResource(qcx_save_writer_t *writer, const char *prefix,
	const char *value)
{
	const uint32_t prefix_size = QCX_SaveBoundedStringLength(prefix, 16U);
	const uint32_t value_size = QCX_SaveBoundedStringLength(value, QCX_SAVE_MAX_RESOURCE_BYTES - prefix_size);
	const uint32_t size = prefix_size + value_size;
	uint32_t index;
	if (prefix_size == 0U || prefix_size > 16U || value_size == 0U
		|| value_size > QCX_SAVE_MAX_RESOURCE_BYTES - prefix_size) return false;
	for (index = 0U; index < value_size; ++index) {
		const uint8_t byte = (uint8_t)value[index];
		if (byte < 32U || byte > 126U || byte == '\\') return false;
	}
	return QCX_SaveWriteU32(writer, size) && QCX_SaveWrite(writer, prefix, prefix_size)
		&& QCX_SaveWrite(writer, value, value_size);
}

static uint32_t QCX_SaveResourcePrefixSize(uint32_t index)
{
	uint32_t digits = 1U;
	while (index >= 10U) {
		index /= 10U;
		++digits;
	}
	return digits + 3U;
}

static qbool QCX_SaveWriteIndexedResource(qcx_save_writer_t *writer, char kind,
	uint32_t index, const char *value)
{
	char prefix[16];
	const int written = snprintf(prefix, sizeof(prefix), "%c:%u:", kind, index);
	return written > 0 && (size_t)written < sizeof(prefix)
		&& QCX_SaveWriteResource(writer, prefix, value);
}

static qbool QCX_SaveWriteLightstyle(qcx_save_writer_t *writer, const char *value)
{
	const char *const saved = value == NULL ? "m" : value;
	const uint32_t size = QCX_SaveBoundedStringLength(saved, QCX_SAVE_MAX_RESOURCE_BYTES);
	uint32_t index;
	if (size == 0U || size > QCX_SAVE_MAX_RESOURCE_BYTES) return false;
	for (index = 0U; index < size; ++index) {
		const uint8_t byte = (uint8_t)saved[index];
		if (byte < 32U || byte > 126U || byte == '\\') return false;
	}
	return QCX_SaveWriteU32(writer, size) && QCX_SaveWrite(writer, saved, size);
}

static qbool QCX_SaveAppendSize(uint32_t *size, uint32_t add)
{
	if (*size > UINT32_MAX - add) return false;
	*size += add;
	return true;
}

static qbool QCX_SaveEngineSize(uint32_t entity_capacity, uint32_t *out_size,
	uint32_t *out_precache_count)
{
	uint32_t size = 4U + 8U + 4U + 4U;
	uint32_t precache_count = 0U;
	int index;
	for (index = 0; index < MAX_LIGHTSTYLES; ++index) {
		const uint32_t length = QCX_SaveBoundedStringLength(sv.lightstyles[index] == NULL ? "m" : sv.lightstyles[index], QCX_SAVE_MAX_RESOURCE_BYTES);
		if (length == 0U || length > QCX_SAVE_MAX_RESOURCE_BYTES || !QCX_SaveAppendSize(&size, 4U + length)) return false;
	}
	if (!QCX_SaveAppendSize(&size, 4U)) return false;
	for (index = 1; index < MAX_MODELS; ++index) if (sv.model_precache[index] != NULL && sv.model_precache[index][0] != '\0') {
		const uint32_t prefix_size = QCX_SaveResourcePrefixSize((uint32_t)index);
		const uint32_t length = QCX_SaveBoundedStringLength(sv.model_precache[index], QCX_SAVE_MAX_RESOURCE_BYTES - prefix_size);
		if (length == 0U || length > QCX_SAVE_MAX_RESOURCE_BYTES - prefix_size || !QCX_SaveAppendSize(&size, 4U + prefix_size + length)) return false;
		++precache_count;
	}
	for (index = 1; index < MAX_SOUNDS; ++index) if (sv.sound_precache[index] != NULL && sv.sound_precache[index][0] != '\0') {
		const uint32_t prefix_size = QCX_SaveResourcePrefixSize((uint32_t)index);
		const uint32_t length = QCX_SaveBoundedStringLength(sv.sound_precache[index], QCX_SAVE_MAX_RESOURCE_BYTES - prefix_size);
		if (length == 0U || length > QCX_SAVE_MAX_RESOURCE_BYTES - prefix_size || !QCX_SaveAppendSize(&size, 4U + prefix_size + length)) return false;
		++precache_count;
	}
	if (precache_count > MAX_MODELS + MAX_SOUNDS || !QCX_SaveAppendSize(&size, 4U)
		|| !QCX_SaveAppendSize(&size, entity_capacity * 8U)) return false;
	*out_size = size; *out_precache_count = precache_count;
	return true;
}

static qbool QCX_SaveBuildEngine(uint32_t entity_capacity, qcx_save_bytes_t *out)
{
	uint32_t size, precache_count;
	qcx_save_writer_t writer;
	int index;
	if (sv.max_edicts <= 0 || entity_capacity == 0U || entity_capacity > QCX_SAVE_MAX_ENTITY_CAPACITY
		|| (uint32_t)sv.max_edicts > entity_capacity
		|| !QCX_SaveEngineSize(entity_capacity, &size, &precache_count)) return false;
	out->data = malloc(size); out->size = 0U;
	if (out->data == NULL) return false;
	writer = (qcx_save_writer_t){out->data, out->data + size};
	if (!QCX_SaveWriteU32(&writer, QCX_SAVE_ENGINE_VERSION) || !QCX_SaveWriteF64(&writer, sv.time)
		|| !QCX_SaveWriteU32(&writer, svs.serverflags) || !QCX_SaveWriteU32(&writer, MAX_LIGHTSTYLES)) goto fail;
	for (index = 0; index < MAX_LIGHTSTYLES; ++index) if (!QCX_SaveWriteLightstyle(&writer, sv.lightstyles[index])) goto fail;
	if (!QCX_SaveWriteU32(&writer, precache_count)) goto fail;
	for (index = 1; index < MAX_MODELS; ++index) if (sv.model_precache[index] != NULL && sv.model_precache[index][0] != '\0'
		&& !QCX_SaveWriteIndexedResource(&writer, 'M', (uint32_t)index,
			sv.model_precache[index])) goto fail;
	for (index = 1; index < MAX_SOUNDS; ++index) if (sv.sound_precache[index] != NULL && sv.sound_precache[index][0] != '\0'
		&& !QCX_SaveWriteIndexedResource(&writer, 'S', (uint32_t)index,
			sv.sound_precache[index])) goto fail;
	if (!QCX_SaveWriteU32(&writer, entity_capacity)) goto fail;
	for (index = 0; index < (int)entity_capacity; ++index) {
		const uint32_t flags = index >= sv.num_edicts ? 0U : sv.edicts[index].e.free ? QCX_SAVE_FREE_EDICT : QCX_SAVE_ACTIVE_EDICT;
		if (!QCX_SaveWriteU32(&writer, flags) || !QCX_SaveWriteF32(&writer, sv.edicts[index].e.freetime)) goto fail;
	}
	if (writer.cursor != writer.end) goto fail;
	out->size = size; return true;
fail:
	free(out->data); out->data = NULL; return false;
}

static qbool QCX_SaveBuildRoster(qcx_save_image_t *image)
{
	uint32_t slot;
	image->roster_count = 0U;
	for (slot = 0U; slot < MAX_CLIENTS; ++slot) {
		const client_t *const client = &svs.clients[slot];
		qcx_save_roster_entry_t *entry;
		uint32_t name_size;
		uint32_t team_size;
		if (client->state != cs_connected && client->state != cs_spawned) continue;
		if (image->roster_count == QCX_SAVE_MAX_CLIENTS) return false;
		name_size = QCX_SaveBoundedStringLength(client->name,
			QCX_SAVE_CLIENT_STRING_CAPACITY - 1U);
		team_size = QCX_SaveBoundedStringLength(client->team,
			QCX_SAVE_CLIENT_STRING_CAPACITY - 1U);
		if (name_size == 0U || name_size >= QCX_SAVE_CLIENT_STRING_CAPACITY
			|| team_size >= QCX_SAVE_CLIENT_STRING_CAPACITY) {
			return false;
		}
		entry = &image->roster[image->roster_count++];
		entry->saved_slot = slot;
		entry->spawned = client->state == cs_spawned;
		entry->role = client->spectator != 0 ? QCX_SAVE_ROLE_SPECTATOR
			: QCX_SAVE_ROLE_PLAYER;
		memcpy(entry->name, client->name, name_size + 1U);
		memcpy(entry->team, client->team, team_size + 1U);
		memcpy(entry->spawn_parms, client->spawn_parms, sizeof(entry->spawn_parms));
	}
	return true;
}

static void QCX_SaveSelection(uint8_t *bitmap, uint32_t size)
{
	int slot;
	memset(bitmap, 0, size);
	for (slot = 0; slot < sv.num_edicts; ++slot) if (!sv.edicts[slot].e.free) bitmap[(uint32_t)slot / 8U] |= (uint8_t)(1U << ((uint32_t)slot % 8U));
}

static qbool QCX_SaveNameIsSafe(const char *name)
{
	uint32_t index;
	const uint32_t size = QCX_SaveBoundedStringLength(name, QCX_SAVE_NAME_CAPACITY - 1U);
	if (size == 0U || size >= QCX_SAVE_NAME_CAPACITY) return false;
	for (index = 0U; index < size; ++index) {
		const unsigned char byte = (unsigned char)name[index];
		if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '_' || byte == '-')) return false;
	}
	return true;
}

static qbool QCX_SaveWriteFile(const char *name, const uint8_t *bytes, uint32_t size)
{
	char path[MAX_OSPATH], temporary[MAX_OSPATH]; FILE *file;
	qbool written;
	qbool flushed;
	int error = 0;
	const int path_size = snprintf(path, sizeof(path), "%s/save/%s.sav", fs_gamedir, name);
	if (path_size < 0 || (size_t)path_size >= sizeof(path) || snprintf(temporary, sizeof(temporary), "%s.tmp", path) < 0 || strlen(temporary) >= sizeof(temporary)) return false;
	FS_CreatePath(path);
	file = fopen(temporary, "wb");
	if (file == NULL) {
		Con_Printf("qc2cpp save write failed for %s: %s.\n", temporary, strerror(errno));
		return false;
	}
	written = fwrite(bytes, 1U, size, file) == size;
	if (!written) error = errno;
	flushed = written && fflush(file) == 0;
	if (!flushed && error == 0) error = errno;
	if (fclose(file) != 0 && error == 0) error = errno;
	if (!written || !flushed || error != 0) {
		Con_Printf("qc2cpp save write failed for %s: %s.\n", temporary, strerror(error));
		remove(temporary);
		return false;
	}
	if (rename(temporary, path) != 0) {
		Con_Printf("qc2cpp save commit failed for %s: %s.\n", path, strerror(errno));
		remove(temporary);
		return false;
	}
	FS_FlushFSHash(); return true;
}

static qbool QCX_SaveParseIndexedResource(const char *resource, char *kind,
	uint32_t *index, const char **name)
{
	const char *cursor;
	uint32_t value = 0U;
	if ((resource[0] != 'M' && resource[0] != 'S') || resource[1] != ':') return false;
	cursor = resource + 2;
	if (*cursor < '0' || *cursor > '9') return false;
	while (*cursor >= '0' && *cursor <= '9') {
		if (value > (UINT32_MAX - (uint32_t)(*cursor - '0')) / 10U) return false;
		value = value * 10U + (uint32_t)(*cursor - '0');
		++cursor;
	}
	if (*cursor != ':' || cursor[1] == '\0') return false;
	*kind = resource[0];
	*index = value;
	*name = cursor + 1;
	return true;
}

static qcx_restore_status_t QCX_SaveDecodeEngine(const qcx_save_image_t *image)
{
	qcx_save_reader_t reader;
	uint8_t models[MAX_MODELS] = {0};
	uint8_t sounds[MAX_SOUNDS] = {0};
	uint32_t version;
	uint32_t count;
	uint32_t index;
	qbool unused_slot = false;
	char resource[QCX_SAVE_MAX_RESOURCE_BYTES + 1U];

	if (image == NULL || image->engine_state.data == NULL || image->engine_state.size == 0U
		|| image->metadata.entity_capacity != QCX_EntityCapacity()
		|| image->metadata.client_slot_capacity == 0U
		|| image->metadata.client_slot_capacity > MAX_CLIENTS
		|| image->metadata.client_slot_capacity >= image->metadata.entity_capacity
		|| strcmp(image->metadata.logical_game, sv_progsname.string) != 0
		|| strcmp(image->metadata.map_name, sv.mapname) != 0
		|| image->metadata.map_bsp_checksum != sv.map_checksum) {
		Con_Printf("qc2cpp restore engine identity mismatch: game=%s map=%s capacity=%u checksum=%u.\n",
			sv_progsname.string, sv.mapname, QCX_EntityCapacity(), sv.map_checksum);
		return QCX_RESTORE_ENTITY_SET_MISMATCH;
	}
	reader = (qcx_save_reader_t){image->engine_state.data,
		image->engine_state.data + image->engine_state.size};
	if (!QCX_SaveReadU32(&reader, &version) || version != QCX_SAVE_ENGINE_VERSION
		|| !QCX_SaveReadF64(&reader, &qcx_restore_plan.time)
		|| !QCX_SaveReadU32(&reader, &qcx_restore_plan.serverflags)
		|| !QCX_SaveReadU32(&reader, &count) || count != MAX_LIGHTSTYLES) return QCX_RESTORE_MALFORMED_CHUNK;
	for (index = 0U; index < MAX_LIGHTSTYLES; ++index) {
		if (!QCX_SaveReadResource(&reader, qcx_restore_plan.lightstyles[index],
			sizeof(qcx_restore_plan.lightstyles[index]))) return QCX_RESTORE_MALFORMED_CHUNK;
	}
	if (!QCX_SaveReadU32(&reader, &count) || count > MAX_MODELS + MAX_SOUNDS) return QCX_RESTORE_MALFORMED_CHUNK;
	for (index = 0U; index < count; ++index) {
		char kind;
		uint32_t slot;
		const char *name;
		if (!QCX_SaveReadResource(&reader, resource, sizeof(resource))
			|| !QCX_SaveParseIndexedResource(resource, &kind, &slot, &name)) return QCX_RESTORE_MALFORMED_CHUNK;
		if (kind == 'M') {
			if (slot == 0U || slot >= MAX_MODELS || models[slot] != 0U
				|| sv.model_precache[slot] == NULL || strcmp(name, sv.model_precache[slot]) != 0) {
				Con_Printf("qc2cpp restore model mismatch at %u: expected %s, got %s.\n", slot,
					name, slot < MAX_MODELS && sv.model_precache[slot] != NULL ? sv.model_precache[slot] : "<none>");
				return QCX_RESTORE_ENTITY_SET_MISMATCH;
			}
			models[slot] = 1U;
		} else {
			if (slot == 0U || slot >= MAX_SOUNDS || sounds[slot] != 0U
				|| sv.sound_precache[slot] == NULL || strcmp(name, sv.sound_precache[slot]) != 0) {
				Con_Printf("qc2cpp restore sound mismatch at %u: expected %s, got %s.\n", slot,
					name, slot < MAX_SOUNDS && sv.sound_precache[slot] != NULL ? sv.sound_precache[slot] : "<none>");
				return QCX_RESTORE_ENTITY_SET_MISMATCH;
			}
			sounds[slot] = 1U;
		}
	}
	if (qcx_prepared_image == NULL) {
		for (index = 1U; index < MAX_MODELS; ++index) {
			if ((sv.model_precache[index] != NULL && sv.model_precache[index][0] != '\0')
				!= (models[index] != 0U)) return QCX_RESTORE_ENTITY_SET_MISMATCH;
		}
		for (index = 1U; index < MAX_SOUNDS; ++index) {
			if ((sv.sound_precache[index] != NULL && sv.sound_precache[index][0] != '\0')
				!= (sounds[index] != 0U)) return QCX_RESTORE_ENTITY_SET_MISMATCH;
		}
	}
	if (!QCX_SaveReadU32(&reader, &count) || count != image->metadata.entity_capacity) return QCX_RESTORE_MALFORMED_CHUNK;
	memset(qcx_restore_plan.selection, 0, sizeof(qcx_restore_plan.selection));
	qcx_restore_plan.num_edicts = 0U;
	for (index = 0U; index < count; ++index) {
		if (!QCX_SaveReadU32(&reader, &qcx_restore_plan.edict_flags[index])
			|| qcx_restore_plan.edict_flags[index] > QCX_SAVE_FREE_EDICT
			|| !QCX_SaveReadF32(&reader, &qcx_restore_plan.freetimes[index])) return QCX_RESTORE_MALFORMED_CHUNK;
		if (index == 0U && qcx_restore_plan.edict_flags[index] != QCX_SAVE_ACTIVE_EDICT) {
			return QCX_RESTORE_ENTITY_SET_MISMATCH;
		}
		if (unused_slot && qcx_restore_plan.edict_flags[index] != 0U) return QCX_RESTORE_MALFORMED_CHUNK;
		if (qcx_restore_plan.edict_flags[index] == 0U) {
			unused_slot = true;
		} else {
			qcx_restore_plan.num_edicts = index + 1U;
			if (qcx_restore_plan.edict_flags[index] == QCX_SAVE_ACTIVE_EDICT) qcx_restore_plan.selection[index / 8U] |= (uint8_t)(1U << (index % 8U));
		}
	}
	if (reader.cursor != reader.end || image->roster_count > QCX_SAVE_MAX_CLIENTS
		|| image->roster_count > image->metadata.client_slot_capacity) {
		return QCX_RESTORE_MALFORMED_CHUNK;
	}
	for (index = 0U; index < image->roster_count; ++index) {
		const qcx_save_roster_entry_t *const entry = &image->roster[index];
		uint32_t prior;
		uint32_t parm;
		if (entry->saved_slot >= image->metadata.client_slot_capacity
			|| entry->saved_slot >= MAX_CLIENTS
			|| entry->saved_slot + 1U >= image->metadata.entity_capacity
			|| qcx_restore_plan.edict_flags[entry->saved_slot + 1U]
				!= QCX_SAVE_ACTIVE_EDICT
			|| entry->spawned > 1U
			|| (entry->role != QCX_SAVE_ROLE_PLAYER
				&& entry->role != QCX_SAVE_ROLE_SPECTATOR)
			|| entry->name[0] == '\0') {
			return QCX_RESTORE_MALFORMED_CHUNK;
		}
		for (parm = 0U; parm < QCX_SAVE_SPAWN_PARM_COUNT; ++parm) {
			if (!isfinite(entry->spawn_parms[parm])) return QCX_RESTORE_MALFORMED_CHUNK;
		}
		for (prior = 0U; prior < index; ++prior) {
			if (entry->saved_slot == image->roster[prior].saved_slot
				|| QCX_SaveClientNameEqual(entry->name, image->roster[prior].name)) {
				return QCX_RESTORE_MALFORMED_CHUNK;
			}
		}
	}
	for (index = 0U; index < MAX_CLIENTS; ++index) {
		const client_t *const client = &svs.clients[index];
		const qcx_save_roster_entry_t *saved = NULL;
		uint32_t roster_index;
		for (roster_index = 0U; roster_index < image->roster_count; ++roster_index) {
			if (image->roster[roster_index].saved_slot == index) {
				saved = &image->roster[roster_index];
				break;
			}
		}
		if ((client->state == cs_connected || client->state == cs_spawned)
			!= (saved != NULL)) {
			return QCX_RESTORE_ENTITY_SET_MISMATCH;
		}
		if (saved != NULL
			&& ((client->state == cs_spawned) != (saved->spawned != 0U)
				|| (client->spectator != 0)
					!= (saved->role == QCX_SAVE_ROLE_SPECTATOR))) {
			return QCX_RESTORE_ENTITY_SET_MISMATCH;
		}
	}
	return QCX_RESTORE_OK;
}

qcx_restore_status_t QCX_ValidateSaveGame(const qcx_save_image_t *image)
{
	qcx_restore_status_t status;
	qcx_restore_plan.image = NULL;
	status = QCX_SaveDecodeEngine(image);
	if (status != QCX_RESTORE_OK) return status;
	status = QCX_ValidateGuestRestore(image->guest_payload.data, image->guest_payload.size,
		qcx_restore_plan.selection, (image->metadata.entity_capacity + 7U) / 8U);
	if (status != QCX_RESTORE_OK) return status;
	qcx_restore_plan.image = image;
	return QCX_RESTORE_OK;
}

static void QCX_RefreshClientReplication(void)
{
	uint32_t source_index;
	for (source_index = 0U; source_index < MAX_CLIENTS; ++source_index) {
		client_t *const source = &svs.clients[source_index];
		uint32_t recipient_index;
		if (source->state != cs_connected && source->state != cs_spawned) continue;
		source->delta_sequence = -1;
		for (recipient_index = 0U; recipient_index < MAX_CLIENTS; ++recipient_index) {
			client_t *const recipient = &svs.clients[recipient_index];
			if (recipient->state < cs_preconnected) continue;
			SV_FullClientUpdateToClient(source, recipient);
			/* A load can happen between client packets; make the rebuilt reliable
			 * stream eligible for the next server send rather than waiting for a
			 * later movement command. */
			recipient->send_message = true;
		}
	}
}

void QCX_ApplySaveGame(const qcx_save_image_t *image)
{
	uint32_t index;
	if (image == NULL || qcx_restore_plan.image != image
		|| QCX_SetSaveSelection(qcx_restore_plan.selection,
			(image->metadata.entity_capacity + 7U) / 8U) != QCX_RESTORE_OK
		|| QCX_RestoreGuest(image->guest_payload.data, image->guest_payload.size) != QCX_RESTORE_OK) {
		SV_Error("qc2cpp restore failed after commit");
	}
	for (index = 0U; index < MAX_LIGHTSTYLES; ++index) {
		memcpy(qcx_restored_lightstyles[index], qcx_restore_plan.lightstyles[index],
			sizeof(qcx_restored_lightstyles[index]));
		sv.lightstyles[index] = qcx_restored_lightstyles[index];
	}
	for (index = 0U; index < image->metadata.entity_capacity; ++index) {
		sv.edicts[index].e.free = qcx_restore_plan.edict_flags[index] != QCX_SAVE_ACTIVE_EDICT;
		sv.edicts[index].e.freetime = qcx_restore_plan.freetimes[index];
	}
	/* Player edicts are reserved engine slots, not ordinary game objects.  A
	 * roster-free player slot therefore must not become visible merely because
	 * the guest's fixed entity storage contains a value at that index. */
	for (index = 0U; index < image->metadata.client_slot_capacity; ++index) {
		qbool restored_client = false;
		uint32_t client_index;
		for (client_index = 0U; client_index < image->roster_count; ++client_index) {
			if (image->roster[client_index].saved_slot == index) {
				restored_client = true;
				break;
			}
		}
		if (!restored_client) sv.edicts[index + 1U].e.free = true;
	}
	sv.num_edicts = (int)qcx_restore_plan.num_edicts;
	sv.time = qcx_restore_plan.time;
	sv.old_time = sv.time;
	svs.serverflags = qcx_restore_plan.serverflags;
	PR_GLOBAL(serverflags) = svs.serverflags;
	SV_ClearWorld();
	for (index = 0U; index < qcx_restore_plan.num_edicts; ++index) {
		if (!sv.edicts[index].e.free) SV_LinkEdict(&sv.edicts[index], false);
	}
	for (index = 0U; index < image->roster_count; ++index) {
		const qcx_save_roster_entry_t *const entry = &image->roster[index];
		client_t *const client = &svs.clients[entry->saved_slot];
		if (client->state == cs_connected || client->state == cs_spawned) {
			memcpy(client->spawn_parms, entry->spawn_parms, sizeof(entry->spawn_parms));
		}
	}
#if defined(QCX_TESTS)
	QCX_TestObserverRestoreReplicationBegin();
#endif
	QCX_RefreshClientReplication();
#if defined(QCX_TESTS)
	QCX_TestObserverRestoreReplicationComplete();
#endif
	qcx_restore_plan.image = NULL;
}

qbool QCX_SaveGame(const char *name)
{
	uint8_t selection[(QCX_SAVE_MAX_ENTITY_CAPACITY + 7U) / 8U];
	const uint32_t entity_capacity = QCX_EntityCapacity();
	const uint32_t selection_size = (entity_capacity + 7U) / 8U;
	qcx_save_bytes_t engine = {0}, guest = {0}; qcx_save_image_t image = {0};
	uint8_t *encoded = NULL; uint32_t encoded_size = 0U; qbool result = false;
	const char *failure = "unknown error";
	qcx_byte_count_t required;
	if (!QCX_Active() || sv.state != ss_active || sv.max_edicts <= 0 || entity_capacity == 0U
		|| entity_capacity > QCX_SAVE_MAX_ENTITY_CAPACITY || (uint32_t)sv.max_edicts > entity_capacity
		|| !QCX_SaveNameIsSafe(name)) {
		Con_Printf("qc2cpp save rejected: inactive or incompatible server state.\n");
		return false;
	}
	QCX_SaveSelection(selection, selection_size);
	if (QCX_SetSaveSelection(selection, selection_size) != QCX_RESTORE_OK) {
		Con_Printf("qc2cpp save rejected: game rejected entity selection.\n");
		return false;
	}
	required = QCX_SaveGuest(NULL, 0U);
	if (required == 0U || required > QCX_SAVE_MAX_GUEST_BYTES) {
		Con_Printf("qc2cpp save rejected: invalid game snapshot size.\n");
		return false;
	}
	guest.size = required;
	if (guest.size != 0U) {
		guest.data = malloc(guest.size);
		if (guest.data == NULL || QCX_SaveGuest(guest.data, guest.size) != guest.size) {
			failure = "could not serialize game state";
			goto done;
		}
	}
	if (!QCX_SaveBuildEngine(entity_capacity, &engine)) {
		failure = "could not serialize server state";
		goto done;
	}
	if (!QCX_SaveBuildRoster(&image)) {
		failure = "could not serialize restored player roster";
		goto done;
	}
	strlcpy(image.metadata.logical_game, sv_progsname.string, sizeof(image.metadata.logical_game));
	strlcpy(image.metadata.map_name, sv.mapname, sizeof(image.metadata.map_name));
	image.metadata.map_bsp_checksum = sv.map_checksum;
	image.metadata.entity_capacity = entity_capacity;
	image.metadata.client_slot_capacity = MAX_CLIENTS;
	image.engine_state = engine; image.guest_payload = guest;
	if (QCX_SaveEncode(&image, &encoded, &encoded_size) != QCX_PLUGIN_OK) {
		failure = "could not encode QCMS image";
		goto done;
	}
	result = QCX_SaveWriteFile(name, encoded, encoded_size);
	if (!result) failure = "could not write save file";
	if (result && image.roster_count != 0U) {
		qcx_connected_snapshot = (qcx_connected_snapshot_t){
			.hash = QCX_SaveFingerprint(encoded, encoded_size), .size = encoded_size, .valid = true};
	} else if (result) {
		QCX_SaveInvalidateConnectedSnapshot();
	}
done:
	if (!result) Con_Printf("qc2cpp save rejected: %s.\n", failure);
	free(encoded); free(engine.data); free(guest.data); return result;
}

static qbool QCX_SaveReadFile(const char *name, uint8_t **out, uint32_t *out_size)
{
	char path[MAX_OSPATH];
	FILE *file;
	long length;
	uint8_t *bytes;
	qbool read;
	int close_status;
	const int path_size = snprintf(path, sizeof(path), "%s/save/%s.sav", fs_gamedir, name);
	*out = NULL;
	*out_size = 0U;
	if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
	file = fopen(path, "rb");
	if (file == NULL || fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) <= 0L
		|| (uint64_t)length > 12U * 1024U * 1024U || fseek(file, 0L, SEEK_SET) != 0) {
		if (file != NULL) fclose(file);
		return false;
	}
	bytes = malloc((size_t)length);
	if (bytes == NULL) {
		fclose(file);
		return false;
	}
	read = fread(bytes, 1U, (size_t)length, file) == (size_t)length;
	close_status = fclose(file);
	if (!read || close_status != 0) {
		free(bytes);
		return false;
	}
	*out = bytes;
	*out_size = (uint32_t)length;
	return true;
}

void QCX_DiscardPreparedLoadGame(void)
{
	QCX_SaveImageFree(qcx_prepared_image);
	qcx_prepared_image = NULL;
}

qbool QCX_HasPreparedLoadGame(void)
{
	return qcx_prepared_image != NULL;
}

qbool QCX_PrepareLoadGame(const char *name, char *map_name, uint32_t map_name_size)
{
	uint8_t *bytes = NULL;
	uint32_t size = 0U;
	qcx_save_image_t *image = NULL;
	if (map_name == NULL || map_name_size == 0U || !QCX_SaveNameIsSafe(name)
		|| !QCX_SaveReadFile(name, &bytes, &size)
		|| QCX_SaveParse(bytes, size, &image) != QCX_PLUGIN_OK) {
		QCX_SaveImageFree(image);
		free(bytes);
		return false;
	}
	if (image->roster_count != 0U
		|| strcmp(image->metadata.logical_game, sv_progsname.string) != 0
		|| QCX_SaveBoundedStringLength(image->metadata.map_name, map_name_size - 1U)
		>= map_name_size) {
		Con_Printf("qc2cpp fresh restore rejected: saved game=%s, selected game=%s, map=%s, roster=%u.\n",
			image->metadata.logical_game, sv_progsname.string, image->metadata.map_name,
			(unsigned)image->roster_count);
		QCX_SaveImageFree(image);
		free(bytes);
		return false;
	}
	QCX_DiscardPreparedLoadGame();
	qcx_prepared_image = image;
	strlcpy(map_name, image->metadata.map_name, map_name_size);
	free(bytes);
	return true;
}

qbool QCX_PrepareLoadResources(void)
{
	qcx_save_reader_t reader;
	uint8_t models[MAX_MODELS] = {0};
	uint8_t sounds[MAX_SOUNDS] = {0};
	uint32_t version;
	uint32_t index;
	uint32_t count;
	uint32_t ignored;
	double ignored_time;
	char resource[QCX_SAVE_MAX_RESOURCE_BYTES + 1U];
	if (qcx_prepared_image == NULL) return false;
	/* PR_LoadProgs may invoke a qc2cpp host call before SV_SpawnServer installs
	 * its normal slot-zero sentinels.  Keep the indexed precache arrays
	 * traversable while restoring their saved order. */
	sv.model_precache[0] = "";
	sv.sound_precache[0] = "";
	reader = (qcx_save_reader_t){qcx_prepared_image->engine_state.data,
		qcx_prepared_image->engine_state.data + qcx_prepared_image->engine_state.size};
	if (!QCX_SaveReadU32(&reader, &version) || version != QCX_SAVE_ENGINE_VERSION
		|| !QCX_SaveReadF64(&reader, &ignored_time) || !QCX_SaveReadU32(&reader, &ignored)
		|| !QCX_SaveReadU32(&reader, &count) || count != MAX_LIGHTSTYLES) return false;
	for (index = 0U; index < count; ++index) {
		if (!QCX_SaveReadResource(&reader, resource, sizeof(resource))) return false;
	}
	if (!QCX_SaveReadU32(&reader, &count) || count > MAX_MODELS + MAX_SOUNDS) return false;
	for (index = 0U; index < count; ++index) {
		char kind;
		uint32_t slot;
		const char *name;
		char *persistent;
		if (!QCX_SaveReadResource(&reader, resource, sizeof(resource))
			|| !QCX_SaveParseIndexedResource(resource, &kind, &slot, &name)
			|| (kind == 'M' && (slot == 0U || slot >= MAX_MODELS || models[slot] != 0U))
			|| (kind == 'S' && (slot == 0U || slot >= MAX_SOUNDS || sounds[slot] != 0U))) return false;
		persistent = Hunk_Alloc((int)strlen(name) + 1);
		memcpy(persistent, name, strlen(name) + 1U);
		if (kind == 'M') {
			sv.model_precache[slot] = persistent;
			models[slot] = 1U;
		} else {
			sv.sound_precache[slot] = persistent;
			sounds[slot] = 1U;
		}
	}
	return true;
}

qbool QCX_CommitPreparedLoadGame(void)
{
	qcx_restore_status_t status;
	if (qcx_prepared_image == NULL) return false;
	status = QCX_ValidateSaveGame(qcx_prepared_image);
	if (status != QCX_RESTORE_OK) {
		Con_Printf("qc2cpp prepared restore rejected: status %u.\n", (unsigned)status);
		return false;
	}
	QCX_ApplySaveGame(qcx_prepared_image);
	QCX_DiscardPreparedLoadGame();
	return true;
}

qbool QCX_LoadGame(const char *name)
{
	uint8_t *bytes = NULL;
	uint32_t size = 0U;
	qcx_save_image_t *image = NULL;
	qbool result = false;
	if (!QCX_Active() || !QCX_SaveNameIsSafe(name) || !QCX_SaveReadFile(name, &bytes, &size)
		|| QCX_SaveParse(bytes, size, &image) != QCX_PLUGIN_OK
		|| QCX_ValidateSaveGame(image) != QCX_RESTORE_OK
		|| (image->roster_count != 0U
			&& (!qcx_connected_snapshot.valid || qcx_connected_snapshot.size != size
				|| qcx_connected_snapshot.hash != QCX_SaveFingerprint(bytes, size)))) goto done;
	QCX_ApplySaveGame(image);
	result = true;
done:
	QCX_SaveImageFree(image);
	free(bytes);
	return result;
}
