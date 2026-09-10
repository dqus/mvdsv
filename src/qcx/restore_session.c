#include "qcx/restore_session.h"

#include "qcx/restore_roster.h"
#include "qcx/save.h"
#if defined(QCX_TESTS)
#include "qcx/test_observer.h"
#endif

#include <string.h>

typedef enum qcx_restore_session_state_e {
	QCX_RESTORE_SESSION_INACTIVE,
	QCX_RESTORE_SESSION_WAITING,
	QCX_RESTORE_SESSION_COMPLETE
} qcx_restore_session_state_t;

typedef struct qcx_restore_session_s {
	qcx_restore_session_state_t state;
	qcx_restore_roster_t roster;
	uint32_t slot_capacity;
	qbool dirty;
	double deadline;
	qbool continue_requested;
	qbool initial_handshake_pending;
} qcx_restore_session_t;

static qcx_restore_session_t qcx_restore_session;
cvar_t qcx_restore_wait_timeout = {"qcx_restore_wait_timeout", "60", CVAR_NONE, NULL,
	0.0f, NULL, NULL};

static void QCX_RestoreSessionReset(qbool notify_clients);

extern void MVD_PlayerReset(int player);

#if defined(QCX_RESTORE_SESSION_TEST)
extern void QCX_RestoreSessionTestVisitSlot(void);
#define QCX_RESTORE_SESSION_VISIT_SLOT() QCX_RestoreSessionTestVisitSlot()
#else
#define QCX_RESTORE_SESSION_VISIT_SLOT() ((void)0)
#endif

static qbool QCX_RestoreSessionClientIsLive(const client_t *client)
{
	return client->state == cs_preconnected || client->state == cs_connected
		|| client->state == cs_spawned;
}

static void QCX_RestoreSessionClearClientFlags(client_t *client)
{
	client->qcx_restore_waiting = false;
	client->qcx_restore_pending = false;
	client->qcx_restore_roster_index = -1;
}

static void QCX_RestoreSessionClearClientOriginalIdentity(client_t *client)
{
	client->qcx_restore_has_original_identity = false;
	client->qcx_restore_original_spectator = false;
	client->qcx_restore_original_team[0] = '\0';
}

static void QCX_RestoreSessionRepairClient(client_t *client, uint32_t slot)
{
	client->netchan.message.data = client->netchan.message_buf;
	client->datagram.data = client->datagram_buf;
	if (client->num_backbuf > 0 && client->num_backbuf <= MAX_BACK_BUFFERS) {
		client->backbuf.data = client->backbuf_data[client->num_backbuf - 1];
	} else {
		client->backbuf.data = client->backbuf_data[0];
	}
	client->edict = &sv.edicts[slot + 1U];
}

static void QCX_RestoreSessionClearMovementCaches(client_t *client)
{
	client->delta_sequence = -1;
	client->antilag_position_next = 0;
	memset(client->antilag_positions, 0, sizeof(client->antilag_positions));
	client->laggedents_count = 0U;
	client->laggedents_frac = 0.0f;
	client->laggedents_time = 0.0f;
	memset(client->laggedents, 0, sizeof(client->laggedents));
}

static qcx_restore_roster_entry_t *QCX_RestoreSessionClientEntry(client_t *client)
{
	const int index = client == NULL ? -1 : client->qcx_restore_roster_index;
	qcx_restore_roster_entry_t *entry;
	if (!QCX_RestoreSessionWaiting() || index < 0
		|| (uint32_t)index >= qcx_restore_session.roster.count) {
		return NULL;
	}
	entry = &qcx_restore_session.roster.entries[index];
	if (entry->saved.saved_slot != (uint32_t)(client - svs.clients)) return NULL;
	return entry;
}

