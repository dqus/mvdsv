#include "qcx/save_format.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	QCX_SAVE_VERSION = 2,
	QCX_SAVE_METADATA_SECTION = 1,
	QCX_SAVE_ENGINE_SECTION = 2,
	QCX_SAVE_ROSTER_SECTION = 3,
	QCX_SAVE_GUEST_SECTION = 4,
	QCX_SAVE_ENGINE_VERSION = 2,
	QCX_SAVE_ROSTER_VERSION = 1
};

typedef struct qcx_save_reader_s {
	const uint8_t *cursor;
	const uint8_t *end;
} qcx_save_reader_t;

typedef struct qcx_save_writer_s {
	uint8_t *cursor;
	const uint8_t *end;
} qcx_save_writer_t;

static bool QCX_SaveRead(qcx_save_reader_t *reader, void *out, uint32_t size)
{
	if ((size_t)(reader->end - reader->cursor) < size) return false;
	if (size != 0U) memcpy(out, reader->cursor, size);
	reader->cursor += size;
	return true;
}

static bool QCX_SaveReadU32(qcx_save_reader_t *reader, uint32_t *out)
{
	uint8_t bytes[4];
	if (!QCX_SaveRead(reader, bytes, sizeof(bytes))) return false;
	*out = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
		| ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
	return true;
}

static bool QCX_SaveReadU64(qcx_save_reader_t *reader, uint64_t *out)
{
	uint8_t bytes[8];
	uint32_t index;
	if (!QCX_SaveRead(reader, bytes, sizeof(bytes))) return false;
	*out = 0U;
	for (index = 0U; index < sizeof(bytes); ++index) {
		*out |= (uint64_t)bytes[index] << (8U * index);
	}
	return true;
}

static bool QCX_SaveReadF32(qcx_save_reader_t *reader, float *out)
{
	uint32_t bits;
	if (!QCX_SaveReadU32(reader, &bits)) return false;
	memcpy(out, &bits, sizeof(bits));
	return true;
}

static bool QCX_SaveReadF64(qcx_save_reader_t *reader, double *out)
{
	uint64_t bits;
	if (!QCX_SaveReadU64(reader, &bits)) return false;
	memcpy(out, &bits, sizeof(bits));
	return true;
}

static bool QCX_SaveWrite(qcx_save_writer_t *writer, const void *bytes, uint32_t size)
{
	if ((size_t)(writer->end - writer->cursor) < size) return false;
	if (size != 0U) memcpy(writer->cursor, bytes, size);
	writer->cursor += size;
	return true;
}

