#ifndef MVDSV_QC2CPP_RESTORE_ROSTER_H
#define MVDSV_QC2CPP_RESTORE_ROSTER_H

#include "qcx/save_format.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum qcx_restore_entry_state_e {
	QCX_RESTORE_ENTRY_AVAILABLE,
	QCX_RESTORE_ENTRY_BOUND,
	QCX_RESTORE_ENTRY_ACTIVE,
	QCX_RESTORE_ENTRY_ABANDONED
} qcx_restore_entry_state_t;

typedef struct qcx_restore_roster_entry_s {
	qcx_save_roster_entry_t saved;
	qcx_restore_entry_state_t state;
} qcx_restore_roster_entry_t;

typedef struct qcx_restore_roster_s {
	qcx_restore_roster_entry_t entries[QCX_SAVE_MAX_CLIENTS];
	uint32_t count;
	bool sealed;
} qcx_restore_roster_t;

bool QCX_RestoreRosterInit(qcx_restore_roster_t *out,
	const qcx_save_roster_entry_t *saved, uint32_t count);
int QCX_RestoreRosterFindAvailable(const qcx_restore_roster_t *roster,
	const char *canonical_name);
bool QCX_RestoreRosterBind(qcx_restore_roster_t *roster, uint32_t index);
bool QCX_RestoreRosterActivate(qcx_restore_roster_t *roster, uint32_t index);
bool QCX_RestoreRosterRelease(qcx_restore_roster_t *roster, uint32_t saved_slot);
bool QCX_RestoreRosterAllActive(const qcx_restore_roster_t *roster);
void QCX_RestoreRosterAbandonNonActive(qcx_restore_roster_t *roster);

#endif