static void QCX_RestoreSessionApplySavedIdentity(client_t *client,
	const qcx_save_roster_entry_t *saved)
{
	const qbool spectator = saved->role == QCX_SAVE_ROLE_SPECTATOR;
	if (!client->qcx_restore_has_original_identity) {
		client->qcx_restore_has_original_identity = true;
		client->qcx_restore_original_spectator = client->spectator != 0;
		strlcpy(client->qcx_restore_original_team, client->team,
			sizeof(client->qcx_restore_original_team));
	}
	client->spectator = spectator;
	if (spectator) {
		(void)Info_SetStar(&client->_userinfo_ctx_, "*spectator", "1");
		(void)Info_SetStar(&client->_userinfoshort_ctx_, "*spectator", "1");
	} else {
		(void)Info_Remove(&client->_userinfo_ctx_, "*spectator");
		(void)Info_Remove(&client->_userinfoshort_ctx_, "*spectator");
	}
	if (saved->team[0] == '\0') {
		(void)Info_Remove(&client->_userinfo_ctx_, "team");
		(void)Info_Remove(&client->_userinfoshort_ctx_, "team");
	} else {
		(void)Info_Set(&client->_userinfo_ctx_, "team", saved->team);
		(void)Info_Set(&client->_userinfoshort_ctx_, "team", saved->team);
	}
	strlcpy(client->team, saved->team, sizeof(client->team));
	memcpy(client->spawn_parms, saved->spawn_parms, sizeof(client->spawn_parms));
	SV_ClientPrintf(client, PRINT_HIGH, "Restoring saved identity as %s%s%s.\n",
		spectator ? "spectator" : "player", saved->team[0] == '\0' ? "" : " on team ",
		saved->team[0] == '\0' ? "" : saved->team);
}

static void QCX_RestoreSessionQueueRestoredNew(client_t *client)
{
	/* The usual map path normally does this in SV_SaveSpawnparms.  Keep the
	 * requirement local to the restore handoff as well, so Cmd_New_f cannot
	 * discard the client's new signon while it is still marked spawned. */
	if (client->state == cs_spawned) client->state = cs_connected;
	SV_ClearReliable(client);
	/* An active QW client must first leave its old signon state.  FTE (and
	 * ordinary QW clients) implements this pair as a same-netchannel fresh
	 * handshake: changing makes it connected, reconnect sends `new` without a
	 * new direct-connect or ClientConnect callback. */
	ClientReliableWrite_Begin(client, svc_stufftext, 20);
	ClientReliableWrite_String(client, "changing\nreconnect\n");
	client->send_message = true;
}

static void QCX_RestoreSessionQueueWaitingNew(client_t *client)
{
	/* A newcomer in the restore list was deliberately held before serverdata,
	 * so starting its normal QW signon is now a server-side action.  This avoids
	 * depending on an extra client command after its name update; it is the same
	 * Cmd_New_f path an ordinary `new` request reaches.  The roster-list reply
	 * may still occupy the one reliable retransmit slot, so discard that
	 * deliberately obsolete pre-serverdata stream before queueing serverdata. */
	SV_ClearReliable(client);
	client->netchan.reliable_length = 0;
	client->netchan.last_reliable_sequence = client->netchan.outgoing_sequence;
	SV_QCXStartClientSignon(client);
	client->send_message = true;
}

static void QCX_RestoreSessionRestoreOriginalIdentity(client_t *client)
{
	if (!client->qcx_restore_has_original_identity) return;
	client->spectator = client->qcx_restore_original_spectator;
	if (client->spectator) {
		(void)Info_SetStar(&client->_userinfo_ctx_, "*spectator", "1");
		(void)Info_SetStar(&client->_userinfoshort_ctx_, "*spectator", "1");
	} else {
		(void)Info_Remove(&client->_userinfo_ctx_, "*spectator");
		(void)Info_Remove(&client->_userinfoshort_ctx_, "*spectator");
	}
	if (client->qcx_restore_original_team[0] == '\0') {
		(void)Info_Remove(&client->_userinfo_ctx_, "team");
		(void)Info_Remove(&client->_userinfoshort_ctx_, "team");
	} else {
		(void)Info_Set(&client->_userinfo_ctx_, "team",
			client->qcx_restore_original_team);
		(void)Info_Set(&client->_userinfoshort_ctx_, "team",
			client->qcx_restore_original_team);
	}
	strlcpy(client->team, client->qcx_restore_original_team, sizeof(client->team));
	QCX_RestoreSessionClearClientOriginalIdentity(client);
}

