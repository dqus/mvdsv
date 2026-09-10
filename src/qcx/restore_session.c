#include "qcx/restore_session.h"

#include "qcx/restore_roster.h"

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
} qcx_restore_session_t;

static qcx_restore_session_t qcx_restore_session;

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
			|| client->qcx_restore_pending) {
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
			&& !client->qcx_restore_pending
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
			|| svs.clients[slot].qcx_restore_pending) {
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
	(void)monotonic_now;
	if (!QCX_RestoreSessionImageIsValid(image)
		|| !QCX_RestoreRosterInit(&roster, image->roster, image->roster_count)) {
		return false;
	}
	QCX_RestoreSessionCancel();
	qcx_restore_session.roster = roster;
	qcx_restore_session.slot_capacity = image->metadata.client_slot_capacity;
	qcx_restore_session.state = image->roster_count == 0U
		? QCX_RESTORE_SESSION_COMPLETE : QCX_RESTORE_SESSION_WAITING;
	qcx_restore_session.dirty = image->roster_count != 0U;
	for (slot = 0U; slot < qcx_restore_session.slot_capacity; ++slot) {
		QCX_RESTORE_SESSION_VISIT_SLOT();
		QCX_RestoreSessionClearClientFlags(&svs.clients[slot]);
	}
	return true;
}

void QCX_RestoreSessionCancel(void)
{
	uint32_t slot;
	for (slot = 0U; slot < qcx_restore_session.slot_capacity; ++slot) {
		QCX_RESTORE_SESSION_VISIT_SLOT();
		QCX_RestoreSessionClearClientFlags(&svs.clients[slot]);
	}
	memset(&qcx_restore_session, 0, sizeof(qcx_restore_session));
}

qbool QCX_RestoreSessionBlocksSave(void)
{
	return qcx_restore_session.state == QCX_RESTORE_SESSION_WAITING;
}

qbool QCX_RestoreSessionWaiting(void)
{
	return qcx_restore_session.state == QCX_RESTORE_SESSION_WAITING;
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

client_t *QCX_RestoreSessionAdmissionSlot(const char *raw_name)
{
	uint32_t index;
	int slot;
	if (!QCX_RestoreSessionWaiting() || raw_name == NULL) return NULL;
	for (index = 0U; index < qcx_restore_session.roster.count; ++index) {
		const qcx_restore_roster_entry_t *const entry =
			&qcx_restore_session.roster.entries[index];
		if (entry->state == QCX_RESTORE_ENTRY_AVAILABLE
			&& Q_namecmp(entry->saved.name, raw_name) == 0
			&& svs.clients[entry->saved.saved_slot].state == cs_free) {
			return &svs.clients[entry->saved.saved_slot];
		}
	}
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

void QCX_RestoreSessionFrame(double monotonic_now)
{
	uint32_t index;
	uint32_t slot;
	(void)monotonic_now;
	if (!QCX_RestoreSessionWaiting() || !qcx_restore_session.dirty) return;
	qcx_restore_session.dirty = false;

	for (index = 0U; index < qcx_restore_session.roster.count; ++index) {
		qcx_restore_roster_entry_t *const entry = &qcx_restore_session.roster.entries[index];
		if (entry->state == QCX_RESTORE_ENTRY_BOUND) {
			(void)QCX_RestoreRosterRelease(&qcx_restore_session.roster,
				entry->saved.saved_slot);
		}
	}
	for (slot = 0U; slot < qcx_restore_session.slot_capacity; ++slot) {
		QCX_RESTORE_SESSION_VISIT_SLOT();
		QCX_RestoreSessionClearClientFlags(&svs.clients[slot]);
	}
	for (index = 0U; index < qcx_restore_session.roster.count; ++index) {
		const int client_slot = QCX_RestoreSessionFindLowestNamedClient(
			qcx_restore_session.roster.entries[index].saved.name);
		if (client_slot >= 0
			&& QCX_RestoreRosterBind(&qcx_restore_session.roster, index)) {
			client_t *const client = &svs.clients[client_slot];
			client->qcx_restore_pending = true;
			client->qcx_restore_roster_index = (int)index;
		}
	}
	for (slot = 0U; slot < qcx_restore_session.slot_capacity; ++slot) {
		QCX_RESTORE_SESSION_VISIT_SLOT();
		client_t *const client = &svs.clients[slot];
		if (QCX_RestoreSessionClientIsLive(client)
			&& !client->qcx_restore_pending) {
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
}

qbool QCX_RestoreSessionClientWaiting(const client_t *client)
{
	return client != NULL && client->qcx_restore_waiting;
}

qbool QCX_RestoreSessionClientPending(const client_t *client)
{
	return client != NULL && client->qcx_restore_pending;
}
