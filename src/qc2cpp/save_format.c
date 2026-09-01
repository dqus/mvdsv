#include "qc2cpp/save_format.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum {
	QC_SAVE_VERSION = 1,
	QC_SAVE_METADATA_SECTION = 1,
	QC_SAVE_ENGINE_SECTION = 2,
	QC_SAVE_GUEST_SECTION = 3,
	QC_SAVE_ENGINE_VERSION = 1,
	QC_SAVE_MAX_LIGHTSTYLES = 64,
	QC_SAVE_MAX_PRECACHES = 4096,
	QC_SAVE_MAX_CLIENTS = 32,
	QC_SAVE_MAX_RESOURCE_NAME = 255,
	QC_SAVE_MAX_GUEST_BYTES = 8 * 1024 * 1024,
	QC_SAVE_MAX_FILE_BYTES = 12 * 1024 * 1024
};

typedef struct qc_save_reader_s {
	const uint8_t *cursor;
	const uint8_t *end;
} qc_save_reader_t;

typedef struct qc_save_writer_s {
	uint8_t *cursor;
	const uint8_t *end;
} qc_save_writer_t;

typedef bool qbool;

static qbool QC_SaveRead(qc_save_reader_t *reader, void *out, uint32_t size)
{
	if ((size_t)(reader->end - reader->cursor) < size) {
		return false;
	}
	memcpy(out, reader->cursor, size);
	reader->cursor += size;
	return true;
}

static qbool QC_SaveReadU32(qc_save_reader_t *reader, uint32_t *out)
{
	uint8_t bytes[4];
	if (!QC_SaveRead(reader, bytes, sizeof(bytes))) {
		return false;
	}
	*out = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
		| ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
	return true;
}

static qbool QC_SaveReadU64(qc_save_reader_t *reader, uint64_t *out)
{
	uint8_t bytes[8];
	uint32_t index;
	if (!QC_SaveRead(reader, bytes, sizeof(bytes))) {
		return false;
	}
	*out = 0;
	for (index = 0; index < sizeof(bytes); ++index) {
		*out |= (uint64_t)bytes[index] << (8U * index);
	}
	return true;
}

static qbool QC_SaveReadF32(qc_save_reader_t *reader, float *out)
{
	uint32_t bits;
	if (!QC_SaveReadU32(reader, &bits)) {
		return false;
	}
	memcpy(out, &bits, sizeof(bits));
	return true;
}

static qbool QC_SaveReadF64(qc_save_reader_t *reader, double *out)
{
	uint64_t bits;
	if (!QC_SaveReadU64(reader, &bits)) {
		return false;
	}
	memcpy(out, &bits, sizeof(bits));
	return true;
}

static qbool QC_SaveWrite(qc_save_writer_t *writer, const void *bytes, uint32_t size)
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