static void QCX_RestoreSessionSwapClients(uint32_t left_slot, uint32_t right_slot)
{
	client_t *const left = &svs.clients[left_slot];
	client_t *const right = &svs.clients[right_slot];
	client_t temporary;
	edict_t *const left_edict = left->edict;
	edict_t *const right_edict = right->edict;

	if (left_slot == right_slot) return;
	memcpy(&temporary, left, sizeof(temporary));
	memcpy(left, right, sizeof(*left));
	memcpy(right, &temporary, sizeof(*right));
	QCX_RestoreSessionRepairClient(left, left_slot);
	QCX_RestoreSessionRepairClient(right, right_slot);
	QCX_RestoreSessionClearMovementCaches(left);
	QCX_RestoreSessionClearMovementCaches(right);
	MVD_PlayerReset((int)left_slot);
	MVD_PlayerReset((int)right_slot);

	if (sv_client == left) {
		sv_client = right;
	} else if (sv_client == right) {
		sv_client = left;
	}
	if (WatcherId == left) {
		WatcherId = right;
	} else if (WatcherId == right) {
		WatcherId = left;
	}
#if defined(WWW_INTEGRATION)
	Central_SwapClientPointers(left, right);
#endif
	if (sv_player == left_edict) {
		sv_player = right->edict;
	} else if (sv_player == right_edict) {
		sv_player = left->edict;
	}
}

static int QCX_RestoreSessionFindFreeUnreservedSlot(void)
{
	uint32_t slot;
	for (slot = 0U; slot < qcx_restore_session.slot_capacity; ++slot) {
		QCX_RESTORE_SESSION_VISIT_SLOT();
		if (svs.clients[slot].state == cs_free
			&& !QCX_RestoreSessionSlotReserved(slot)) {
			return (int)slot;
		}
	}
	return -1;
}

static int QCX_RestoreSessionFindClientForRoster(uint32_t roster_index)
{
	uint32_t slot;
	for (slot = 0U; slot < qcx_restore_session.slot_capacity; ++slot) {
		QCX_RESTORE_SESSION_VISIT_SLOT();
		const client_t *const client = &svs.clients[slot];
		if (QCX_RestoreSessionClientIsLive(client)
			&& client->qcx_restore_pending
			&& client->qcx_restore_roster_index == (int)roster_index) {
			return (int)slot;
		}
	}
	return -1;
}

static int QCX_RestoreSessionFindLowestNamedClient(const char *name)
{
	uint32_t slot;
	for (slot = 0U; slot < qcx_restore_session.slot_capacity; ++slot) {
		QCX_RESTORE_SESSION_VISIT_SLOT();
		const client_t *const client = &svs.clients[slot];
		if (!QCX_RestoreSessionClientIsLive(client)
			|| client->qcx_restore_roster_index >= 0) {
			continue;
		}
		if (Q_namecmp(client->name, name) == 0) return (int)slot;
	}
	return -1;
}

static int QCX_RestoreSessionFindHighestUnmatchedUnreservedClient(void)
{
	uint32_t slot;
	for (slot = qcx_restore_session.slot_capacity; slot > 0U; --slot) {
		const uint32_t candidate = slot - 1U;
		const client_t *const client = &svs.clients[candidate];
		QCX_RESTORE_SESSION_VISIT_SLOT();
		if (QCX_RestoreSessionClientIsLive(client)
			&& client->qcx_restore_roster_index < 0
			&& !QCX_RestoreSessionSlotReserved(candidate)) {
			return (int)candidate;
		}
	}
	return -1;
}

static void QCX_RestoreSessionDropClient(uint32_t slot)
{
	client_t *const client = &svs.clients[slot];
	SV_DropClient(client);
	client->state = cs_free;
	QCX_RestoreSessionClearClientFlags(client);
	QCX_RestoreSessionRepairClient(client, slot);
	QCX_RestoreSessionClearMovementCaches(client);
	MVD_PlayerReset((int)slot);
}

static void QCX_RestoreSessionEvacuateUnmatchedReservations(void)
{
	uint32_t slot;
	for (slot = 0U; slot < qcx_restore_session.slot_capacity; ++slot) {
		int destination;
		QCX_RESTORE_SESSION_VISIT_SLOT();
		if (!QCX_RestoreSessionSlotReserved(slot)
			|| !QCX_RestoreSessionClientIsLive(&svs.clients[slot])
			|| svs.clients[slot].qcx_restore_roster_index >= 0) {
			continue;
		}
		destination = QCX_RestoreSessionFindFreeUnreservedSlot();
		if (destination < 0) {
			destination = QCX_RestoreSessionFindHighestUnmatchedUnreservedClient();
			if (destination >= 0) {
				QCX_RestoreSessionDropClient((uint32_t)destination);
			}
		}
		if (destination < 0) {
			QCX_RestoreSessionDropClient(slot);
			continue;
		}
		QCX_RestoreSessionSwapClients(slot, (uint32_t)destination);
	}
}

