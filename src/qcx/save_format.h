#ifndef MVDSV_QC2CPP_SAVE_FORMAT_H
#define MVDSV_QC2CPP_SAVE_FORMAT_H

#include <game/host_types.h>

#include <stdint.h>

#define QCX_SAVE_NAME_CAPACITY 64U
#define QCX_SAVE_MAX_ENTITY_CAPACITY 2048U

typedef struct qcx_save_bytes_s {
	uint8_t *data;
	uint32_t size;
} qcx_save_bytes_t;

typedef struct qcx_save_metadata_s {
	char logical_game[QCX_SAVE_NAME_CAPACITY];
	char map_name[QCX_SAVE_NAME_CAPACITY];
	uint32_t map_bsp_checksum;
	uint32_t entity_capacity;
	uint32_t contains_connected_clients;
} qcx_save_metadata_t;

typedef struct qcx_save_image {
	qcx_save_metadata_t metadata;
	/* Engine section V1 is explicit little-endian server time, serverflags,
	 * lightstyles, precache order, edict active/free/freetime records, then
	 * client slots with gameplay flags and sixteen spawn parameters. */
	qcx_save_bytes_t engine_state;
	qcx_save_bytes_t guest_payload;
} qcx_save_image_t;

qcx_plugin_status_t QCX_SaveParse(const uint8_t *bytes, uint32_t size,
	qcx_save_image_t **out);
void QCX_SaveImageFree(qcx_save_image_t *image);
qcx_plugin_status_t QCX_SaveEncode(const qcx_save_image_t *image, uint8_t **out,
	uint32_t *size);

#endif
