#include "qcx/restore_roster.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static qcx_save_roster_entry_t make_entry(uint32_t saved_slot, const char *name)
{
	qcx_save_roster_entry_t entry = {0};
	entry.saved_slot = saved_slot;
	entry.spawned = 1U;
	entry.role = QCX_SAVE_ROLE_PLAYER;
	strcpy(entry.name, name);
	return entry;
}

static void test_zero_entries_are_already_complete(void)
{
	qcx_restore_roster_t roster = {0};
	assert(QCX_RestoreRosterInit(&roster, NULL, 0U));
	assert(roster.count == 0U);
	assert(QCX_RestoreRosterFindAvailable(&roster, "Alice") == -1);
	assert(QCX_RestoreRosterAllActive(&roster));
	assert(!QCX_RestoreRosterBind(&roster, 0U));
	assert(!QCX_RestoreRosterActivate(&roster, 0U));
	assert(!QCX_RestoreRosterRelease(&roster, 0U));
}

static void test_available_lookup_is_canonical_and_has_no_fallback(void)
{
	qcx_save_roster_entry_t saved[2] = {
		make_entry(3U, "Alice"), make_entry(7U, "Bob")};
	qcx_restore_roster_t roster = {0};
	const char colored_alice[] = {(char)0xc1, 'l', 'i', 'c', 'e', '\0'};
	assert(QCX_RestoreRosterInit(&roster, saved, 2U));
	saved[0].name[0] = 'Z';
	assert(strcmp(roster.entries[0].saved.name, "Alice") == 0);
	assert(QCX_RestoreRosterFindAvailable(&roster, "alice") == 0);
	assert(QCX_RestoreRosterFindAvailable(&roster, colored_alice) == 0);
	assert(QCX_RestoreRosterFindAvailable(&roster, "Bob") == 1);
	assert(QCX_RestoreRosterFindAvailable(&roster, "Charlie") == -1);
	assert(QCX_RestoreRosterBind(&roster, 0U));
	assert(QCX_RestoreRosterFindAvailable(&roster, "Alice") == -1);
}

static void test_bind_activate_and_release_follow_the_transition_table(void)
{
	qcx_save_roster_entry_t saved = make_entry(4U, "Alice");
	qcx_restore_roster_t roster = {0};
	assert(QCX_RestoreRosterInit(&roster, &saved, 1U));
	assert(!QCX_RestoreRosterActivate(&roster, 0U));
	assert(QCX_RestoreRosterBind(&roster, 0U));
	assert(!QCX_RestoreRosterBind(&roster, 0U));
	assert(QCX_RestoreRosterActivate(&roster, 0U));
	assert(QCX_RestoreRosterAllActive(&roster));
	assert(QCX_RestoreRosterRelease(&roster, 4U));
	assert(roster.entries[0].state == QCX_RESTORE_ENTRY_AVAILABLE);
	assert(!QCX_RestoreRosterAllActive(&roster));
	assert(QCX_RestoreRosterBind(&roster, 0U));
	assert(QCX_RestoreRosterRelease(&roster, 4U));
	assert(!QCX_RestoreRosterRelease(&roster, 4U));
	assert(!QCX_RestoreRosterBind(&roster, 1U));
	assert(!QCX_RestoreRosterActivate(&roster, 1U));
}

static void test_abandonment_preserves_active_entries_and_seals_the_rest(void)
{
	qcx_save_roster_entry_t saved[3] = {
		make_entry(1U, "Alice"), make_entry(2U, "Bob"), make_entry(3U, "Cara")};
	qcx_restore_roster_t roster = {0};
	assert(QCX_RestoreRosterInit(&roster, saved, 3U));
	assert(QCX_RestoreRosterBind(&roster, 0U));
	assert(QCX_RestoreRosterActivate(&roster, 0U));
	assert(QCX_RestoreRosterBind(&roster, 1U));
	QCX_RestoreRosterAbandonNonActive(&roster);
	assert(roster.entries[0].state == QCX_RESTORE_ENTRY_ACTIVE);
	assert(roster.entries[1].state == QCX_RESTORE_ENTRY_ABANDONED);
	assert(roster.entries[2].state == QCX_RESTORE_ENTRY_ABANDONED);
	assert(!QCX_RestoreRosterRelease(&roster, 1U));
	assert(roster.entries[0].state == QCX_RESTORE_ENTRY_ACTIVE);
	assert(QCX_RestoreRosterFindAvailable(&roster, "Alice") == -1);
	assert(QCX_RestoreRosterFindAvailable(&roster, "Bob") == -1);
	assert(!QCX_RestoreRosterRelease(&roster, 2U));
	assert(!QCX_RestoreRosterBind(&roster, 1U));
	assert(!QCX_RestoreRosterActivate(&roster, 1U));
	assert(!QCX_RestoreRosterAllActive(&roster));
}

static void test_init_rejects_invalid_bounds(void)
{
	qcx_save_roster_entry_t saved = make_entry(0U, "Alice");
	qcx_restore_roster_t roster = {0};
	assert(!QCX_RestoreRosterInit(NULL, &saved, 1U));
	assert(!QCX_RestoreRosterInit(&roster, NULL, 1U));
	assert(!QCX_RestoreRosterInit(&roster, &saved, QCX_SAVE_MAX_CLIENTS + 1U));
}

int main(void)
{
	test_zero_entries_are_already_complete();
	test_available_lookup_is_canonical_and_has_no_fallback();
	test_bind_activate_and_release_follow_the_transition_table();
	test_abandonment_preserves_active_entries_and_seals_the_rest();
	test_init_rejects_invalid_bounds();
	return 0;
}
