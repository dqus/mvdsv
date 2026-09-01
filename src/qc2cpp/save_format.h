#ifndef MVDSV_QC2CPP_SAVE_FORMAT_H
#define MVDSV_QC2CPP_SAVE_FORMAT_H

#include <game/host_types.h>

#include <stdint.h>

#define QC_SAVE_NAME_CAPACITY 64U
#define QC_SAVE_MAX_ENTITY_CAPACITY 2048U

typedef struct qc_save_bytes_s {
	uint8_t *data;
	uint32_t size;
} qc_save_bytes_t;

typedef struct qc_save_metadata_s {
	char logical_game[QC_SAVE_NAME_CAPACITY];
	char map_name[QC_SAVE_NAME_CAPACITY];
	uint32_t map_bsp_checksum;
	uint32_t entity_capacity;
	uint32_t contains_connected_clients;
} qc_save_metadata_t;

typedef struct qc_save_image {
	qc_save_metadata_t metadata;
	/* Engine section V1 is explicit little-endian server time, serverflags,
	 * lightstyles, precache order, edict active/free/freetime records, then
	 * client slots with gameplay flags and sixteen spawn parameters. */
	qc_save_bytes_t engine_state;
	qc_save_bytes_t guest_payload;
} qc_save_image_t;

qc_plugin_status_t QC_SaveParse(const uint8_t *bytes, uint32_t size,
	qc_save_image_t **out);
void QC_SaveImageFree(qc_save_image_t *image);
qc_plugin_status_t QC_SaveEncode(const qc_save_image_t *image, uint8_t **out,
	uint32_t *size);

#endif