static qbool QCX_RestoreSessionImageIsValid(const qcx_save_image_t *image)
{
	uint32_t left;
	uint32_t right;
	if (image == NULL || image->metadata.client_slot_capacity == 0U
		|| image->metadata.client_slot_capacity > MAX_CLIENTS
		|| image->metadata.entity_capacity <= image->metadata.client_slot_capacity
		|| image->roster_count > QCX_SAVE_MAX_CLIENTS) {
		return false;
	}
	for (left = 0U; left < image->roster_count; ++left) {
		if (image->roster[left].saved_slot >= image->metadata.client_slot_capacity) {
			return false;
		}
		for (right = 0U; right < left; ++right) {
			if (image->roster[right].saved_slot == image->roster[left].saved_slot) {
				return false;
			}
		}
	}
	return true;
}

qbool QCX_RestoreSessionInstall(const qcx_save_image_t *image, double monotonic_now)
{
	qcx_restore_roster_t roster;
	uint32_t slot;
	float timeout;
	(void)monotonic_now;
	if (!QCX_RestoreSessionImageIsValid(image)
		|| !QCX_RestoreRosterInit(&roster, image->roster, image->roster_count)) {
		return false;
	}
	QCX_RestoreSessionReset(false);
	qcx_restore_session.roster = roster;
	qcx_restore_session.slot_capacity = image->metadata.client_slot_capacity;
	qcx_restore_session.state = image->roster_count == 0U
		? QCX_RESTORE_SESSION_COMPLETE : QCX_RESTORE_SESSION_WAITING;
	qcx_restore_session.dirty = image->roster_count != 0U;
	timeout = qcx_restore_wait_timeout.value;
	if (timeout < 0.0f) {
		Cvar_SetValue(&qcx_restore_wait_timeout, 0.0f);
		timeout = 0.0f;
	}
	qcx_restore_session.deadline = timeout > 0.0f ? monotonic_now + timeout : 0.0;
	qcx_restore_session.initial_handshake_pending = image->roster_count != 0U;
	for (slot = 0U; slot < qcx_restore_session.slot_capacity; ++slot) {
		QCX_RESTORE_SESSION_VISIT_SLOT();
		QCX_RestoreSessionClearClientFlags(&svs.clients[slot]);
		QCX_RestoreSessionClearClientOriginalIdentity(&svs.clients[slot]);
		if (image->roster_count != 0U
			&& QCX_RestoreSessionClientIsLive(&svs.clients[slot])) {
			svs.clients[slot].qcx_restore_waiting = true;
		}
	}
	SV_SetPauseReason(SV_PAUSE_RESTORE, image->roster_count != 0U, NULL, false);
	return true;
}

static void QCX_RestoreSessionContinueCommand(void)
{
	if (!QCX_RestoreSessionWaiting()) {
		Con_Printf("No QCX restore session is waiting.\n");
		return;
	}
	qcx_restore_session.continue_requested = true;
}

void QCX_RestoreSessionInit(void)
{
	Cvar_Register(&qcx_restore_wait_timeout);
	Cmd_AddCommand("qcx_restore_continue", QCX_RestoreSessionContinueCommand);
}

void QCX_RestoreSessionContinue(void)
{
	if (QCX_RestoreSessionWaiting()) qcx_restore_session.continue_requested = true;
}

