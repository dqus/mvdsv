#include "qwsvdef.h"

extern void MVD_PlayerReset(int player);

void SV_RestartClientSignon(client_t *client)
{
	/* SV_SaveSpawnparms normally does this on map changes. Repeat the
	 * requirement at a restore handoff so Cmd_New_f cannot reject `new`
	 * merely because the old session was still marked spawned. */
	if (client->state == cs_spawned) {
		client->state = cs_connected;
	}
	SV_ClearReliable(client);
	/* Keep any in-flight reliable packet. The client leaves its old signon
	 * state via changing/reconnect, then sends `new` on the same netchannel,
	 * without another direct-connect or gameplay ClientConnect callback. */
	ClientReliableWrite_Begin(client, svc_stufftext, 20);
	ClientReliableWrite_String(client, "changing\nreconnect\n");
	client->send_message = true;
}

void SV_ResumeClientSignon(client_t *client)
{
	/* A waiting newcomer has not received serverdata. Its roster-list reply
	 * may still occupy the reliable retransmit slot; discard that obsolete
	 * pre-serverdata stream before starting the ordinary `new` path. */
	SV_ClearReliable(client);
	client->netchan.reliable_length = 0;
	client->netchan.last_reliable_sequence = client->netchan.outgoing_sequence;
	SV_QCXStartClientSignon(client);
	client->send_message = true;
}

static void SV_RebindClientSlot(client_t *client, unsigned int slot)
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

static void SV_ClearClientSlotMovement(client_t *client)
{
	client->delta_sequence = -1;
	client->antilag_position_next = 0;
	memset(client->antilag_positions, 0, sizeof(client->antilag_positions));
	client->laggedents_count = 0U;
	client->laggedents_frac = 0.0f;
	client->laggedents_time = 0.0f;
	memset(client->laggedents, 0, sizeof(client->laggedents));
}

/* Move connection state, not player entities. The caller chooses the slots
 * before fresh signon, when no old movement/delta history can remain valid.
 * Keep repair of client_t's self-pointers and references from other server
 * subsystems together here as client_t and its consumers evolve. */
void SV_SwapClientSlots(unsigned int left_slot, unsigned int right_slot)
{
	client_t *left;
	client_t *right;
	client_t temporary;
	edict_t *left_edict;
	edict_t *right_edict;

	assert(left_slot < MAX_CLIENTS && right_slot < MAX_CLIENTS);
	if (left_slot == right_slot) {
		return;
	}
	left = &svs.clients[left_slot];
	right = &svs.clients[right_slot];
	left_edict = left->edict;
	right_edict = right->edict;
	memcpy(&temporary, left, sizeof(temporary));
	memcpy(left, right, sizeof(*left));
	memcpy(right, &temporary, sizeof(*right));
	SV_RebindClientSlot(left, left_slot);
	SV_RebindClientSlot(right, right_slot);
	SV_ClearClientSlotMovement(left);
	SV_ClearClientSlotMovement(right);
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

void SV_ReleaseDroppedClientSlot(unsigned int slot)
{
	client_t *client;

	assert(slot < MAX_CLIENTS);
	client = &svs.clients[slot];
	/* A dropped pending QCX client is already free rather than a zombie. */
	assert(client->state == cs_zombie || client->state == cs_free);
	client->state = cs_free;
	SV_RebindClientSlot(client, slot);
	SV_ClearClientSlotMovement(client);
	MVD_PlayerReset((int)slot);
}
