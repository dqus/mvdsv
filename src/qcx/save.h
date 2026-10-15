#ifndef MVDSV_QC2CPP_SAVE_H
#define MVDSV_QC2CPP_SAVE_H

#include "qwsvdef.h"
#include "qcx/save_format.h"

#include <game/shared_abi.h>

typedef enum qcx_restore_resource_policy_e {
	QCX_RESTORE_RESOURCES_EXACT,
	/* Fresh-map startup may add resources, but every saved index/name must match. */
	QCX_RESTORE_RESOURCES_ALLOW_EXTRA
} qcx_restore_resource_policy_t;

typedef struct qcx_restore_plan_s {
	const qcx_save_image_t *image;
	uint32_t num_edicts;
	uint8_t selection[(QCX_SAVE_MAX_ENTITY_CAPACITY + 7U) / 8U];
} qcx_restore_plan_t;

qbool QCX_SaveGame(const char *name);
qbool QCX_PrepareLoadGame(const char *name, char *map_name, uint32_t map_name_size);
qbool QCX_HasPreparedLoadGame(void);
/* After successful preflight and old-world teardown, before guest load/init:
 * apply saved mode cvars and install indexed resource names. */
qbool QCX_PrepareLoadStartup(void);
qbool QCX_CommitPreparedLoadGame(void);
void QCX_DiscardPreparedLoadGame(void);
/* Preparation leaves out_plan empty on failure and borrows image on success.
 * Keep that image alive and unchanged, and apply before any intervening world
 * change/gameplay. Preparing another plan does not invalidate this one. */
qcx_restore_status_t QCX_PrepareRestore(const qcx_save_image_t *image,
	qcx_restore_resource_policy_t resource_policy, qcx_restore_plan_t *out_plan);
/* Applies the prepared plan and clears it. Does not take ownership of image. */
void QCX_ApplyRestore(qcx_restore_plan_t *plan);
void QCX_RefreshClientReplication(void);

#endif
