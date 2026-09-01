#include "qc2cpp/save.h"

#include "qc2cpp/adapter.h"
#include "qc2cpp/entities.h"
#if defined(MVDSV_QC2CPP_TESTS)
#include "qc2cpp/test_observer.h"
#endif

#include <limits.h>
#include <math.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	QC_SAVE_ENGINE_VERSION = 1U,
	QC_SAVE_MAX_GUEST_BYTES = 8U * 1024U * 1024U,
	QC_SAVE_MAX_RESOURCE_BYTES = 255U,
	QC_SAVE_ACTIVE_EDICT = 1U,
	QC_SAVE_FREE_EDICT = 2U,
	QC_SAVE_CLIENT_CONNECTED = 1U,
	QC_SAVE_CLIENT_SPAWNED = 2U,
	QC_SAVE_CLIENT_SPECTATOR = 4U
};

typedef struct qc_save_writer_s {
	uint8_t *cursor;
	const uint8_t *end;
} qc_save_writer_t;

typedef struct qc_connected_snapshot_s {
	uint64_t hash;
	uint32_t size;
	qbool valid;
} qc_connected_snapshot_t;

static qc_connected_snapshot_t qc_connected_snapshot;
static qc_save_image_t *qc_prepared_image;

static uint64_t QC_SaveFingerprint(const uint8_t *bytes, uint32_t size)
{
	uint64_t hash = UINT64_C(14695981039346656037);
	uint32_t index;
	for (index = 0U; index < size; ++index) {
		hash ^= bytes[index];
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

void QC_SaveInvalidateConnectedSnapshot(void)
{
	qc_connected_snapshot.valid = false;
}

typedef struct qc_save_reader_s {
	const uint8_t *cursor;
	const uint8_t *end;
} qc_save_reader_t;

typedef struct qc_restore_client_s {
	uint32_t slot;
	uint32_t flags;
	float spawn_parms[NUM_SPAWN_PARMS];
} qc_restore_client_t;

typedef struct qc_restore_plan_s {
	const qc_save_image_t *image;
	double time;
	uint32_t serverflags;
	uint32_t num_edicts;
	uint8_t selection[(QC_SAVE_MAX_ENTITY_CAPACITY + 7U) / 8U];
	uint32_t edict_flags[QC_SAVE_MAX_ENTITY_CAPACITY];
	float freetimes[QC_SAVE_MAX_ENTITY_CAPACITY];
	char lightstyles[MAX_LIGHTSTYLES][QC_SAVE_MAX_RESOURCE_BYTES + 1U];
	qc_restore_client_t clients[MAX_CLIENTS];
	uint32_t client_count;
} qc_restore_plan_t;

static qc_restore_plan_t qc_restore_plan;
static char qc_restored_lightstyles[MAX_LIGHTSTYLES][QC_SAVE_MAX_RESOURCE_BYTES + 1U];

static qbool QC_SaveRead(qc_save_reader_t *reader, void *out, uint32_t size)
{
	if ((size_t)(reader->end - reader->cursor) < size) return false;
	if (size != 0U) { memcpy(out, reader->cursor, size); reader->cursor += size; }
	return true;
}

static qbool QC_SaveReadU32(qc_save_reader_t *reader, uint32_t *out)
{
	uint8_t bytes[4];
	if (!QC_SaveRead(reader, bytes, sizeof(bytes))) return false;
	*out = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16)
		| ((uint32_t)bytes[3] << 24);
	return true;
}

static qbool QC_SaveReadF32(qc_save_reader_t *reader, float *out)
{
	uint32_t bits;
	if (!QC_SaveReadU32(reader, &bits)) return false;
	memcpy(out, &bits, sizeof(bits));
	return isfinite(*out);
}

static qbool QC_SaveReadF64(qc_save_reader_t *reader, double *out)
{
	uint8_t bytes[8];
	uint64_t bits = 0U;
	uint32_t index;
	if (!QC_SaveRead(reader, bytes, sizeof(bytes))) return false;
	for (index = 0U; index < sizeof(bytes); ++index) bits |= (uint64_t)bytes[index] << (8U * index);
	memcpy(out, &bits, sizeof(bits));
	return isfinite(*out);
}

static qbool QC_SaveReadResource(qc_save_reader_t *reader, char *out, uint32_t capacity)
{
	uint32_t size;
	uint32_t index;
	if (!QC_SaveReadU32(reader, &size) || size == 0U || size >= capacity
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

static qbool QC_SaveWrite(qc_save_writer_t *writer, const void *bytes, uint32_t size)
{
	if ((size_t)(writer->end - writer->cursor) < size) return false;
	if (size != 0U) { memcpy(writer->cursor, bytes, size); writer->cursor += size; }
	return true;
}

static qbool QC_SaveWriteU32(qc_save_writer_t *writer, uint32_t value)
{
	const uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8),
		(uint8_t)(value >> 16), (uint8_t)(value >> 24)};
	return QC_SaveWrite(writer, bytes, sizeof(bytes));
}

static qbool QC_SaveWriteF32(qc_save_writer_t *writer, float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return QC_SaveWriteU32(writer, bits);
}

static qbool QC_SaveWriteF64(qc_save_writer_t *writer, double value)
{
	uint64_t bits;
	uint8_t bytes[8];
	uint32_t index;
	memcpy(&bits, &value, sizeof(bits));
	for (index = 0U; index < sizeof(bytes); ++index) bytes[index] = (uint8_t)(bits >> (8U * index));
	return QC_SaveWrite(writer, bytes, sizeof(bytes));
}

static uint32_t QC_SaveBoundedStringLength(const char *value, uint32_t limit)
{
	uint32_t length;
	if (value == NULL) return 0U;
	for (length = 0U; length <= limit; ++length) if (value[length] == '\0') return length;
	return limit + 1U;
}

static qbool QC_SaveWriteResource(qc_save_writer_t *writer, const char *prefix,
	const char *value)
{
	const uint32_t prefix_size = QC_SaveBoundedStringLength(prefix, 16U);
	const uint32_t value_size = QC_SaveBoundedStringLength(value, QC_SAVE_MAX_RESOURCE_BYTES - prefix_size);
	const uint32_t size = prefix_size + value_size;
	uint32_t index;
	if (prefix_size == 0U || prefix_size > 16U || value_size == 0U
		|| value_size > QC_SAVE_MAX_RESOURCE_BYTES - prefix_size) return false;
	for (index = 0U; index < value_size; ++index) {
		const uint8_t byte = (uint8_t)value[index];
		if (byte < 32U || byte > 126U || byte == '\\') return false;
	}
	return QC_SaveWriteU32(writer, size) && QC_SaveWrite(writer, prefix, prefix_size)
		&& QC_SaveWrite(writer, value, value_size);
}

static uint32_t QC_SaveResourcePrefixSize(uint32_t index)
{
	uint32_t digits = 1U;
	while (index >= 10U) {
		index /= 10U;
		++digits;
	}
	return digits + 3U;
}

static qbool QC_SaveWriteIndexedResource(qc_save_writer_t *writer, char kind,
	uint32_t index, const char *value)
{
	char prefix[16];
	const int written = snprintf(prefix, sizeof(prefix), "%c:%u:", kind, index);
	return written > 0 && (size_t)written < sizeof(prefix)
		&& QC_SaveWriteResource(writer, prefix, value);
}

static qbool QC_SaveWriteLightstyle(qc_save_writer_t *writer, const char *value)
{
	const char *const saved = value == NULL ? "m" : value;
	const uint32_t size = QC_SaveBoundedStringLength(saved, QC_SAVE_MAX_RESOURCE_BYTES);
	uint32_t index;
	if (size == 0U || size > QC_SAVE_MAX_RESOURCE_BYTES) return false;
	for (index = 0U; index < size; ++index) {
		const uint8_t byte = (uint8_t)saved[index];
		if (byte < 32U || byte > 126U || byte == '\\') return false;
	}
	return QC_SaveWriteU32(writer, size) && QC_SaveWrite(writer, saved, size);
}

static qbool QC_SaveAppendSize(uint32_t *size, uint32_t add)
{
	if (*size > UINT32_MAX - add) return false;
	*size += add;
	return true;
}

static qbool QC_SaveEngineSize(uint32_t entity_capacity, uint32_t *out_size,
	uint32_t *out_precache_count, uint32_t *out_client_count)
{
	uint32_t size = 4U + 8U + 4U + 4U;
	uint32_t precache_count = 0U;
	uint32_t client_count = 0U;
	int index;
	for (index = 0; index < MAX_LIGHTSTYLES; ++index) {
		const uint32_t length = QC_SaveBoundedStringLength(sv.lightstyles[index] == NULL ? "m" : sv.lightstyles[index], QC_SAVE_MAX_RESOURCE_BYTES);
		if (length == 0U || length > QC_SAVE_MAX_RESOURCE_BYTES || !QC_SaveAppendSize(&size, 4U + length)) return false;
	}
	if (!QC_SaveAppendSize(&size, 4U)) return false;
	for (index = 1; index < MAX_MODELS; ++index) if (sv.model_precache[index] != NULL && sv.model_precache[index][0] != '\0') {
		const uint32_t prefix_size = QC_SaveResourcePrefixSize((uint32_t)index);
		const uint32_t length = QC_SaveBoundedStringLength(sv.model_precache[index], QC_SAVE_MAX_RESOURCE_BYTES - prefix_size);
		if (length == 0U || length > QC_SAVE_MAX_RESOURCE_BYTES - prefix_size || !QC_SaveAppendSize(&size, 4U + prefix_size + length)) return false;
		++precache_count;
	}
	for (index = 1; index < MAX_SOUNDS; ++index) if (sv.sound_precache[index] != NULL && sv.sound_precache[index][0] != '\0') {
		const uint32_t prefix_size = QC_SaveResourcePrefixSize((uint32_t)index);
		const uint32_t length = QC_SaveBoundedStringLength(sv.sound_precache[index], QC_SAVE_MAX_RESOURCE_BYTES - prefix_size);
		if (length == 0U || length > QC_SAVE_MAX_RESOURCE_BYTES - prefix_size || !QC_SaveAppendSize(&size, 4U + prefix_size + length)) return false;
		++precache_count;
	}
	if (precache_count > MAX_MODELS + MAX_SOUNDS || !QC_SaveAppendSize(&size, 4U)
		|| !QC_SaveAppendSize(&size, entity_capacity * 8U) || !QC_SaveAppendSize(&size, 4U)) return false;
	for (index = 0; index < MAX_CLIENTS; ++index) if (svs.clients[index].state == cs_connected || svs.clients[index].state == cs_spawned) {
		if (!QC_SaveAppendSize(&size, 8U + NUM_SPAWN_PARMS * 4U)) return false;
		++client_count;
	}
	*out_size = size; *out_precache_count = precache_count; *out_client_count = client_count;
	return true;
}

static uint32_t QC_SaveClientFlags(const client_t *client)
{
	uint32_t flags = QC_SAVE_CLIENT_CONNECTED;
	if (client->state == cs_spawned) flags |= QC_SAVE_CLIENT_SPAWNED;
	if (client->spectator != 0) flags |= QC_SAVE_CLIENT_SPECTATOR;
	return flags;
}

static qbool QC_SaveBuildEngine(uint32_t entity_capacity, qc_save_bytes_t *out)
{
	uint32_t size, precache_count, client_count, written_clients = 0U;
	qc_save_writer_t writer;
	int index;
	if (sv.max_edicts <= 0 || entity_capacity == 0U || entity_capacity > QC_SAVE_MAX_ENTITY_CAPACITY
		|| (uint32_t)sv.max_edicts > entity_capacity
		|| !QC_SaveEngineSize(entity_capacity, &size, &precache_count, &client_count)) return false;
	out->data = malloc(size); out->size = 0U;
	if (out->data == NULL) return false;
	writer = (qc_save_writer_t){out->data, out->data + size};
	if (!QC_SaveWriteU32(&writer, QC_SAVE_ENGINE_VERSION) || !QC_SaveWriteF64(&writer, sv.time)
		|| !QC_SaveWriteU32(&writer, svs.serverflags) || !QC_SaveWriteU32(&writer, MAX_LIGHTSTYLES)) goto fail;
	for (index = 0; index < MAX_LIGHTSTYLES; ++index) if (!QC_SaveWriteLightstyle(&writer, sv.lightstyles[index])) goto fail;
	if (!QC_SaveWriteU32(&writer, precache_count)) goto fail;
	for (index = 1; index < MAX_MODELS; ++index) if (sv.model_precache[index] != NULL && sv.model_precache[index][0] != '\0'
		&& !QC_SaveWriteIndexedResource(&writer, 'M', (uint32_t)index,
			sv.model_precache[index])) goto fail;
	for (index = 1; index < MAX_SOUNDS; ++index) if (sv.sound_precache[index] != NULL && sv.sound_precache[index][0] != '\0'
		&& !QC_SaveWriteIndexedResource(&writer, 'S', (uint32_t)index,
			sv.sound_precache[index])) goto fail;
	if (!QC_SaveWriteU32(&writer, entity_capacity)) goto fail;
	for (index = 0; index < (int)entity_capacity; ++index) {
		const uint32_t flags = index >= sv.num_edicts ? 0U : sv.edicts[index].e.free ? QC_SAVE_FREE_EDICT : QC_SAVE_ACTIVE_EDICT;
		if (!QC_SaveWriteU32(&writer, flags) || !QC_SaveWriteF32(&writer, sv.edicts[index].e.freetime)) goto fail;
	}
	if (!QC_SaveWriteU32(&writer, client_count)) goto fail;
	for (index = 0; index < MAX_CLIENTS; ++index) {
		client_t *const client = &svs.clients[index]; int parm;
		if (client->state != cs_connected && client->state != cs_spawned) continue;
		if (!QC_SaveWriteU32(&writer, (uint32_t)index)
			|| !QC_SaveWriteU32(&writer, QC_SaveClientFlags(client))) goto fail;
		for (parm = 0; parm < NUM_SPAWN_PARMS; ++parm) if (!QC_SaveWriteF32(&writer, client->spawn_parms[parm])) goto fail;
		++written_clients;
	}
	if (writer.cursor != writer.end || written_clients != client_count) goto fail;
	out->size = size; return true;
fail:
	free(out->data); out->data = NULL; return false;
}

static void QC_SaveSelection(uint8_t *bitmap, uint32_t size)
{
	int slot;
	memset(bitmap, 0, size);
	for (slot = 0; slot < sv.num_edicts; ++slot) if (!sv.edicts[slot].e.free) bitmap[(uint32_t)slot / 8U] |= (uint8_t)(1U << ((uint32_t)slot % 8U));
}

static qbool QC_SaveNameIsSafe(const char *name)
{
	uint32_t index;
	const uint32_t size = QC_SaveBoundedStringLength(name, QC_SAVE_NAME_CAPACITY - 1U);
	if (size == 0U || size >= QC_SAVE_NAME_CAPACITY) return false;
	for (index = 0U; index < size; ++index) {
		const unsigned char byte = (unsigned char)name[index];
		if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '_' || byte == '-')) return false;
	}
	return true;
}