static void QCX_RestoreSessionReset(qbool notify_clients)
{
	uint32_t slot;
	for (slot = 0U; slot < qcx_restore_session.slot_capacity; ++slot) {
		client_t *const client = &svs.clients[slot];
		QCX_RESTORE_SESSION_VISIT_SLOT();
		/* A bound identity has not reached Cmd_Begin yet.  A replacement map
		 * must not carry its saved spectator/team choice into an unrelated
		 * session.  Active identities, on the other hand, have completed their
		 * restore and keep the saved choice across an ordinary map change. */
		if (client->qcx_restore_pending
			&& QCX_RestoreSessionClientIsLive(client)) {
			QCX_RestoreSessionRestoreOriginalIdentity(client);
		}
		QCX_RestoreSessionClearClientFlags(client);
		QCX_RestoreSessionClearClientOriginalIdentity(client);
	}
	memset(&qcx_restore_session, 0, sizeof(qcx_restore_session));
	SV_SetPauseReason(SV_PAUSE_RESTORE, false, NULL, notify_clients);
}

void QCX_RestoreSessionCancel(void)
{
	QCX_RestoreSessionReset(true);
}

qbool QCX_RestoreSessionBlocksSave(void)
{
	return qcx_restore_session.state == QCX_RESTORE_SESSION_WAITING;
}

qbool QCX_RestoreSessionWaiting(void)
{
	return qcx_restore_session.state == QCX_RESTORE_SESSION_WAITING;
}

void QCX_RestoreSessionGetStatus(qcx_restore_session_status_t *out,
	double monotonic_now)
{
	uint32_t index;
	if (out == NULL) return;
	memset(out, 0, sizeof(*out));
	out->waiting = QCX_RestoreSessionWaiting();
	if (!out->waiting) return;
	if (qcx_restore_session.deadline > monotonic_now) {
		out->remaining_seconds = qcx_restore_session.deadline - monotonic_now;
	}
	for (index = 0U; index < qcx_restore_session.roster.count; ++index) {
		switch (qcx_restore_session.roster.entries[index].state) {
		case QCX_RESTORE_ENTRY_AVAILABLE:
			++out->available_count;
			break;
		case QCX_RESTORE_ENTRY_BOUND:
			++out->bound_count;
			break;
		case QCX_RESTORE_ENTRY_ACTIVE:
			++out->active_count;
			break;
		case QCX_RESTORE_ENTRY_ABANDONED:
			break;
		}
	}
}

qbool QCX_RestoreSessionSlotReserved(uint32_t slot)
{
	uint32_t index;
	if (!QCX_RestoreSessionWaiting() || slot >= qcx_restore_session.slot_capacity) {
		return false;
	}
	for (index = 0U; index < qcx_restore_session.roster.count; ++index) {
		const qcx_restore_roster_entry_t *const entry =
			&qcx_restore_session.roster.entries[index];
		if (entry->saved.saved_slot == slot
			&& entry->state != QCX_RESTORE_ENTRY_ABANDONED) {
			return true;
		}
	}
	return false;
}

static int QCX_RestoreSessionFindAdmissionEntry(const char *raw_name)
{
	uint32_t index;
	if (!QCX_RestoreSessionWaiting() || raw_name == NULL) return -1;
	for (index = 0U; index < qcx_restore_session.roster.count; ++index) {
		const qcx_restore_roster_entry_t *const entry =
			&qcx_restore_session.roster.entries[index];
		if (entry->state == QCX_RESTORE_ENTRY_AVAILABLE
			&& Q_namecmp(entry->saved.name, raw_name) == 0
			&& svs.clients[entry->saved.saved_slot].state == cs_free) {
			return (int)index;
		}
	}
	return -1;
}

qbool QCX_RestoreSessionAdmissionRole(const char *raw_name, qbool *spectator)
{
	const int index = QCX_RestoreSessionFindAdmissionEntry(raw_name);
	if (index < 0 || spectator == NULL) return false;
	*spectator = qcx_restore_session.roster.entries[index].saved.role
		== QCX_SAVE_ROLE_SPECTATOR;
	return true;
}

client_t *QCX_RestoreSessionAdmissionSlot(const char *raw_name)
{
	const int index = QCX_RestoreSessionFindAdmissionEntry(raw_name);
	int slot;
	if (!QCX_RestoreSessionWaiting() || raw_name == NULL) return NULL;
	if (index >= 0) return &svs.clients[qcx_restore_session.roster.entries[index].saved.saved_slot];
	slot = QCX_RestoreSessionFindFreeUnreservedSlot();
	return slot < 0 ? NULL : &svs.clients[slot];
}

void QCX_RestoreSessionObserveClient(client_t *client)
{
	(void)client;
	if (QCX_RestoreSessionWaiting()) qcx_restore_session.dirty = true;
}