static qbool QC_SaveWriteU32(qc_save_writer_t *writer, uint32_t value)
{
	const uint8_t bytes[4] = {
		(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
		(uint8_t)(value >> 24)};
	return QC_SaveWrite(writer, bytes, sizeof(bytes));
}

static qbool QC_SaveWriteString(qc_save_writer_t *writer, const char *value,
	uint32_t size)
{
	return QC_SaveWriteU32(writer, size) && QC_SaveWrite(writer, value, size);
}

static qbool QC_SaveValidNameByte(uint8_t byte)
{
	return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
		|| (byte >= '0' && byte <= '9') || byte == '_' || byte == '-' || byte == '.';
}

static qbool QC_SaveValidResourceByte(uint8_t byte)
{
	return byte >= 32U && byte <= 126U && byte != '\\';
}

static qbool QC_SaveReadString(qc_save_reader_t *reader, char *out,
	uint32_t out_size, qbool (*valid_byte)(uint8_t))
{
	uint32_t length;
	uint32_t index;
	if (!QC_SaveReadU32(reader, &length) || length == 0U || length >= out_size
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

static qbool QC_SaveReadResource(qc_save_reader_t *reader)
{
	uint32_t length;
	uint32_t index;
	if (!QC_SaveReadU32(reader, &length) || length == 0U
		|| length > QC_SAVE_MAX_RESOURCE_NAME
		|| (size_t)(reader->end - reader->cursor) < length) {
		return false;
	}
	for (index = 0; index < length; ++index) {
		if (!QC_SaveValidResourceByte(reader->cursor[index])) {
			return false;
		}
	}
	reader->cursor += length;
	return true;
}

static uint32_t QC_SaveStringLength(const char *value, uint32_t capacity)
{
	uint32_t length;
	for (length = 0; length < capacity; ++length) {
		if (value[length] == '\0') {
			return length;
		}
	}
	return capacity;
}

static qbool QC_SaveValidName(const char *value, uint32_t *size)
{
	uint32_t index;
	*size = QC_SaveStringLength(value, QC_SAVE_NAME_CAPACITY);
	if (*size == 0U || *size == QC_SAVE_NAME_CAPACITY) {
		return false;
	}
	for (index = 0; index < *size; ++index) {
		if (!QC_SaveValidNameByte((uint8_t)value[index])) {
			return false;
		}
	}
	return true;
}

static qbool QC_SaveValidateMetadata(const qc_save_metadata_t *metadata,
	uint32_t *game_size, uint32_t *map_size)
{
	return QC_SaveValidName(metadata->logical_game, game_size)
		&& QC_SaveValidName(metadata->map_name, map_size)
		&& metadata->entity_capacity > 0U
		&& metadata->entity_capacity <= QC_SAVE_MAX_ENTITY_CAPACITY
		&& metadata->contains_connected_clients <= 1U;
}

static qbool QC_SaveParseMetadata(const uint8_t *bytes, uint32_t size,
	qc_save_metadata_t *metadata)
{
	qc_save_reader_t reader = {bytes, bytes + size};
	if (!QC_SaveReadString(&reader, metadata->logical_game,
		QC_SAVE_NAME_CAPACITY, QC_SaveValidNameByte)
		|| !QC_SaveReadString(&reader, metadata->map_name,
		QC_SAVE_NAME_CAPACITY, QC_SaveValidNameByte)
		|| !QC_SaveReadU32(&reader, &metadata->map_bsp_checksum)
		|| !QC_SaveReadU32(&reader, &metadata->entity_capacity)
		|| !QC_SaveReadU32(&reader, &metadata->contains_connected_clients)
		|| reader.cursor != reader.end) {
		return false;
	}
	return QC_SaveValidateMetadata(metadata, &(uint32_t){0}, &(uint32_t){0});
}

static qbool QC_SaveValidateEngine(const uint8_t *bytes, uint32_t size,
	const qc_save_metadata_t *metadata)
{
	qc_save_reader_t reader = {bytes, bytes + size};
	uint32_t version;
	uint32_t index;
	uint32_t count;
	uint32_t slot;
	uint32_t slots[QC_SAVE_MAX_CLIENTS];
	qbool connected = false;
	double server_time;
	float value;

	if (!QC_SaveReadU32(&reader, &version) || version != QC_SAVE_ENGINE_VERSION
		|| !QC_SaveReadF64(&reader, &server_time) || !isfinite(server_time)
		|| !QC_SaveReadU32(&reader, &index) || !QC_SaveReadU32(&reader, &count)
		|| count > QC_SAVE_MAX_LIGHTSTYLES) {
		return false;
	}
	for (index = 0; index < count; ++index) {
		if (!QC_SaveReadResource(&reader)) {
			return false;
		}
	}
	if (!QC_SaveReadU32(&reader, &count) || count > QC_SAVE_MAX_PRECACHES) {
		return false;
	}
	for (index = 0; index < count; ++index) {
		if (!QC_SaveReadResource(&reader)) {
			return false;
		}
	}
	if (!QC_SaveReadU32(&reader, &count) || count != metadata->entity_capacity) {
		return false;
	}
	for (index = 0; index < count; ++index) {
		if (!QC_SaveReadU32(&reader, &slot) || slot > UINT32_C(2)
			|| !QC_SaveReadF32(&reader, &value)
			|| !isfinite(value)) {
			return false;
		}
	}
	if (!QC_SaveReadU32(&reader, &count) || count > QC_SAVE_MAX_CLIENTS
		|| count > metadata->entity_capacity) {
		return false;
	}
	for (index = 0; index < count; ++index) {
		uint32_t prior;
		uint32_t flags;
		uint32_t parm;
		if (!QC_SaveReadU32(&reader, &slot) || slot >= metadata->entity_capacity
			|| !QC_SaveReadU32(&reader, &flags) || (flags & ~UINT32_C(1)) != 0U) {
			return false;
		}
		for (prior = 0; prior < index; ++prior) {
			if (slots[prior] == slot) {
				return false;
			}
		}
		slots[index] = slot;
		connected = connected || (flags & UINT32_C(1)) != 0U;
		for (parm = 0; parm < 16U; ++parm) {
			if (!QC_SaveReadF32(&reader, &value) || !isfinite(value)) {
				return false;
			}
		}
	}
	return connected == (metadata->contains_connected_clients != 0U)
		&& reader.cursor == reader.end;
}

static qbool QC_SaveCopy(qc_save_bytes_t *out, const uint8_t *bytes, uint32_t size)
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

void QC_SaveImageFree(qc_save_image_t *image)
{
	if (image == NULL) {
		return;
	}
	free(image->engine_state.data);
	free(image->guest_payload.data);
	free(image);
}

qc_plugin_status_t QC_SaveParse(const uint8_t *bytes, uint32_t size,
	qc_save_image_t **out)
{
	qc_save_reader_t reader;
	qc_save_image_t *image;
	uint32_t version;
	uint32_t section;
	if (out == NULL) {
		return QC_PLUGIN_BAD_ARGUMENT;
	}
	*out = NULL;
	if (bytes == NULL || size < 8U || size > QC_SAVE_MAX_FILE_BYTES
		|| memcmp(bytes, "QCMS", 4) != 0) {
		return QC_PLUGIN_BAD_ARGUMENT;
	}
	reader.cursor = bytes + 4;
	reader.end = bytes + size;
	if (!QC_SaveReadU32(&reader, &version) || version != QC_SAVE_VERSION) {
		return QC_PLUGIN_BAD_ARGUMENT;
	}
	image = calloc(1, sizeof(*image));
	if (image == NULL) {
		return QC_PLUGIN_IO_ERROR;
	}
	for (section = QC_SAVE_METADATA_SECTION; section <= QC_SAVE_GUEST_SECTION; ++section) {
		uint32_t id;
		uint32_t section_size;
		const uint8_t *section_bytes;
		if (!QC_SaveReadU32(&reader, &id) || id != section
			|| !QC_SaveReadU32(&reader, &section_size)
			|| (size_t)(reader.end - reader.cursor) < section_size) {
			goto bad_argument;
		}
		section_bytes = reader.cursor;
		reader.cursor += section_size;
		if (section == QC_SAVE_METADATA_SECTION) {
			if (!QC_SaveParseMetadata(section_bytes, section_size, &image->metadata)) {
				goto bad_argument;
			}
		} else if (section == QC_SAVE_ENGINE_SECTION) {
			if (!QC_SaveValidateEngine(section_bytes, section_size, &image->metadata)) {
				goto bad_argument;
			}
			if (!QC_SaveCopy(&image->engine_state, section_bytes, section_size)) {
				goto io_error;
			}
		} else {
			if (section_size > QC_SAVE_MAX_GUEST_BYTES) {
				goto bad_argument;
			}
			if (!QC_SaveCopy(&image->guest_payload, section_bytes, section_size)) {
				goto io_error;
			}
		}
	}
	if (reader.cursor != reader.end) {
		goto bad_argument;
	}
	*out = image;
	return QC_PLUGIN_OK;

bad_argument:
	QC_SaveImageFree(image);
	return QC_PLUGIN_BAD_ARGUMENT;

io_error:
	QC_SaveImageFree(image);
	return QC_PLUGIN_IO_ERROR;
}

qc_plugin_status_t QC_SaveEncode(const qc_save_image_t *image, uint8_t **out,
	uint32_t *size)
{
	qc_save_writer_t writer;
	uint32_t game_size;
	uint32_t map_size;
	uint64_t metadata_size;
	uint64_t total_size;
	uint8_t *bytes;
	if (out == NULL || size == NULL) {
		return QC_PLUGIN_BAD_ARGUMENT;
	}
	*out = NULL;
	*size = 0U;
	if (image == NULL || !QC_SaveValidateMetadata(&image->metadata, &game_size, &map_size)
		|| image->engine_state.data == NULL || image->engine_state.size == 0U
		|| image->guest_payload.size > QC_SAVE_MAX_GUEST_BYTES
		|| (image->guest_payload.size != 0U && image->guest_payload.data == NULL)
		|| !QC_SaveValidateEngine(image->engine_state.data, image->engine_state.size,
			&image->metadata)) {
		return QC_PLUGIN_BAD_ARGUMENT;
	}
	metadata_size = 4U + game_size + 4U + map_size + 12U;
	total_size = 8U + 8U + metadata_size + 8U + image->engine_state.size
		+ 8U + image->guest_payload.size;
	if (total_size > QC_SAVE_MAX_FILE_BYTES || total_size > UINT32_MAX) {
		return QC_PLUGIN_BAD_ARGUMENT;
	}
	bytes = malloc((size_t)total_size);
	if (bytes == NULL) {
		return QC_PLUGIN_IO_ERROR;
	}
	writer.cursor = bytes;
	writer.end = bytes + total_size;
	if (!QC_SaveWrite(&writer, "QCMS", 4) || !QC_SaveWriteU32(&writer, QC_SAVE_VERSION)
		|| !QC_SaveWriteU32(&writer, QC_SAVE_METADATA_SECTION)
		|| !QC_SaveWriteU32(&writer, (uint32_t)metadata_size)
		|| !QC_SaveWriteString(&writer, image->metadata.logical_game, game_size)
		|| !QC_SaveWriteString(&writer, image->metadata.map_name, map_size)
		|| !QC_SaveWriteU32(&writer, image->metadata.map_bsp_checksum)
		|| !QC_SaveWriteU32(&writer, image->metadata.entity_capacity)
		|| !QC_SaveWriteU32(&writer, image->metadata.contains_connected_clients)
		|| !QC_SaveWriteU32(&writer, QC_SAVE_ENGINE_SECTION)
		|| !QC_SaveWriteU32(&writer, image->engine_state.size)
		|| !QC_SaveWrite(&writer, image->engine_state.data, image->engine_state.size)
		|| !QC_SaveWriteU32(&writer, QC_SAVE_GUEST_SECTION)
		|| !QC_SaveWriteU32(&writer, image->guest_payload.size)
		|| !QC_SaveWrite(&writer, image->guest_payload.data, image->guest_payload.size)
		|| writer.cursor != writer.end) {
		free(bytes);
		return QC_PLUGIN_IO_ERROR;
	}
	*out = bytes;
	*size = (uint32_t)total_size;
	return QC_PLUGIN_OK;
}
