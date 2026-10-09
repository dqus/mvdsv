#ifndef MVDSV_QC2CPP_SAVE_FORMAT_H
#define MVDSV_QC2CPP_SAVE_FORMAT_H

#include <game/host_types.h>

#include <stdbool.h>
#include <stdint.h>

#define QCX_SAVE_NAME_CAPACITY 64U
#define QCX_SAVE_MAX_ENTITY_CAPACITY 2048U
#define QCX_SAVE_MAX_CLIENTS 32U
#define QCX_SAVE_CLIENT_STRING_CAPACITY 32U
#define QCX_SAVE_SPAWN_PARM_COUNT 16U
#define QCX_SAVE_LIGHTSTYLE_COUNT 64U
#define QCX_SAVE_MODEL_CAPACITY 4096U
#define QCX_SAVE_SOUND_CAPACITY 256U
#define QCX_SAVE_MAX_PRECACHES (QCX_SAVE_MODEL_CAPACITY + QCX_SAVE_SOUND_CAPACITY - 2U)
#define QCX_SAVE_MAX_RESOURCE_BYTES 255U
#define QCX_SAVE_MAX_GUEST_BYTES (8U * 1024U * 1024U)
#define QCX_SAVE_MAX_FILE_BYTES (12U * 1024U * 1024U)

typedef enum qcx_save_edict_state_e {
	QCX_SAVE_UNUSED_EDICT = 0,
	QCX_SAVE_ACTIVE_EDICT = 1,
	QCX_SAVE_FREE_EDICT = 2
} qcx_save_edict_state_t;

typedef struct qcx_save_edict_s {
	qcx_save_edict_state_t state;
	float freetime;
} qcx_save_edict_t;

typedef struct qcx_save_precache_s {
	char kind; /* 'M' (model) or 'S' (sound), at its saved nonzero index. */
	uint32_t slot;
	char name[QCX_SAVE_MAX_RESOURCE_BYTES + 1U];
} qcx_save_precache_t;

typedef struct qcx_save_engine_s {
	double time;
	uint32_t serverflags;
	char lightstyles[QCX_SAVE_LIGHTSTYLE_COUNT][QCX_SAVE_MAX_RESOURCE_BYTES + 1U];
	uint32_t precache_count;
	qcx_save_precache_t *precaches;
	qcx_save_edict_t edicts[QCX_SAVE_MAX_ENTITY_CAPACITY];
} qcx_save_engine_t;

typedef struct qcx_save_bytes_s {
	uint8_t *data;
	uint32_t size;
} qcx_save_bytes_t;

typedef struct qcx_save_metadata_s {
	char logical_game[QCX_SAVE_NAME_CAPACITY];
	char map_name[QCX_SAVE_NAME_CAPACITY];
	uint32_t map_bsp_checksum;
	uint32_t entity_capacity;
	uint32_t client_slot_capacity;
} qcx_save_metadata_t;

typedef enum qcx_save_role_e {
	QCX_SAVE_ROLE_PLAYER = 0,
	QCX_SAVE_ROLE_SPECTATOR = 1
} qcx_save_role_t;

typedef struct qcx_save_roster_entry_s {
	uint32_t saved_slot;
	uint32_t spawned;
	qcx_save_role_t role;
	char name[QCX_SAVE_CLIENT_STRING_CAPACITY];
	char team[QCX_SAVE_CLIENT_STRING_CAPACITY];
	float spawn_parms[QCX_SAVE_SPAWN_PARM_COUNT];
} qcx_save_roster_entry_t;

typedef struct qcx_save_image {
	qcx_save_metadata_t metadata;
	/* Parsed engine state owns its precache array; no opaque engine bytes remain. */
	qcx_save_engine_t engine;
	uint32_t roster_count;
	qcx_save_roster_entry_t roster[QCX_SAVE_MAX_CLIENTS];
	qcx_save_bytes_t guest_payload;
} qcx_save_image_t;

bool QCX_SaveClientNameEqual(const char *left, const char *right);
/* Also validates constructed images before encoding or restoring them. */
bool QCX_SaveImageValid(const qcx_save_image_t *image);
qcx_plugin_status_t QCX_SaveParse(const uint8_t *bytes, uint32_t size,
	qcx_save_image_t **out);
void QCX_SaveImageFree(qcx_save_image_t *image);
qcx_plugin_status_t QCX_SaveEncode(const qcx_save_image_t *image, uint8_t **out,
	uint32_t *size);

#endif