void QCX_RestoreSessionNameChanged(client_t *client)
{
	(void)client;
	if (QCX_RestoreSessionWaiting()) qcx_restore_session.dirty = true;
}

static void QCX_RestoreSessionFinish(qbool abandon)
{
	uint32_t index;
	uint32_t slot;
	client_t *const saved_client = sv_client;
	edict_t *const saved_player = sv_player;
	if (abandon) {
		for (index = 0U; index < qcx_restore_session.roster.count; ++index) {
			const qcx_restore_roster_entry_t *const entry =
				&qcx_restore_session.roster.entries[index];
			edict_t *const edict = &sv.edicts[entry->saved.saved_slot + 1U];
			if (entry->state == QCX_RESTORE_ENTRY_ACTIVE) continue;
			sv_client = &svs.clients[entry->saved.saved_slot];
			sv_player = edict;
			if (entry->saved.spawned != 0U) {
				PR_GameClientDisconnect(entry->saved.role == QCX_SAVE_ROLE_SPECTATOR);
			}
			ED_Free(edict);
		}
		QCX_RestoreRosterAbandonNonActive(&qcx_restore_session.roster);
	}
	for (slot = 0U; slot < qcx_restore_session.slot_capacity; ++slot) {
		client_t *const client = &svs.clients[slot];
		if (!QCX_RestoreSessionClientIsLive(client)) {
			QCX_RestoreSessionClearClientFlags(client);
			QCX_RestoreSessionClearClientOriginalIdentity(client);
			continue;
		}
		if (client->qcx_restore_pending) {
			QCX_RestoreSessionRestoreOriginalIdentity(client);
			QCX_RestoreSessionClearClientFlags(client);
			QCX_RestoreSessionQueueRestoredNew(client);
		} else if (client->qcx_restore_waiting) {
			QCX_RestoreSessionClearClientFlags(client);
			QCX_RestoreSessionQueueWaitingNew(client);
		} else {
			QCX_RestoreSessionClearClientFlags(client);
			QCX_RestoreSessionClearClientOriginalIdentity(client);
		}
	}
	sv_client = saved_client;
	sv_player = saved_player;
	memset(&qcx_restore_session.roster, 0, sizeof(qcx_restore_session.roster));
	qcx_restore_session.deadline = 0.0;
	qcx_restore_session.continue_requested = false;
	qcx_restore_session.initial_handshake_pending = false;
	qcx_restore_session.dirty = false;
	qcx_restore_session.state = QCX_RESTORE_SESSION_COMPLETE;
	SV_SetPauseReason(SV_PAUSE_RESTORE, false, NULL, true);
	if (!abandon) {
#if defined(QCX_TESTS)
		QCX_TestObserverRestoreReplicationBegin();
#endif
		QCX_RefreshClientReplication();
#if defined(QCX_TESTS)
		QCX_TestObserverRestoreReplicationComplete();
#endif
	}
}

