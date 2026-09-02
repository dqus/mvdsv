#include "qcx/save_format.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum {
	QCX_SAVE_VERSION = 1,
	QCX_SAVE_METADATA_SECTION = 1,
	QCX_SAVE_ENGINE_SECTION = 2,
	QCX_SAVE_GUEST_SECTION = 3,
	QCX_SAVE_ENGINE_VERSION = 1,
	QCX_SAVE_MAX_LIGHTSTYLES = 64,
	QCX_SAVE_MAX_PRECACHES = 4096 + 256,
	QCX_SAVE_MAX_CLIENTS = 32,
	QCX_SAVE_MAX_RESOURCE_NAME = 255,
	QCX_SAVE_MAX_GUEST_BYTES = 8 * 1024 * 1024,
	QCX_SAVE_MAX_FILE_BYTES = 12 * 1024 * 1024,
	QCX_SAVE_CLIENT_CONNECTED = 1,
	QCX_SAVE_CLIENT_SPAWNED = 2,
	QCX_SAVE_CLIENT_SPECTATOR = 4
};

typedef struct qcx_save_reader_s {
	const uint8_t *cursor;
	const uint8_t *end;
} qcx_save_reader_t;

typedef struct qcx_save_writer_s {
	uint8_t *cursor;
	const uint8_t *end;
} qcx_save_writer_t;

typedef bool qbool;

static qbool QCX_SaveRead(qcx_save_reader_t *reader, void *out, uint32_t size)
{
	if ((size_t)(reader->end - reader->cursor) < size) {
		return false;
	}
	memcpy(out, reader->cursor, size);
	reader->cursor += size;
	return true;
}

static qbool QCX_SaveReadU32(qcx_save_reader_t *reader, uint32_t *out)
{
	uint8_t bytes[4];
	if (!QCX_SaveRead(reader, bytes, sizeof(bytes))) {
		return false;
	}
	*out = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
		| ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
	return true;
}

static qbool QCX_SaveReadU64(qcx_save_reader_t *reader, uint64_t *out)
{
	uint8_t bytes[8];
	uint32_t index;
	if (!QCX_SaveRead(reader, bytes, sizeof(bytes))) {
		return false;
	}
	*out = 0;
	for (index = 0; index < sizeof(bytes); ++index) {
		*out |= (uint64_t)bytes[index] << (8U * index);
	}
	return true;
}

static qbool QCX_SaveReadF32(qcx_save_reader_t *reader, float *out)
{
	uint32_t bits;
	if (!QCX_SaveReadU32(reader, &bits)) {
		return false;
	}
	memcpy(out, &bits, sizeof(bits));
	return true;
}

static qbool QCX_SaveReadF64(qcx_save_reader_t *reader, double *out)
{
	uint64_t bits;
	if (!QCX_SaveReadU64(reader, &bits)) {
		return false;
	}
	memcpy(out, &bits, sizeof(bits));
	return true;
}

static qbool QCX_SaveWrite(qcx_save_writer_t *writer, const void *bytes, uint32_t size)
{
	if ((size_t)(writer->end - writer->cursor) < size) {
		return false;
	}
	if (size == 0U) {
		return true;
	}
	memcpy(writer->cursor, bytes, size);
	writer->cursor += size;
	return true;
}

