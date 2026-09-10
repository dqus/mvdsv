#include "qcx/restore_roster.h"

#include <string.h>

bool QCX_RestoreRosterInit(qcx_restore_roster_t *out,
	const qcx_save_roster_entry_t *saved, uint32_t count)
{
	uint32_t index;
	if (out == NULL || count > QCX_SAVE_MAX_CLIENTS
		|| (count != 0U && saved == NULL)) {
		return false;
	}
	memset(out, 0, sizeof(*out));
	for (index = 0U; index < count; ++index) {
		out->entries[index].saved = saved[index];
		out->entries[index].state = QCX_RESTORE_ENTRY_AVAILABLE;
	}
	out->count = count;
	return true;
}

int QCX_RestoreRosterFindAvailable(const qcx_restore_roster_t *roster,
	const char *canonical_name)
{
	uint32_t index;
	if (roster == NULL || canonical_name == NULL) return -1;
	for (index = 0U; index < roster->count; ++index) {
		const qcx_restore_roster_entry_t *const entry = &roster->entries[index];
		if (entry->state == QCX_RESTORE_ENTRY_AVAILABLE
			&& QCX_SaveClientNameEqual(entry->saved.name, canonical_name)) {
			return (int)index;
		}
	}
	return -1;
}

bool QCX_RestoreRosterBind(qcx_restore_roster_t *roster, uint32_t index)
{
	if (roster == NULL || roster->sealed || index >= roster->count
		|| roster->entries[index].state != QCX_RESTORE_ENTRY_AVAILABLE) {
		return false;
	}
	roster->entries[index].state = QCX_RESTORE_ENTRY_BOUND;
	return true;
}

bool QCX_RestoreRosterActivate(qcx_restore_roster_t *roster, uint32_t index)
{
	if (roster == NULL || roster->sealed || index >= roster->count
		|| roster->entries[index].state != QCX_RESTORE_ENTRY_BOUND) {
		return false;
	}
	roster->entries[index].state = QCX_RESTORE_ENTRY_ACTIVE;
	return true;
}

bool QCX_RestoreRosterRelease(qcx_restore_roster_t *roster, uint32_t saved_slot)
{
	uint32_t index;
	if (roster == NULL || roster->sealed) return false;
	for (index = 0U; index < roster->count; ++index) {
		qcx_restore_roster_entry_t *const entry = &roster->entries[index];
		if (entry->saved.saved_slot != saved_slot) continue;
		if (entry->state != QCX_RESTORE_ENTRY_BOUND
			&& entry->state != QCX_RESTORE_ENTRY_ACTIVE) {
			return false;
		}
		entry->state = QCX_RESTORE_ENTRY_AVAILABLE;
		return true;
	}
	return false;
}

bool QCX_RestoreRosterAllActive(const qcx_restore_roster_t *roster)
{
	uint32_t index;
	if (roster == NULL) return false;
	for (index = 0U; index < roster->count; ++index) {
		if (roster->entries[index].state != QCX_RESTORE_ENTRY_ACTIVE) return false;
	}
	return true;
}

void QCX_RestoreRosterAbandonNonActive(qcx_restore_roster_t *roster)
{
	uint32_t index;
	if (roster == NULL) return;
	for (index = 0U; index < roster->count; ++index) {
		if (roster->entries[index].state != QCX_RESTORE_ENTRY_ACTIVE) {
			roster->entries[index].state = QCX_RESTORE_ENTRY_ABANDONED;
		}
	}
	roster->sealed = true;
}