void QCX_RestoreSessionFrame(double monotonic_now)
{
	uint32_t index;
	uint32_t slot;
	qbool was_waiting[MAX_CLIENTS];
	qbool queue_waiting_new[QCX_SAVE_MAX_CLIENTS];
	(void)monotonic_now;
	if (!QCX_RestoreSessionWaiting()) return;
	if (QCX_RestoreRosterAllActive(&qcx_restore_session.roster)) {
		QCX_RestoreSessionFinish(false);
		return;
	}
	if (qcx_restore_session.continue_requested
		|| (qcx_restore_session.deadline > 0.0
			&& monotonic_now >= qcx_restore_session.deadline)) {
		QCX_RestoreSessionFinish(true);
		return;
	}
	if (!qcx_restore_session.dirty) return;
	qcx_restore_session.dirty = false;
	memset(queue_waiting_new, 0, sizeof(queue_waiting_new));

	for (index = 0U; index < qcx_restore_session.roster.count; ++index) {
		qcx_restore_roster_entry_t *const entry = &qcx_restore_session.roster.entries[index];
		if (entry->state == QCX_RESTORE_ENTRY_BOUND) {
			(void)QCX_RestoreRosterRelease(&qcx_restore_session.roster,
				entry->saved.saved_slot);
		}
	}
	if (QCX_RestoreRosterAllActive(&qcx_restore_session.roster)) {
		QCX_RestoreSessionFinish(false);
		return;
	}
	for (slot = 0U; slot < qcx_restore_session.slot_capacity; ++slot) {
		QCX_RESTORE_SESSION_VISIT_SLOT();
		was_waiting[slot] = svs.clients[slot].qcx_restore_waiting;
		QCX_RestoreSessionClearClientFlags(&svs.clients[slot]);
	}
	for (index = 0U; index < qcx_restore_session.roster.count; ++index) {
		const qcx_restore_roster_entry_t *const entry =
			&qcx_restore_session.roster.entries[index];
		client_t *const client = &svs.clients[entry->saved.saved_slot];
		if (entry->state == QCX_RESTORE_ENTRY_ACTIVE
			&& QCX_RestoreSessionClientIsLive(client)) {
			client->qcx_restore_roster_index = (int)index;
		}
	}
	for (index = 0U; index < qcx_restore_session.roster.count; ++index) {
		const int client_slot = QCX_RestoreSessionFindLowestNamedClient(
			qcx_restore_session.roster.entries[index].saved.name);
		if (client_slot >= 0
			&& QCX_RestoreRosterBind(&qcx_restore_session.roster, index)) {
			client_t *const client = &svs.clients[client_slot];
			client->qcx_restore_pending = true;
			client->qcx_restore_roster_index = (int)index;
			QCX_RestoreSessionApplySavedIdentity(client,
				&qcx_restore_session.roster.entries[index].saved);
			if (was_waiting[client_slot]
				&& !qcx_restore_session.initial_handshake_pending) {
				/* The binding is about to move the client object to its saved
				 * slot.  Defer queuing the fresh `new` request until that
				 * permutation has completed, so the reliable message stays with
				 * the netchannel that will receive serverdata. */
				queue_waiting_new[index] = true;
			}
		}
	}
	for (slot = 0U; slot < qcx_restore_session.slot_capacity; ++slot) {
		QCX_RESTORE_SESSION_VISIT_SLOT();
		client_t *const client = &svs.clients[slot];
		if (QCX_RestoreSessionClientIsLive(client)
			&& client->qcx_restore_roster_index < 0) {
			client->qcx_restore_waiting = true;
		}
	}
	QCX_RestoreSessionEvacuateUnmatchedReservations();
	for (index = 0U; index < qcx_restore_session.roster.count; ++index) {
		const qcx_restore_roster_entry_t *const entry =
			&qcx_restore_session.roster.entries[index];
		const int client_slot = QCX_RestoreSessionFindClientForRoster(index);
		if (entry->state == QCX_RESTORE_ENTRY_BOUND && client_slot >= 0
			&& (uint32_t)client_slot != entry->saved.saved_slot) {
			QCX_RestoreSessionSwapClients((uint32_t)client_slot,
				entry->saved.saved_slot);
		}
	}
	for (index = 0U; index < qcx_restore_session.roster.count; ++index) {
		int client_slot;
		if (!queue_waiting_new[index]) continue;
		client_slot = QCX_RestoreSessionFindClientForRoster(index);
		if (client_slot >= 0) {
			QCX_RestoreSessionQueueWaitingNew(&svs.clients[client_slot]);
		}
	}
	if (qcx_restore_session.initial_handshake_pending) {
		for (slot = 0U; slot < qcx_restore_session.slot_capacity; ++slot) {
			client_t *const client = &svs.clients[slot];
			QCX_RESTORE_SESSION_VISIT_SLOT();
			if (QCX_RestoreSessionClientIsLive(client)) {
				QCX_RestoreSessionQueueRestoredNew(client);
			}
		}
		qcx_restore_session.initial_handshake_pending = false;
	}
}

qbool QCX_RestoreSessionClientWaiting(const client_t *client)
{
	return client != NULL && client->qcx_restore_waiting;
}

qbool QCX_RestoreSessionClientPending(const client_t *client)
{
	return client != NULL && client->qcx_restore_pending;
}

qbool QCX_RestoreSessionClientRoleLocked(const client_t *client)
{
	qcx_restore_roster_entry_t *entry;
	if (client == NULL) return false;
	entry = QCX_RestoreSessionClientEntry((client_t *)client);
	return entry != NULL && (entry->state == QCX_RESTORE_ENTRY_BOUND
		|| entry->state == QCX_RESTORE_ENTRY_ACTIVE);
}