static qbool QCX_SaveWriteU32(qcx_save_writer_t *writer, uint32_t value)
{
	const uint8_t bytes[4] = {
		(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
		(uint8_t)(value >> 24)};
	return QCX_SaveWrite(writer, bytes, sizeof(bytes));
}

static qbool QCX_SaveWriteString(qcx_save_writer_t *writer, const char *value,
	uint32_t size)
{
	return QCX_SaveWriteU32(writer, size) && QCX_SaveWrite(writer, value, size);
}

static qbool QCX_SaveValidNameByte(uint8_t byte)
{
	return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
		|| (byte >= '0' && byte <= '9') || byte == '_' || byte == '-' || byte == '.';
}

static qbool QCX_SaveValidResourceByte(uint8_t byte)
{
	return byte >= 32U && byte <= 126U && byte != '\\';
}

static qbool QCX_SaveReadString(qcx_save_reader_t *reader, char *out,
	uint32_t out_size, qbool (*valid_byte)(uint8_t))
{
	uint32_t length;
	uint32_t index;
	if (!QCX_SaveReadU32(reader, &length) || length == 0U || length >= out_size
		|| (size_t)(reader->end - reader->cursor) < length) {
		return false;
	}
	for (index = 0; index < length; ++index) {
		if (!valid_byte(reader->cursor[index])) {
			return false;
		}
	}
	memcpy(out, reader->cursor, length);
	out[length] = '\0';
	reader->cursor += length;
	return true;
}

static qbool QCX_SaveReadResource(qcx_save_reader_t *reader)
{
	uint32_t length;
	uint32_t index;
	if (!QCX_SaveReadU32(reader, &length) || length == 0U
		|| length > QCX_SAVE_MAX_RESOURCE_NAME
		|| (size_t)(reader->end - reader->cursor) < length) {
		return false;
	}
	for (index = 0; index < length; ++index) {
		if (!QCX_SaveValidResourceByte(reader->cursor[index])) {
			return false;
		}
	}
	reader->cursor += length;
	return true;
}

static uint32_t QCX_SaveStringLength(const char *value, uint32_t capacity)
{
	uint32_t length;
	for (length = 0; length < capacity; ++length) {
		if (value[length] == '\0') {
			return length;
		}
	}
	return capacity;
}

static qbool QCX_SaveValidName(const char *value, uint32_t *size)
{
	uint32_t index;
	*size = QCX_SaveStringLength(value, QCX_SAVE_NAME_CAPACITY);
	if (*size == 0U || *size == QCX_SAVE_NAME_CAPACITY) {
		return false;
	}
	for (index = 0; index < *size; ++index) {
		if (!QCX_SaveValidNameByte((uint8_t)value[index])) {
			return false;
		}
	}
	return true;
}

static qbool QCX_SaveValidateMetadata(const qcx_save_metadata_t *metadata,
	uint32_t *game_size, uint32_t *map_size)
{
	return QCX_SaveValidName(metadata->logical_game, game_size)
		&& QCX_SaveValidName(metadata->map_name, map_size)
		&& metadata->entity_capacity > 0U
		&& metadata->entity_capacity <= QCX_SAVE_MAX_ENTITY_CAPACITY
		&& metadata->contains_connected_clients <= 1U;
}

static qbool QCX_SaveParseMetadata(const uint8_t *bytes, uint32_t size,
	qcx_save_metadata_t *metadata)
{
	qcx_save_reader_t reader = {bytes, bytes + size};
	if (!QCX_SaveReadString(&reader, metadata->logical_game,
		QCX_SAVE_NAME_CAPACITY, QCX_SaveValidNameByte)
		|| !QCX_SaveReadString(&reader, metadata->map_name,
		QCX_SAVE_NAME_CAPACITY, QCX_SaveValidNameByte)
		|| !QCX_SaveReadU32(&reader, &metadata->map_bsp_checksum)
		|| !QCX_SaveReadU32(&reader, &metadata->entity_capacity)
		|| !QCX_SaveReadU32(&reader, &metadata->contains_connected_clients)
		|| reader.cursor != reader.end) {
		return false;
	}
	return QCX_SaveValidateMetadata(metadata, &(uint32_t){0}, &(uint32_t){0});
}

static qbool QCX_SaveValidateEngine(const uint8_t *bytes, uint32_t size,
	const qcx_save_metadata_t *metadata)
{
	qcx_save_reader_t reader = {bytes, bytes + size};
	uint32_t version;
	uint32_t index;
	uint32_t count;
	uint32_t slot;
	uint32_t slots[QCX_SAVE_MAX_CLIENTS];
	qbool connected = false;
	double server_time;
	float value;

	if (!QCX_SaveReadU32(&reader, &version) || version != QCX_SAVE_ENGINE_VERSION
		|| !QCX_SaveReadF64(&reader, &server_time) || !isfinite(server_time)
		|| !QCX_SaveReadU32(&reader, &index) || !QCX_SaveReadU32(&reader, &count)
		|| count > QCX_SAVE_MAX_LIGHTSTYLES) {
		return false;
	}
	for (index = 0; index < count; ++index) {
		if (!QCX_SaveReadResource(&reader)) {
			return false;
		}
	}
	if (!QCX_SaveReadU32(&reader, &count) || count > QCX_SAVE_MAX_PRECACHES) {
		return false;
	}
	for (index = 0; index < count; ++index) {
		if (!QCX_SaveReadResource(&reader)) {
			return false;
		}
	}
	if (!QCX_SaveReadU32(&reader, &count) || count != metadata->entity_capacity) {
		return false;
	}
	for (index = 0; index < count; ++index) {
		if (!QCX_SaveReadU32(&reader, &slot) || slot > UINT32_C(2)
			|| !QCX_SaveReadF32(&reader, &value)
			|| !isfinite(value)) {
			return false;
		}
	}
	if (!QCX_SaveReadU32(&reader, &count) || count > QCX_SAVE_MAX_CLIENTS
		|| count > metadata->entity_capacity) {
		return false;
	}
	for (index = 0; index < count; ++index) {
		uint32_t prior;
		uint32_t flags;
		uint32_t parm;
		if (!QCX_SaveReadU32(&reader, &slot)
			|| slot >= metadata->entity_capacity - 1U
			|| !QCX_SaveReadU32(&reader, &flags)
			|| (flags & ~(QCX_SAVE_CLIENT_CONNECTED | QCX_SAVE_CLIENT_SPAWNED
				| QCX_SAVE_CLIENT_SPECTATOR)) != 0U) {
			return false;
		}
		for (prior = 0; prior < index; ++prior) {
			if (slots[prior] == slot) {
				return false;
			}
		}
		slots[index] = slot;
		connected = connected || (flags & QCX_SAVE_CLIENT_CONNECTED) != 0U;
		for (parm = 0; parm < 16U; ++parm) {
			if (!QCX_SaveReadF32(&reader, &value) || !isfinite(value)) {
				return false;
			}
		}
	}
	return connected == (metadata->contains_connected_clients != 0U)
		&& reader.cursor == reader.end;
}

static qbool QCX_SaveCopy(qcx_save_bytes_t *out, const uint8_t *bytes, uint32_t size)
{
	out->data = NULL;
	out->size = 0U;
	if (size == 0U) {
		return true;
	}
	out->data = malloc(size);
	if (out->data == NULL) {
		return false;
	}
	memcpy(out->data, bytes, size);
	out->size = size;
	return true;
}

void QCX_SaveImageFree(qcx_save_image_t *image)
{
	if (image == NULL) {
		return;
	}
	free(image->engine_state.data);
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
	if (out == NULL) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	*out = NULL;
	if (bytes == NULL || size < 8U || size > QCX_SAVE_MAX_FILE_BYTES
		|| memcmp(bytes, "QCMS", 4) != 0) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	reader.cursor = bytes + 4;
	reader.end = bytes + size;
	if (!QCX_SaveReadU32(&reader, &version) || version != QCX_SAVE_VERSION) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	image = calloc(1, sizeof(*image));
	if (image == NULL) {
		return QCX_PLUGIN_IO_ERROR;
	}
	for (section = QCX_SAVE_METADATA_SECTION; section <= QCX_SAVE_GUEST_SECTION; ++section) {
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
			if (!QCX_SaveValidateEngine(section_bytes, section_size, &image->metadata)) {
				goto bad_argument;
			}
			if (!QCX_SaveCopy(&image->engine_state, section_bytes, section_size)) {
				goto io_error;
			}
		} else {
			if (section_size > QCX_SAVE_MAX_GUEST_BYTES) {
				goto bad_argument;
			}
			if (!QCX_SaveCopy(&image->guest_payload, section_bytes, section_size)) {
				goto io_error;
			}
		}
	}
	if (reader.cursor != reader.end) {
		goto bad_argument;
	}
	*out = image;
	return QCX_PLUGIN_OK;

bad_argument:
	QCX_SaveImageFree(image);
	return QCX_PLUGIN_BAD_ARGUMENT;

io_error:
	QCX_SaveImageFree(image);
	return QCX_PLUGIN_IO_ERROR;
}