static bool QCX_SaveWriteU32(qcx_save_writer_t *writer, uint32_t value)
{
	const uint8_t bytes[4] = {
		(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
		(uint8_t)(value >> 24)};
	return QCX_SaveWrite(writer, bytes, sizeof(bytes));
}

static bool QCX_SaveWriteF32(qcx_save_writer_t *writer, float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return QCX_SaveWriteU32(writer, bits);
}

static bool QCX_SaveWriteF64(qcx_save_writer_t *writer, double value)
{
	uint64_t bits;
	uint8_t bytes[8];
	memcpy(&bits, &value, sizeof(bits));
	for (uint32_t index = 0U; index < sizeof(bytes); ++index) {
		bytes[index] = (uint8_t)(bits >> (8U * index));
	}
	return QCX_SaveWrite(writer, bytes, sizeof(bytes));
}

static bool QCX_SaveWriteString(qcx_save_writer_t *writer, const char *value,
	uint32_t size)
{
	return QCX_SaveWriteU32(writer, size) && QCX_SaveWrite(writer, value, size);
}

static bool QCX_SaveAddSize(uint32_t *total, uint32_t add)
{
	if (*total > UINT32_MAX - add) return false;
	*total += add;
	return true;
}

static uint32_t QCX_SaveStringLength(const char *value, uint32_t capacity)
{
	uint32_t length;
	if (value == NULL) return capacity;
	for (length = 0U; length < capacity; ++length) {
		if (value[length] == '\0') return length;
	}
	return capacity;
}

static bool QCX_SaveValidNameByte(uint8_t byte)
{
	return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
		|| (byte >= '0' && byte <= '9') || byte == '_' || byte == '-'
		|| byte == '.';
}

static bool QCX_SaveValidResourceByte(uint8_t byte)
{
	return byte >= 32U && byte <= 126U && byte != '\\';
}

static bool QCX_SaveValidRosterByte(uint8_t byte)
{
	return byte > 13U && byte != '\\' && byte != '"' && byte != '$'
		&& byte != ';' && (byte & 0x7fU) != 0U
		&& (byte & 0x7fU) != '\\';
}

static bool QCX_SaveReadString(qcx_save_reader_t *reader, char *out,
	uint32_t out_size, bool allow_empty, bool (*valid_byte)(uint8_t))
{
	uint32_t length;
	uint32_t index;
	if (!QCX_SaveReadU32(reader, &length) || (!allow_empty && length == 0U)
		|| length >= out_size || (size_t)(reader->end - reader->cursor) < length) {
		return false;
	}
	for (index = 0U; index < length; ++index) {
		if (!valid_byte(reader->cursor[index])) return false;
	}
	if (length != 0U) memcpy(out, reader->cursor, length);
	out[length] = '\0';
	reader->cursor += length;
	return true;
}

static bool QCX_SaveValidMetadata(const qcx_save_metadata_t *metadata,
	uint32_t *game_size, uint32_t *map_size)
{
	uint32_t local_game_size;
	uint32_t local_map_size;
	game_size = game_size == NULL ? &local_game_size : game_size;
	map_size = map_size == NULL ? &local_map_size : map_size;
	*game_size = QCX_SaveStringLength(metadata->logical_game, QCX_SAVE_NAME_CAPACITY);
	*map_size = QCX_SaveStringLength(metadata->map_name, QCX_SAVE_NAME_CAPACITY);
	if (*game_size == 0U || *game_size == QCX_SAVE_NAME_CAPACITY || *map_size == 0U
		|| *map_size == QCX_SAVE_NAME_CAPACITY || metadata->entity_capacity == 0U
		|| metadata->entity_capacity > QCX_SAVE_MAX_ENTITY_CAPACITY
		|| metadata->client_slot_capacity == 0U
		|| metadata->client_slot_capacity > QCX_SAVE_MAX_CLIENTS
		|| metadata->client_slot_capacity >= metadata->entity_capacity) {
		return false;
	}
	for (uint32_t index = 0U; index < *game_size; ++index) {
		if (!QCX_SaveValidNameByte((uint8_t)metadata->logical_game[index])) return false;
	}
	for (uint32_t index = 0U; index < *map_size; ++index) {
		if (!QCX_SaveValidNameByte((uint8_t)metadata->map_name[index])) return false;
	}
	return true;
}

static bool QCX_SaveParseMetadata(const uint8_t *bytes, uint32_t size,
	qcx_save_metadata_t *metadata)
{
	qcx_save_reader_t reader = {bytes, bytes + size};
	if (!QCX_SaveReadString(&reader, metadata->logical_game,
		QCX_SAVE_NAME_CAPACITY, false, QCX_SaveValidNameByte)
		|| !QCX_SaveReadString(&reader, metadata->map_name,
			QCX_SAVE_NAME_CAPACITY, false, QCX_SaveValidNameByte)
		|| !QCX_SaveReadU32(&reader, &metadata->map_bsp_checksum)
		|| !QCX_SaveReadU32(&reader, &metadata->entity_capacity)
		|| !QCX_SaveReadU32(&reader, &metadata->client_slot_capacity)
		|| reader.cursor != reader.end) {
		return false;
	}
	return QCX_SaveValidMetadata(metadata, NULL, NULL);
}

static uint32_t QCX_SaveResourcePrefixSize(uint32_t slot)
{
	uint32_t size = 4U; /* kind, two colons, and at least one decimal digit. */
	while (slot >= 10U) {
		slot /= 10U;
		++size;
	}
	return size;
}

static bool QCX_SaveValidResource(const char *value, uint32_t capacity)
{
	const uint32_t size = QCX_SaveStringLength(value, capacity);
	if (size == 0U || size == capacity) {
		return false;
	}
	for (uint32_t index = 0U; index < size; ++index) {
		if (!QCX_SaveValidResourceByte((uint8_t)value[index])) {
			return false;
		}
	}
	return true;
}

static bool QCX_SaveValidateEngine(const qcx_save_engine_t *engine,
	const qcx_save_metadata_t *metadata)
{
	uint8_t models[QCX_SAVE_MODEL_CAPACITY] = {0};
	uint8_t sounds[QCX_SAVE_SOUND_CAPACITY] = {0};
	bool unused_slot = false;
	if (!isfinite(engine->time) || engine->precache_count > QCX_SAVE_MAX_PRECACHES
		|| (engine->precache_count != 0U && engine->precaches == NULL)
		|| engine->edicts[0].state != QCX_SAVE_ACTIVE_EDICT) {
		return false;
	}
	for (uint32_t index = 0U; index < QCX_SAVE_LIGHTSTYLE_COUNT; ++index) {
		if (!QCX_SaveValidResource(engine->lightstyles[index], sizeof(engine->lightstyles[index]))) {
			return false;
		}
	}
	for (uint32_t index = 0U; index < engine->precache_count; ++index) {
		const qcx_save_precache_t *entry = &engine->precaches[index];
		uint8_t *slots;
		uint32_t capacity;
		if (entry->kind == 'M') {
			slots = models;
			capacity = QCX_SAVE_MODEL_CAPACITY;
		} else if (entry->kind == 'S') {
			slots = sounds;
			capacity = QCX_SAVE_SOUND_CAPACITY;
		} else {
			return false;
		}
		if (entry->slot == 0U || entry->slot >= capacity || slots[entry->slot] != 0U
			|| !QCX_SaveValidResource(entry->name,
				sizeof(entry->name) - QCX_SaveResourcePrefixSize(entry->slot))) {
			return false;
		}
		slots[entry->slot] = 1U;
	}
	for (uint32_t index = 0U; index < metadata->entity_capacity; ++index) {
		const qcx_save_edict_t *edict = &engine->edicts[index];
		if ((uint32_t)edict->state > QCX_SAVE_FREE_EDICT || !isfinite(edict->freetime)
			|| (unused_slot && edict->state != QCX_SAVE_UNUSED_EDICT)) {
			return false;
		}
		unused_slot = edict->state == QCX_SAVE_UNUSED_EDICT;
	}
	return true;
}

static bool QCX_SaveReadPrecache(qcx_save_reader_t *reader, qcx_save_precache_t *entry)
{
	char resource[QCX_SAVE_MAX_RESOURCE_BYTES + 1U];
	const char *cursor;
	uint32_t slot = 0U;
	if (!QCX_SaveReadString(reader, resource, sizeof(resource), false,
		QCX_SaveValidResourceByte) || (resource[0] != 'M' && resource[0] != 'S')
		|| resource[1] != ':') {
		return false;
	}
	cursor = resource + 2;
	if (*cursor < '0' || *cursor > '9') {
		return false;
	}
	while (*cursor >= '0' && *cursor <= '9') {
		if (slot > (UINT32_MAX - (uint32_t)(*cursor - '0')) / 10U) {
			return false;
		}
		slot = slot * 10U + (uint32_t)(*cursor++ - '0');
	}
	if (*cursor != ':' || cursor[1] == '\0') {
		return false;
	}
	entry->kind = resource[0];
	entry->slot = slot;
	memcpy(entry->name, cursor + 1, strlen(cursor + 1) + 1U);
	return true;
}

static bool QCX_SaveParseEngine(const uint8_t *bytes, uint32_t size,
	const qcx_save_metadata_t *metadata, qcx_save_engine_t *engine)
{
	qcx_save_reader_t reader = {bytes, bytes + size};
	uint32_t version;
	uint32_t count;
	if (!QCX_SaveReadU32(&reader, &version) || version != QCX_SAVE_ENGINE_VERSION
		|| !QCX_SaveReadF64(&reader, &engine->time)
		|| !QCX_SaveReadU32(&reader, &engine->serverflags)
		|| !QCX_SaveReadU32(&reader, &count) || count != QCX_SAVE_LIGHTSTYLE_COUNT) {
		return false;
	}
	for (uint32_t index = 0U; index < count; ++index) {
		if (!QCX_SaveReadString(&reader, engine->lightstyles[index],
			sizeof(engine->lightstyles[index]), false, QCX_SaveValidResourceByte)) {
			return false;
		}
	}
	if (!QCX_SaveReadU32(&reader, &engine->precache_count)
		|| engine->precache_count > QCX_SAVE_MAX_PRECACHES) {
		return false;
	}
	if (engine->precache_count != 0U) {
		engine->precaches = calloc(engine->precache_count, sizeof(*engine->precaches));
		if (engine->precaches == NULL) {
			return false;
		}
	}
	for (uint32_t index = 0U; index < engine->precache_count; ++index) {
		if (!QCX_SaveReadPrecache(&reader, &engine->precaches[index])) {
			return false;
		}
	}
	if (!QCX_SaveReadU32(&reader, &count) || count != metadata->entity_capacity) {
		return false;
	}
	for (uint32_t index = 0U; index < count; ++index) {
		uint32_t state;
		if (!QCX_SaveReadU32(&reader, &state)
			|| !QCX_SaveReadF32(&reader, &engine->edicts[index].freetime)) {
			return false;
		}
		engine->edicts[index].state = (qcx_save_edict_state_t)state;
	}
	return reader.cursor == reader.end && QCX_SaveValidateEngine(engine, metadata);
}

bool QCX_SaveClientNameEqual(const char *left, const char *right)
{
	if (left == NULL || right == NULL) return false;
	for (;;) {
		uint8_t left_byte = (uint8_t)*left++ & 0x7fU;
		uint8_t right_byte = (uint8_t)*right++ & 0x7fU;
		if (left_byte >= 'A' && left_byte <= 'Z') left_byte += 'a' - 'A';
		if (right_byte >= 'A' && right_byte <= 'Z') right_byte += 'a' - 'A';
		if (left_byte != right_byte) return false;
		if (left_byte == 0U) return true;
	}
}

static bool QCX_SaveValidRosterEntry(const qcx_save_roster_entry_t *entry)
{
	uint32_t name_size = QCX_SaveStringLength(entry->name,
		QCX_SAVE_CLIENT_STRING_CAPACITY);
	uint32_t team_size = QCX_SaveStringLength(entry->team,
		QCX_SAVE_CLIENT_STRING_CAPACITY);
	uint32_t index;
	if (name_size == 0U || name_size == QCX_SAVE_CLIENT_STRING_CAPACITY
		|| team_size == QCX_SAVE_CLIENT_STRING_CAPACITY || entry->spawned > 1U
		|| (entry->role != QCX_SAVE_ROLE_PLAYER
			&& entry->role != QCX_SAVE_ROLE_SPECTATOR)) {
		return false;
	}
	for (index = 0U; index < name_size; ++index) {
		if (!QCX_SaveValidRosterByte((uint8_t)entry->name[index])) return false;
	}
	for (index = 0U; index < team_size; ++index) {
		if (!QCX_SaveValidRosterByte((uint8_t)entry->team[index])) return false;
	}
	for (index = 0U; index < QCX_SAVE_SPAWN_PARM_COUNT; ++index) {
		if (!isfinite(entry->spawn_parms[index])) return false;
	}
	return true;
}

static bool QCX_SaveValidateRoster(const qcx_save_metadata_t *metadata,
	const qcx_save_roster_entry_t *roster, uint32_t roster_count,
	const qcx_save_engine_t *engine)
{
	uint8_t slots[QCX_SAVE_MAX_CLIENTS] = {0};
	uint32_t index;
	if (roster_count > QCX_SAVE_MAX_CLIENTS
		|| roster_count > metadata->client_slot_capacity) {
		return false;
	}
	for (index = 0U; index < roster_count; ++index) {
		const qcx_save_roster_entry_t *entry = &roster[index];
		uint32_t prior;
		if (!QCX_SaveValidRosterEntry(entry)
			|| entry->saved_slot >= metadata->client_slot_capacity
			|| entry->saved_slot + 1U >= metadata->entity_capacity
			|| engine->edicts[entry->saved_slot + 1U].state != QCX_SAVE_ACTIVE_EDICT
			|| slots[entry->saved_slot] != 0U) {
			return false;
		}
		slots[entry->saved_slot] = 1U;
		for (prior = 0U; prior < index; ++prior) {
			if (QCX_SaveClientNameEqual(entry->name, roster[prior].name)) return false;
		}
	}
	return true;
}

static bool QCX_SaveParseRoster(const uint8_t *bytes, uint32_t size,
	const qcx_save_metadata_t *metadata,
	qcx_save_image_t *image)
{
	qcx_save_reader_t reader = {bytes, bytes + size};
	uint32_t version;
	uint32_t index;
	uint32_t count;
	if (!QCX_SaveReadU32(&reader, &version) || version != QCX_SAVE_ROSTER_VERSION
		|| !QCX_SaveReadU32(&reader, &count) || count > QCX_SAVE_MAX_CLIENTS) {
		return false;
	}
	for (index = 0U; index < count; ++index) {
		qcx_save_roster_entry_t *entry = &image->roster[index];
		uint32_t parm;
		uint32_t role;
		if (!QCX_SaveReadU32(&reader, &entry->saved_slot)
			|| !QCX_SaveReadU32(&reader, &entry->spawned)
			|| !QCX_SaveReadU32(&reader, &role)
			|| !QCX_SaveReadString(&reader, entry->name,
				QCX_SAVE_CLIENT_STRING_CAPACITY, false, QCX_SaveValidRosterByte)
			|| !QCX_SaveReadString(&reader, entry->team,
				QCX_SAVE_CLIENT_STRING_CAPACITY, true, QCX_SaveValidRosterByte)) {
			return false;
		}
		entry->role = (qcx_save_role_t)role;
		for (parm = 0U; parm < QCX_SAVE_SPAWN_PARM_COUNT; ++parm) {
			if (!QCX_SaveReadF32(&reader, &entry->spawn_parms[parm])
				|| !isfinite(entry->spawn_parms[parm])) {
				return false;
			}
		}
	}
	if (reader.cursor != reader.end
		|| !QCX_SaveValidateRoster(metadata, image->roster, count, &image->engine)) {
		return false;
	}
	image->roster_count = count;
	return true;
}

static bool QCX_SaveCopy(qcx_save_bytes_t *out, const uint8_t *bytes, uint32_t size)
{
	out->data = NULL;
	out->size = 0U;
	if (size == 0U) return true;
	out->data = malloc(size);
	if (out->data == NULL) return false;
	memcpy(out->data, bytes, size);
	out->size = size;
	return true;
}

void QCX_SaveImageFree(qcx_save_image_t *image)
{
	if (image == NULL) return;
	free(image->engine.precaches);
	free(image->guest_payload.data);
	free(image);
}

qcx_plugin_status_t QCX_SaveParse(const uint8_t *bytes, uint32_t size,
	qcx_save_image_t **out)
{
	qcx_save_reader_t reader;
	qcx_save_image_t *image;
	uint32_t version;
	uint32_t section;
	if (out == NULL) return QCX_PLUGIN_BAD_ARGUMENT;
	*out = NULL;
	if (bytes == NULL || size < 8U || size > QCX_SAVE_MAX_FILE_BYTES
		|| memcmp(bytes, "QCMS", 4) != 0) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	reader = (qcx_save_reader_t){bytes + 4U, bytes + size};
	if (!QCX_SaveReadU32(&reader, &version) || version != QCX_SAVE_VERSION) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	image = calloc(1U, sizeof(*image));
	if (image == NULL) return QCX_PLUGIN_IO_ERROR;
	for (section = QCX_SAVE_METADATA_SECTION; section <= QCX_SAVE_GUEST_SECTION;
		++section) {
		uint32_t id;
		uint32_t section_size;
		const uint8_t *section_bytes;
		if (!QCX_SaveReadU32(&reader, &id) || id != section
			|| !QCX_SaveReadU32(&reader, &section_size)
			|| (size_t)(reader.end - reader.cursor) < section_size) {
			goto bad_argument;
		}
		section_bytes = reader.cursor;
		reader.cursor += section_size;
		if (section == QCX_SAVE_METADATA_SECTION) {
			if (!QCX_SaveParseMetadata(section_bytes, section_size, &image->metadata)) {
				goto bad_argument;
			}
		} else if (section == QCX_SAVE_ENGINE_SECTION) {
			if (!QCX_SaveParseEngine(section_bytes, section_size, &image->metadata,
				&image->engine)) {
				goto bad_argument;
			}
		} else if (section == QCX_SAVE_ROSTER_SECTION) {
			if (!QCX_SaveParseRoster(section_bytes, section_size, &image->metadata, image)) {
				goto bad_argument;
			}
		} else {
			if (section_size > QCX_SAVE_MAX_GUEST_BYTES
				|| !QCX_SaveCopy(&image->guest_payload, section_bytes, section_size)) {
				goto bad_argument;
			}
		}
	}
	if (reader.cursor != reader.end) goto bad_argument;
	*out = image;
	return QCX_PLUGIN_OK;

bad_argument:
	QCX_SaveImageFree(image);
	return QCX_PLUGIN_BAD_ARGUMENT;
}

static bool QCX_SaveRosterSize(const qcx_save_image_t *image, uint32_t *size)
{
	uint32_t total = 8U;
	uint32_t index;
	for (index = 0U; index < image->roster_count; ++index) {
		const qcx_save_roster_entry_t *entry = &image->roster[index];
		const uint32_t name_size = QCX_SaveStringLength(entry->name,
			QCX_SAVE_CLIENT_STRING_CAPACITY);
		const uint32_t team_size = QCX_SaveStringLength(entry->team,
			QCX_SAVE_CLIENT_STRING_CAPACITY);
		if (!QCX_SaveAddSize(&total, 12U) || !QCX_SaveAddSize(&total, 4U + name_size)
			|| !QCX_SaveAddSize(&total, 4U + team_size)
			|| !QCX_SaveAddSize(&total, QCX_SAVE_SPAWN_PARM_COUNT * 4U)) {
			return false;
		}
	}
	*size = total;
	return true;
}

static bool QCX_SaveWriteMetadata(qcx_save_writer_t *writer,
	const qcx_save_metadata_t *metadata, uint32_t game_size, uint32_t map_size)
{
	return QCX_SaveWriteString(writer, metadata->logical_game, game_size)
		&& QCX_SaveWriteString(writer, metadata->map_name, map_size)
		&& QCX_SaveWriteU32(writer, metadata->map_bsp_checksum)
		&& QCX_SaveWriteU32(writer, metadata->entity_capacity)
		&& QCX_SaveWriteU32(writer, metadata->client_slot_capacity);
}

static bool QCX_SaveWriteRoster(qcx_save_writer_t *writer,
	const qcx_save_image_t *image)
{
	uint32_t index;
	if (!QCX_SaveWriteU32(writer, QCX_SAVE_ROSTER_VERSION)
		|| !QCX_SaveWriteU32(writer, image->roster_count)) {
		return false;
	}
	for (index = 0U; index < image->roster_count; ++index) {
		const qcx_save_roster_entry_t *entry = &image->roster[index];
		const uint32_t name_size = QCX_SaveStringLength(entry->name,
			QCX_SAVE_CLIENT_STRING_CAPACITY);
		const uint32_t team_size = QCX_SaveStringLength(entry->team,
			QCX_SAVE_CLIENT_STRING_CAPACITY);
		uint32_t parm;
		if (!QCX_SaveWriteU32(writer, entry->saved_slot)
			|| !QCX_SaveWriteU32(writer, entry->spawned)
			|| !QCX_SaveWriteU32(writer, (uint32_t)entry->role)
			|| !QCX_SaveWriteString(writer, entry->name, name_size)
			|| !QCX_SaveWriteString(writer, entry->team, team_size)) {
			return false;
		}
		for (parm = 0U; parm < QCX_SAVE_SPAWN_PARM_COUNT; ++parm) {
			if (!QCX_SaveWriteF32(writer, entry->spawn_parms[parm])) return false;
		}
	}
	return true;
}

bool QCX_SaveImageValid(const qcx_save_image_t *image)
{
	return image != NULL && QCX_SaveValidMetadata(&image->metadata, NULL, NULL)
		&& image->guest_payload.size <= QCX_SAVE_MAX_GUEST_BYTES
		&& (image->guest_payload.size == 0U || image->guest_payload.data != NULL)
		&& QCX_SaveValidateEngine(&image->engine, &image->metadata)
		&& QCX_SaveValidateRoster(&image->metadata, image->roster,
			image->roster_count, &image->engine);
}

static bool QCX_SaveEngineSize(const qcx_save_image_t *image, uint32_t *size)
{
	const qcx_save_engine_t *engine = &image->engine;
	uint32_t total = 4U + 8U + 4U + 4U + 4U + 4U;
	for (uint32_t index = 0U; index < QCX_SAVE_LIGHTSTYLE_COUNT; ++index) {
		if (!QCX_SaveAddSize(&total, 4U + (uint32_t)strlen(engine->lightstyles[index]))) {
			return false;
		}
	}
	for (uint32_t index = 0U; index < engine->precache_count; ++index) {
		const qcx_save_precache_t *entry = &engine->precaches[index];
		if (!QCX_SaveAddSize(&total, 4U + QCX_SaveResourcePrefixSize(entry->slot)
			+ (uint32_t)strlen(entry->name))) {
			return false;
		}
	}
	if (!QCX_SaveAddSize(&total, image->metadata.entity_capacity * 8U)) {
		return false;
	}
	*size = total;
	return true;
}

static bool QCX_SaveWriteEngine(qcx_save_writer_t *writer, const qcx_save_image_t *image)
{
	const qcx_save_engine_t *engine = &image->engine;
	if (!QCX_SaveWriteU32(writer, QCX_SAVE_ENGINE_VERSION)
		|| !QCX_SaveWriteF64(writer, engine->time)
		|| !QCX_SaveWriteU32(writer, engine->serverflags)
		|| !QCX_SaveWriteU32(writer, QCX_SAVE_LIGHTSTYLE_COUNT)) {
		return false;
	}
	for (uint32_t index = 0U; index < QCX_SAVE_LIGHTSTYLE_COUNT; ++index) {
		if (!QCX_SaveWriteString(writer, engine->lightstyles[index],
			(uint32_t)strlen(engine->lightstyles[index]))) {
			return false;
		}
	}
	if (!QCX_SaveWriteU32(writer, engine->precache_count)) {
		return false;
	}
	for (uint32_t index = 0U; index < engine->precache_count; ++index) {
		const qcx_save_precache_t *entry = &engine->precaches[index];
		char prefix[16];
		const int prefix_size = snprintf(prefix, sizeof(prefix), "%c:%u:", entry->kind, entry->slot);
		const uint32_t name_size = (uint32_t)strlen(entry->name);
		if (prefix_size <= 0 || (size_t)prefix_size >= sizeof(prefix)
			|| !QCX_SaveWriteU32(writer, (uint32_t)prefix_size + name_size)
			|| !QCX_SaveWrite(writer, prefix, (uint32_t)prefix_size)
			|| !QCX_SaveWrite(writer, entry->name, name_size)) {
			return false;
		}
	}
	if (!QCX_SaveWriteU32(writer, image->metadata.entity_capacity)) {
		return false;
	}
	for (uint32_t index = 0U; index < image->metadata.entity_capacity; ++index) {
		if (!QCX_SaveWriteU32(writer, (uint32_t)engine->edicts[index].state)
			|| !QCX_SaveWriteF32(writer, engine->edicts[index].freetime)) {
			return false;
		}
	}
	return true;
}

qcx_plugin_status_t QCX_SaveEncode(const qcx_save_image_t *image, uint8_t **out,
	uint32_t *size)
{
	qcx_save_writer_t writer;
	uint32_t game_size;
	uint32_t map_size;
	uint32_t metadata_size;
	uint32_t roster_size;
	uint32_t engine_size;
	uint32_t total_size = 8U;
	uint8_t *bytes;
	if (out == NULL || size == NULL) return QCX_PLUGIN_BAD_ARGUMENT;
	*out = NULL;
	*size = 0U;
	if (!QCX_SaveImageValid(image) || !QCX_SaveValidMetadata(&image->metadata,
		&game_size, &map_size) || !QCX_SaveRosterSize(image, &roster_size)
		|| !QCX_SaveEngineSize(image, &engine_size)) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	metadata_size = 4U + game_size + 4U + map_size + 12U;
	if (!QCX_SaveAddSize(&total_size, 8U) || !QCX_SaveAddSize(&total_size, metadata_size)
		|| !QCX_SaveAddSize(&total_size, 8U)
		|| !QCX_SaveAddSize(&total_size, engine_size)
		|| !QCX_SaveAddSize(&total_size, 8U) || !QCX_SaveAddSize(&total_size, roster_size)
		|| !QCX_SaveAddSize(&total_size, 8U)
		|| !QCX_SaveAddSize(&total_size, image->guest_payload.size)
		|| total_size > QCX_SAVE_MAX_FILE_BYTES) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	bytes = malloc(total_size);
	if (bytes == NULL) return QCX_PLUGIN_IO_ERROR;
	writer = (qcx_save_writer_t){bytes, bytes + total_size};
	if (!QCX_SaveWrite(&writer, "QCMS", 4U) || !QCX_SaveWriteU32(&writer,
		QCX_SAVE_VERSION) || !QCX_SaveWriteU32(&writer, QCX_SAVE_METADATA_SECTION)
		|| !QCX_SaveWriteU32(&writer, metadata_size)
		|| !QCX_SaveWriteMetadata(&writer, &image->metadata, game_size, map_size)
		|| !QCX_SaveWriteU32(&writer, QCX_SAVE_ENGINE_SECTION)
		|| !QCX_SaveWriteU32(&writer, engine_size) || !QCX_SaveWriteEngine(&writer, image)
		|| !QCX_SaveWriteU32(&writer, QCX_SAVE_ROSTER_SECTION)
		|| !QCX_SaveWriteU32(&writer, roster_size) || !QCX_SaveWriteRoster(&writer, image)
		|| !QCX_SaveWriteU32(&writer, QCX_SAVE_GUEST_SECTION)
		|| !QCX_SaveWriteU32(&writer, image->guest_payload.size)
		|| !QCX_SaveWrite(&writer, image->guest_payload.data, image->guest_payload.size)
		|| writer.cursor != writer.end) {
		free(bytes);
		return QCX_PLUGIN_IO_ERROR;
	}
	*out = bytes;
	*size = total_size;
	return QCX_PLUGIN_OK;
}