void QCX_RestoreSessionPrintRoster(client_t *client)
{
	uint32_t index;
	qbool printed_entry = false;
	if (client == NULL) return;
	if (!QCX_RestoreSessionWaiting()) {
		SV_ClientPrintf(client, PRINT_HIGH, "No QCX restore roster is waiting.\n");
		return;
	}
	SV_ClientPrintf(client, PRINT_HIGH, "Saved players available for restore:\n");
	for (index = 0U; index < qcx_restore_session.roster.count; ++index) {
		const qcx_restore_roster_entry_t *const entry =
			&qcx_restore_session.roster.entries[index];
		const char *const role = entry->saved.role == QCX_SAVE_ROLE_SPECTATOR
			? "spectator" : "player";
		if (entry->state != QCX_RESTORE_ENTRY_AVAILABLE) continue;
		printed_entry = true;
		if (entry->saved.team[0] == '\0') {
			SV_ClientPrintf(client, PRINT_HIGH, "%s [%s]\n", entry->saved.name, role);
		} else {
			SV_ClientPrintf(client, PRINT_HIGH, "%s [%s, team=%s]\n",
				entry->saved.name, role, entry->saved.team);
		}
	}
	if (!printed_entry) {
		SV_ClientPrintf(client, PRINT_HIGH, "No saved identities are currently available.\n");
		return;
	}
	SV_ClientPrintf(client, PRINT_HIGH,
		"Change your name to restore a saved player. Use \"cmd qcx_restore_list\" to show this list again.\n");
	if (qcx_restore_session.deadline > 0.0) {
		const double remaining = qcx_restore_session.deadline - Sys_DoubleTime();
		SV_ClientPrintf(client, PRINT_HIGH,
			"Otherwise you will join as a new player in %d seconds.\n",
			(int)(remaining > 0.0 ? remaining : 0.0));
	} else {
		SV_ClientPrintf(client, PRINT_HIGH,
			"Manual continuation is required before unmatched clients can join.\n");
	}
}

qbool QCX_RestoreSessionPrepareSpawn(client_t *client)
{
	qcx_restore_roster_entry_t *const entry = QCX_RestoreSessionClientEntry(client);
	edict_t *ent;
	if (entry == NULL || entry->state != QCX_RESTORE_ENTRY_BOUND) return false;
	ent = client->edict;
	if (ent == NULL) return false;
	client->entgravity = fofs_gravity ? EdictFieldFloat(ent, fofs_gravity) : 1.0f;
	client->maxspeed = fofs_maxspeed ? EdictFieldFloat(ent, fofs_maxspeed) : sv_maxspeed.value;
	memset(client->stats, 0, sizeof(client->stats));
	memset(client->frames, 0, sizeof(client->frames));
	client->delta_sequence = -1;
	client->lastservertimeupdate = -99.0;
	return true;
}

qbool QCX_RestoreSessionBegin(client_t *client)
{
	qcx_restore_roster_entry_t *const entry = QCX_RestoreSessionClientEntry(client);
	if (entry == NULL || entry->state != QCX_RESTORE_ENTRY_BOUND
		|| !QCX_RestoreRosterActivate(&qcx_restore_session.roster,
			(uint32_t)client->qcx_restore_roster_index)) {
		return false;
	}
	client->qcx_restore_pending = false;
	client->qcx_restore_waiting = false;
	qcx_restore_session.dirty = true;
	return true;
}

qbool QCX_RestoreSessionClientDropped(client_t *client)
{
	qcx_restore_roster_entry_t *const entry = QCX_RestoreSessionClientEntry(client);
	if (entry == NULL || (entry->state != QCX_RESTORE_ENTRY_BOUND
		&& entry->state != QCX_RESTORE_ENTRY_ACTIVE)
		|| !QCX_RestoreRosterRelease(&qcx_restore_session.roster,
			entry->saved.saved_slot)) {
		return false;
	}
	QCX_RestoreSessionClearClientFlags(client);
	QCX_RestoreSessionClearClientOriginalIdentity(client);
	qcx_restore_session.dirty = true;
	return true;
}