qcx_plugin_status_t QCX_SaveEncode(const qcx_save_image_t *image, uint8_t **out,
	uint32_t *size)
{
	qcx_save_writer_t writer;
	uint32_t game_size;
	uint32_t map_size;
	uint64_t metadata_size;
	uint64_t total_size;
	uint8_t *bytes;
	if (out == NULL || size == NULL) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	*out = NULL;
	*size = 0U;
	if (image == NULL || !QCX_SaveValidateMetadata(&image->metadata, &game_size, &map_size)
		|| image->engine_state.data == NULL || image->engine_state.size == 0U
		|| image->guest_payload.size > QCX_SAVE_MAX_GUEST_BYTES
		|| (image->guest_payload.size != 0U && image->guest_payload.data == NULL)
		|| !QCX_SaveValidateEngine(image->engine_state.data, image->engine_state.size,
			&image->metadata)) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	metadata_size = 4U + game_size + 4U + map_size + 12U;
	total_size = 8U + 8U + metadata_size + 8U + image->engine_state.size
		+ 8U + image->guest_payload.size;
	if (total_size > QCX_SAVE_MAX_FILE_BYTES || total_size > UINT32_MAX) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	bytes = malloc((size_t)total_size);
	if (bytes == NULL) {
		return QCX_PLUGIN_IO_ERROR;
	}
	writer.cursor = bytes;
	writer.end = bytes + total_size;
	if (!QCX_SaveWrite(&writer, "QCMS", 4) || !QCX_SaveWriteU32(&writer, QCX_SAVE_VERSION)
		|| !QCX_SaveWriteU32(&writer, QCX_SAVE_METADATA_SECTION)
		|| !QCX_SaveWriteU32(&writer, (uint32_t)metadata_size)
		|| !QCX_SaveWriteString(&writer, image->metadata.logical_game, game_size)
		|| !QCX_SaveWriteString(&writer, image->metadata.map_name, map_size)
		|| !QCX_SaveWriteU32(&writer, image->metadata.map_bsp_checksum)
		|| !QCX_SaveWriteU32(&writer, image->metadata.entity_capacity)
		|| !QCX_SaveWriteU32(&writer, image->metadata.contains_connected_clients)
		|| !QCX_SaveWriteU32(&writer, QCX_SAVE_ENGINE_SECTION)
		|| !QCX_SaveWriteU32(&writer, image->engine_state.size)
		|| !QCX_SaveWrite(&writer, image->engine_state.data, image->engine_state.size)
		|| !QCX_SaveWriteU32(&writer, QCX_SAVE_GUEST_SECTION)
		|| !QCX_SaveWriteU32(&writer, image->guest_payload.size)
		|| !QCX_SaveWrite(&writer, image->guest_payload.data, image->guest_payload.size)
		|| writer.cursor != writer.end) {
		free(bytes);
		return QCX_PLUGIN_IO_ERROR;
	}
	*out = bytes;
	*size = (uint32_t)total_size;
	return QCX_PLUGIN_OK;
}