static qbool QC_SaveWriteFile(const char *name, const uint8_t *bytes, uint32_t size)
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

static qbool QC_SaveParseIndexedResource(const char *resource, char *kind,
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

static qc_restore_status_t QC_SaveDecodeEngine(const qc_save_image_t *image)
{
	qc_save_reader_t reader;
	uint8_t models[MAX_MODELS] = {0};
	uint8_t sounds[MAX_SOUNDS] = {0};
	uint8_t clients[MAX_CLIENTS] = {0};
	uint32_t version;
	uint32_t count;
	uint32_t index;
	qbool unused_slot = false;
	char resource[QC_SAVE_MAX_RESOURCE_BYTES + 1U];

	if (image == NULL || image->engine_state.data == NULL || image->engine_state.size == 0U
		|| image->metadata.entity_capacity != QC_EntityCapacity()
		|| strcmp(image->metadata.logical_game, sv_progsname.string) != 0
		|| strcmp(image->metadata.map_name, sv.mapname) != 0
		|| image->metadata.map_bsp_checksum != sv.map_checksum) {
		Con_Printf("qc2cpp restore engine identity mismatch: game=%s map=%s capacity=%u checksum=%u.\n",
			sv_progsname.string, sv.mapname, QC_EntityCapacity(), sv.map_checksum);
		return QC_RESTORE_ENTITY_SET_MISMATCH;
	}
	reader = (qc_save_reader_t){image->engine_state.data,
		image->engine_state.data + image->engine_state.size};
	if (!QC_SaveReadU32(&reader, &version) || version != QC_SAVE_ENGINE_VERSION
		|| !QC_SaveReadF64(&reader, &qc_restore_plan.time)
		|| !QC_SaveReadU32(&reader, &qc_restore_plan.serverflags)
		|| !QC_SaveReadU32(&reader, &count) || count != MAX_LIGHTSTYLES) return QC_RESTORE_MALFORMED_CHUNK;
	for (index = 0U; index < MAX_LIGHTSTYLES; ++index) {
		if (!QC_SaveReadResource(&reader, qc_restore_plan.lightstyles[index],
			sizeof(qc_restore_plan.lightstyles[index]))) return QC_RESTORE_MALFORMED_CHUNK;
	}
	if (!QC_SaveReadU32(&reader, &count) || count > MAX_MODELS + MAX_SOUNDS) return QC_RESTORE_MALFORMED_CHUNK;
	for (index = 0U; index < count; ++index) {
		char kind;
		uint32_t slot;
		const char *name;
		if (!QC_SaveReadResource(&reader, resource, sizeof(resource))
			|| !QC_SaveParseIndexedResource(resource, &kind, &slot, &name)) return QC_RESTORE_MALFORMED_CHUNK;
		if (kind == 'M') {
			if (slot == 0U || slot >= MAX_MODELS || models[slot] != 0U
				|| sv.model_precache[slot] == NULL || strcmp(name, sv.model_precache[slot]) != 0) {
				Con_Printf("qc2cpp restore model mismatch at %u: expected %s, got %s.\n", slot,
					name, slot < MAX_MODELS && sv.model_precache[slot] != NULL ? sv.model_precache[slot] : "<none>");
				return QC_RESTORE_ENTITY_SET_MISMATCH;
			}
			models[slot] = 1U;
		} else {
			if (slot == 0U || slot >= MAX_SOUNDS || sounds[slot] != 0U
				|| sv.sound_precache[slot] == NULL || strcmp(name, sv.sound_precache[slot]) != 0) {
				Con_Printf("qc2cpp restore sound mismatch at %u: expected %s, got %s.\n", slot,
					name, slot < MAX_SOUNDS && sv.sound_precache[slot] != NULL ? sv.sound_precache[slot] : "<none>");
				return QC_RESTORE_ENTITY_SET_MISMATCH;
			}
			sounds[slot] = 1U;
		}
	}
	if (qc_prepared_image == NULL) {
		for (index = 1U; index < MAX_MODELS; ++index) {
			if ((sv.model_precache[index] != NULL && sv.model_precache[index][0] != '\0')
				!= (models[index] != 0U)) return QC_RESTORE_ENTITY_SET_MISMATCH;
		}
		for (index = 1U; index < MAX_SOUNDS; ++index) {
			if ((sv.sound_precache[index] != NULL && sv.sound_precache[index][0] != '\0')
				!= (sounds[index] != 0U)) return QC_RESTORE_ENTITY_SET_MISMATCH;
		}
	}
	if (!QC_SaveReadU32(&reader, &count) || count != image->metadata.entity_capacity) return QC_RESTORE_MALFORMED_CHUNK;
	memset(qc_restore_plan.selection, 0, sizeof(qc_restore_plan.selection));
	qc_restore_plan.num_edicts = 0U;
	for (index = 0U; index < count; ++index) {
		if (!QC_SaveReadU32(&reader, &qc_restore_plan.edict_flags[index])
			|| qc_restore_plan.edict_flags[index] > QC_SAVE_FREE_EDICT
			|| !QC_SaveReadF32(&reader, &qc_restore_plan.freetimes[index])) return QC_RESTORE_MALFORMED_CHUNK;
		if (index == 0U && qc_restore_plan.edict_flags[index] != QC_SAVE_ACTIVE_EDICT) {
			return QC_RESTORE_ENTITY_SET_MISMATCH;
		}
		if (unused_slot && qc_restore_plan.edict_flags[index] != 0U) return QC_RESTORE_MALFORMED_CHUNK;
		if (qc_restore_plan.edict_flags[index] == 0U) {
			unused_slot = true;
		} else {
			qc_restore_plan.num_edicts = index + 1U;
			if (qc_restore_plan.edict_flags[index] == QC_SAVE_ACTIVE_EDICT) qc_restore_plan.selection[index / 8U] |= (uint8_t)(1U << (index % 8U));
		}
	}
	if (!QC_SaveReadU32(&reader, &count) || count > MAX_CLIENTS) return QC_RESTORE_MALFORMED_CHUNK;
	qc_restore_plan.client_count = count;
	for (index = 0U; index < count; ++index) {
		qc_restore_client_t *const client = &qc_restore_plan.clients[index];
		uint32_t parm;
		if (!QC_SaveReadU32(&reader, &client->slot) || client->slot >= MAX_CLIENTS
			|| clients[client->slot] != 0U || !QC_SaveReadU32(&reader, &client->flags)
			|| (client->flags & ~(QC_SAVE_CLIENT_CONNECTED | QC_SAVE_CLIENT_SPAWNED | QC_SAVE_CLIENT_SPECTATOR)) != 0U
			|| (client->flags & QC_SAVE_CLIENT_CONNECTED) == 0U) return QC_RESTORE_MALFORMED_CHUNK;
		clients[client->slot] = 1U;
		for (parm = 0U; parm < NUM_SPAWN_PARMS; ++parm) if (!QC_SaveReadF32(&reader, &client->spawn_parms[parm])) return QC_RESTORE_MALFORMED_CHUNK;
	}
	if (reader.cursor != reader.end
		|| (image->metadata.contains_connected_clients != 0U) != (count != 0U)) return QC_RESTORE_MALFORMED_CHUNK;
	for (index = 0U; index < MAX_CLIENTS; ++index) {
		const qbool currently_connected = svs.clients[index].state == cs_connected || svs.clients[index].state == cs_spawned;
		if (currently_connected != (clients[index] != 0U)) return QC_RESTORE_ENTITY_SET_MISMATCH;
		if (clients[index] != 0U) {
			uint32_t client_index;
			for (client_index = 0U; client_index < qc_restore_plan.client_count; ++client_index) {
				if (qc_restore_plan.clients[client_index].slot == index
					&& qc_restore_plan.clients[client_index].flags
						!= QC_SaveClientFlags(&svs.clients[index])) {
					return QC_RESTORE_ENTITY_SET_MISMATCH;
				}
			}
		}
	}
	return QC_RESTORE_OK;
}

qc_restore_status_t QC_ValidateSaveGame(const qc_save_image_t *image)
{
	qc_restore_status_t status;
	qc_restore_plan.image = NULL;
	status = QC_SaveDecodeEngine(image);
	if (status != QC_RESTORE_OK) return status;
	status = QC_ValidateGuestRestore(image->guest_payload.data, image->guest_payload.size,
		qc_restore_plan.selection, (image->metadata.entity_capacity + 7U) / 8U);
	if (status != QC_RESTORE_OK) return status;
	qc_restore_plan.image = image;
	return QC_RESTORE_OK;
}

static void QC_RefreshClientReplication(void)
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

void QC_ApplySaveGame(const qc_save_image_t *image)
{
	uint32_t index;
	if (image == NULL || qc_restore_plan.image != image
		|| QC_SetSaveSelection(qc_restore_plan.selection,
			(image->metadata.entity_capacity + 7U) / 8U) != QC_RESTORE_OK
		|| QC_RestoreGuest(image->guest_payload.data, image->guest_payload.size) != QC_RESTORE_OK) {
		SV_Error("qc2cpp restore failed after commit");
	}
	for (index = 0U; index < MAX_LIGHTSTYLES; ++index) {
		memcpy(qc_restored_lightstyles[index], qc_restore_plan.lightstyles[index],
			sizeof(qc_restored_lightstyles[index]));
		sv.lightstyles[index] = qc_restored_lightstyles[index];
	}
	for (index = 0U; index < image->metadata.entity_capacity; ++index) {
		sv.edicts[index].e.free = qc_restore_plan.edict_flags[index] != QC_SAVE_ACTIVE_EDICT;
		sv.edicts[index].e.freetime = qc_restore_plan.freetimes[index];
	}
	/* Player edicts are reserved engine slots, not ordinary game objects.  A
	 * client-free QCMS image therefore must not make them visible merely because
	 * the guest's fixed entity storage contains a value at that index. */
	for (index = 0U; index < MAX_CLIENTS; ++index) {
		qbool restored_client = false;
		uint32_t client_index;
		for (client_index = 0U; client_index < qc_restore_plan.client_count; ++client_index) {
			if (qc_restore_plan.clients[client_index].slot == index) {
				restored_client = true;
				break;
			}
		}
		if (!restored_client) sv.edicts[index + 1U].e.free = true;
	}
	sv.num_edicts = (int)qc_restore_plan.num_edicts;
	sv.time = qc_restore_plan.time;
	sv.old_time = sv.time;
	svs.serverflags = qc_restore_plan.serverflags;
	*PR_Global_serverflags() = svs.serverflags;
	SV_ClearWorld();
	for (index = 0U; index < qc_restore_plan.num_edicts; ++index) {
		if (!sv.edicts[index].e.free) SV_LinkEdict(&sv.edicts[index], false);
	}
	for (index = 0U; index < qc_restore_plan.client_count; ++index) {
		qc_restore_client_t *const client = &qc_restore_plan.clients[index];
		memcpy(svs.clients[client->slot].spawn_parms, client->spawn_parms,
			sizeof(client->spawn_parms));
	}
#if defined(MVDSV_QC2CPP_TESTS)
	QC_TestObserverRestoreReplicationBegin();
#endif
	QC_RefreshClientReplication();
#if defined(MVDSV_QC2CPP_TESTS)
	QC_TestObserverRestoreReplicationComplete();
#endif
	qc_restore_plan.image = NULL;
}

qbool QC_SaveGame(const char *name)
{
	uint8_t selection[(QC_SAVE_MAX_ENTITY_CAPACITY + 7U) / 8U];
	const uint32_t entity_capacity = QC_EntityCapacity();
	const uint32_t selection_size = (entity_capacity + 7U) / 8U;
	qc_save_bytes_t engine = {0}, guest = {0}; qc_save_image_t image = {0};
	uint8_t *encoded = NULL; uint32_t encoded_size = 0U; qbool result = false;
	const char *failure = "unknown error";
	qc_byte_count_t required;
	if (!QC_Active() || sv.state != ss_active || sv.max_edicts <= 0 || entity_capacity == 0U
		|| entity_capacity > QC_SAVE_MAX_ENTITY_CAPACITY || (uint32_t)sv.max_edicts > entity_capacity
		|| !QC_SaveNameIsSafe(name)) {
		Con_Printf("qc2cpp save rejected: inactive or incompatible server state.\n");
		return false;
	}
	QC_SaveSelection(selection, selection_size);
	if (QC_SetSaveSelection(selection, selection_size) != QC_RESTORE_OK) {
		Con_Printf("qc2cpp save rejected: game rejected entity selection.\n");
		return false;
	}
	required = QC_SaveGuest(NULL, 0U);
	if (required == 0U || required > QC_SAVE_MAX_GUEST_BYTES) {
		Con_Printf("qc2cpp save rejected: invalid game snapshot size.\n");
		return false;
	}
	guest.size = required;
	if (guest.size != 0U) {
		guest.data = malloc(guest.size);
		if (guest.data == NULL || QC_SaveGuest(guest.data, guest.size) != guest.size) {
			failure = "could not serialize game state";
			goto done;
		}
	}
	if (!QC_SaveBuildEngine(entity_capacity, &engine)) {
		failure = "could not serialize server state";
		goto done;
	}
	strlcpy(image.metadata.logical_game, sv_progsname.string, sizeof(image.metadata.logical_game));
	strlcpy(image.metadata.map_name, sv.mapname, sizeof(image.metadata.map_name));
	image.metadata.map_bsp_checksum = sv.map_checksum;
	image.metadata.entity_capacity = entity_capacity;
	for (int client = 0; client < MAX_CLIENTS; ++client) if (svs.clients[client].state == cs_connected || svs.clients[client].state == cs_spawned) { image.metadata.contains_connected_clients = 1U; break; }
	image.engine_state = engine; image.guest_payload = guest;
	if (QC_SaveEncode(&image, &encoded, &encoded_size) != QC_PLUGIN_OK) {
		failure = "could not encode QCMS image";
		goto done;
	}
	result = QC_SaveWriteFile(name, encoded, encoded_size);
	if (!result) failure = "could not write save file";
	if (result && image.metadata.contains_connected_clients != 0U) {
		qc_connected_snapshot = (qc_connected_snapshot_t){
			.hash = QC_SaveFingerprint(encoded, encoded_size), .size = encoded_size, .valid = true};
	} else if (result) {
		QC_SaveInvalidateConnectedSnapshot();
	}
done:
	if (!result) Con_Printf("qc2cpp save rejected: %s.\n", failure);
	free(encoded); free(engine.data); free(guest.data); return result;
}

static qbool QC_SaveReadFile(const char *name, uint8_t **out, uint32_t *out_size)
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

void QC_DiscardPreparedLoadGame(void)
{
	QC_SaveImageFree(qc_prepared_image);
	qc_prepared_image = NULL;
}

qbool QC_HasPreparedLoadGame(void)
{
	return qc_prepared_image != NULL;
}

qbool QC_PrepareLoadGame(const char *name, char *map_name, uint32_t map_name_size)
{
	uint8_t *bytes = NULL;
	uint32_t size = 0U;
	qc_save_image_t *image = NULL;
	if (map_name == NULL || map_name_size == 0U || !QC_SaveNameIsSafe(name)
		|| !QC_SaveReadFile(name, &bytes, &size)
		|| QC_SaveParse(bytes, size, &image) != QC_PLUGIN_OK) {
		QC_SaveImageFree(image);
		free(bytes);
		return false;
	}
	if (image->metadata.contains_connected_clients != 0U
		|| strcmp(image->metadata.logical_game, sv_progsname.string) != 0
		|| QC_SaveBoundedStringLength(image->metadata.map_name, map_name_size - 1U)
		>= map_name_size) {
		Con_Printf("qc2cpp fresh restore rejected: saved game=%s, selected game=%s, map=%s, connected=%u.\n",
			image->metadata.logical_game, sv_progsname.string, image->metadata.map_name,
			(unsigned)image->metadata.contains_connected_clients);
		QC_SaveImageFree(image);
		free(bytes);
		return false;
	}
	QC_DiscardPreparedLoadGame();
	qc_prepared_image = image;
	strlcpy(map_name, image->metadata.map_name, map_name_size);
	free(bytes);
	return true;
}

qbool QC_PrepareLoadResources(void)
{
	qc_save_reader_t reader;
	uint8_t models[MAX_MODELS] = {0};
	uint8_t sounds[MAX_SOUNDS] = {0};
	uint32_t version;
	uint32_t index;
	uint32_t count;
	uint32_t ignored;
	double ignored_time;
	char resource[QC_SAVE_MAX_RESOURCE_BYTES + 1U];
	if (qc_prepared_image == NULL) return false;
	/* PR_LoadProgs may invoke a qc2cpp host call before SV_SpawnServer installs
	 * its normal slot-zero sentinels.  Keep the indexed precache arrays
	 * traversable while restoring their saved order. */
	sv.model_precache[0] = "";
	sv.sound_precache[0] = "";
	reader = (qc_save_reader_t){qc_prepared_image->engine_state.data,
		qc_prepared_image->engine_state.data + qc_prepared_image->engine_state.size};
	if (!QC_SaveReadU32(&reader, &version) || version != QC_SAVE_ENGINE_VERSION
		|| !QC_SaveReadF64(&reader, &ignored_time) || !QC_SaveReadU32(&reader, &ignored)
		|| !QC_SaveReadU32(&reader, &count) || count != MAX_LIGHTSTYLES) return false;
	for (index = 0U; index < count; ++index) {
		if (!QC_SaveReadResource(&reader, resource, sizeof(resource))) return false;
	}
	if (!QC_SaveReadU32(&reader, &count) || count > MAX_MODELS + MAX_SOUNDS) return false;
	for (index = 0U; index < count; ++index) {
		char kind;
		uint32_t slot;
		const char *name;
		char *persistent;
		if (!QC_SaveReadResource(&reader, resource, sizeof(resource))
			|| !QC_SaveParseIndexedResource(resource, &kind, &slot, &name)
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

qbool QC_CommitPreparedLoadGame(void)
{
	qc_restore_status_t status;
	if (qc_prepared_image == NULL) return false;
	status = QC_ValidateSaveGame(qc_prepared_image);
	if (status != QC_RESTORE_OK) {
		Con_Printf("qc2cpp prepared restore rejected: status %u.\n", (unsigned)status);
		return false;
	}
	QC_ApplySaveGame(qc_prepared_image);
	QC_DiscardPreparedLoadGame();
	return true;
}

qbool QC_LoadGame(const char *name)
{
	uint8_t *bytes = NULL;
	uint32_t size = 0U;
	qc_save_image_t *image = NULL;
	qbool result = false;
	if (!QC_Active() || !QC_SaveNameIsSafe(name) || !QC_SaveReadFile(name, &bytes, &size)
		|| QC_SaveParse(bytes, size, &image) != QC_PLUGIN_OK
		|| QC_ValidateSaveGame(image) != QC_RESTORE_OK
		|| (image->metadata.contains_connected_clients != 0U
			&& (!qc_connected_snapshot.valid || qc_connected_snapshot.size != size
				|| qc_connected_snapshot.hash != QC_SaveFingerprint(bytes, size)))) goto done;
	QC_ApplySaveGame(image);
	result = true;
done:
	QC_SaveImageFree(image);
	free(bytes);
	return result;
}
