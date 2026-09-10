#include "qcx/restore_session.h"

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { test_slot_capacity = 4 };

server_t sv;
server_static_t svs;
client_t *sv_client;
edict_t *sv_player;
client_t *WatcherId;

static uint32_t name_comparisons;
static uint32_t slot_visits;
static uint32_t reset_calls;
static int reset_slots[32];
static uint32_t central_swap_calls;
static client_t *central_swap_left;
static client_t *central_swap_right;
static uint32_t drop_calls;
static int drop_slots[32];
static info_t *allocated_info[MAX_CLIENTS * 2];
static uint32_t allocated_info_count;

int Q_namecmp(const char *left, const char *right)
{
	++name_comparisons;
	if (left == NULL || right == NULL) return left == right ? 0 : 1;
	while (*left != '\0' || *right != '\0') {
		const int left_char = tolower((unsigned char)*left++ & 0x7fU);
		const int right_char = tolower((unsigned char)*right++ & 0x7fU);
		if (left_char != right_char) return left_char - right_char;
	}
	return 0;
}

void QCX_RestoreSessionTestVisitSlot(void)
{
	++slot_visits;
}

char *Info_Get(ctxinfo_t *context, const char *name)
{
	info_t *item;
	for (item = context == NULL ? NULL : context->info_list; item != NULL;
		item = item->next) {
		if (strcmp(item->name, name) == 0) return item->value;
	}
	return "";
}

void MVD_PlayerReset(int slot)
{
	assert(reset_calls < sizeof(reset_slots) / sizeof(reset_slots[0]));
	reset_slots[reset_calls++] = slot;
}

void Central_SwapClientPointers(client_t *left, client_t *right)
{
	++central_swap_calls;
	central_swap_left = left;
	central_swap_right = right;
}

void SV_DropClient(client_t *client)
{
	const int slot = (int)(client - svs.clients);
	assert(slot >= 0 && slot < test_slot_capacity);
	assert(drop_calls < sizeof(drop_slots) / sizeof(drop_slots[0]));
	drop_slots[drop_calls++] = slot;
	client->state = cs_free;
}

static info_t *make_info(const char *value)
{
	info_t *const item = calloc(1U, sizeof(*item));
	assert(item != NULL);
	item->name = strdup("name");
	item->value = strdup(value);
	assert(item->name != NULL && item->value != NULL);
	assert(allocated_info_count < sizeof(allocated_info) / sizeof(allocated_info[0]));
	allocated_info[allocated_info_count++] = item;
	return item;
}

static void reset_fixture(void)
{
	uint32_t index;
	QCX_RestoreSessionCancel();
	for (index = 0U; index < allocated_info_count; ++index) {
		free(allocated_info[index]->name);
		free(allocated_info[index]->value);
		free(allocated_info[index]);
	}
	allocated_info_count = 0U;
	memset(&sv, 0, sizeof(sv));
	memset(&svs, 0, sizeof(svs));
	sv_client = NULL;
	sv_player = NULL;
	WatcherId = NULL;
	name_comparisons = 0U;
	slot_visits = 0U;
	reset_calls = 0U;
	central_swap_calls = 0U;
	central_swap_left = NULL;
	central_swap_right = NULL;
	drop_calls = 0U;
}

static client_t *connect_client(uint32_t slot, const char *name, int userid)
{
	client_t *const client = &svs.clients[slot];
	assert(slot < test_slot_capacity);
	memset(client, 0, sizeof(*client));
	client->state = cs_connected;
	client->userid = userid;
	strcpy(client->name, name);
	client->_userinfo_ctx_.max = MAX_CLIENT_INFOS;
	client->_userinfo_ctx_.info_list = make_info(name);
	client->_userinfoshort_ctx_.max = MAX_CLIENT_INFOS;
	client->_userinfoshort_ctx_.info_list = make_info(name);
	client->netchan.message.data = client->netchan.message_buf;
	client->netchan.message.maxsize = (int)sizeof(client->netchan.message_buf);
	client->datagram.data = client->datagram_buf;
	client->datagram.maxsize = (int)sizeof(client->datagram_buf);
	client->num_backbuf = 1;
	client->backbuf.data = client->backbuf_data[0];
	client->backbuf.maxsize = (int)sizeof(client->backbuf_data[0]);
	client->edict = &sv.edicts[slot + 1U];
	client->delta_sequence = 11;
	client->antilag_position_next = 3;
	client->laggedents_count = 2U;
	client->laggedents_frac = 1.0f;
	client->laggedents_time = 2.0f;
	return client;
}

static qcx_save_image_t make_image(const qcx_save_roster_entry_t *entries,
	uint32_t count)
{
	qcx_save_image_t image = {0};
	image.metadata.client_slot_capacity = test_slot_capacity;
	image.metadata.entity_capacity = test_slot_capacity + 1U;
	image.roster_count = count;
	if (count != 0U) memcpy(image.roster, entries, count * sizeof(entries[0]));
	return image;
}

static qcx_save_roster_entry_t saved(uint32_t slot, const char *name)
{
	qcx_save_roster_entry_t entry = {0};
	entry.saved_slot = slot;
	entry.spawned = 1U;
	entry.role = QCX_SAVE_ROLE_PLAYER;
	strcpy(entry.name, name);
	return entry;
}

static void assert_client_integrity(client_t *client, uint32_t slot)
{
	assert(client->netchan.message.data == client->netchan.message_buf);
	assert(client->datagram.data == client->datagram_buf);
	assert(client->num_backbuf == 0
		|| client->backbuf.data == client->backbuf_data[client->num_backbuf - 1]);
	assert(client->edict == &sv.edicts[slot + 1U]);
	assert(strcmp(Info_Get(&client->_userinfo_ctx_, "name"), client->name) == 0);
}

static void install_and_reconcile(const qcx_save_roster_entry_t *entries, uint32_t count)
{
	const qcx_save_image_t image = make_image(entries, count);
	assert(QCX_RestoreSessionInstall(&image, 100.0));
	assert(QCX_RestoreSessionWaiting() == (count != 0U));
	QCX_RestoreSessionFrame(101.0);
}

static void test_moves_to_a_free_saved_slot_and_repairs_globals(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	client_t *client;
	reset_fixture();
	client = connect_client(3U, "Alice", 103);
	sv_client = client;
	sv_player = client->edict;
	WatcherId = client;
	install_and_reconcile(&entry, 1U);
	assert(svs.clients[1].userid == 103);
	assert(svs.clients[3].state == cs_free);
	assert(sv_client == &svs.clients[1]);
	assert(sv_player == &sv.edicts[2]);
	assert(WatcherId == &svs.clients[1]);
	assert(svs.clients[1].delta_sequence == -1);
	assert(svs.clients[1].antilag_position_next == 0);
	assert(svs.clients[1].laggedents_count == 0U);
	assert_client_integrity(&svs.clients[1], 1U);
	assert(reset_calls == 2U);
	assert(reset_slots[0] == 3 && reset_slots[1] == 1);
	assert(central_swap_calls == 1U);
	assert(central_swap_left == &svs.clients[3]);
	assert(central_swap_right == &svs.clients[1]);
}

static void test_two_saved_clients_can_cycle(void)
{
	const qcx_save_roster_entry_t entries[] = {saved(0U, "Alice"), saved(1U, "Bob")};
	reset_fixture();
	connect_client(0U, "Bob", 10);
	connect_client(1U, "Alice", 11);
	install_and_reconcile(entries, 2U);
	assert(svs.clients[0].userid == 11);
	assert(svs.clients[1].userid == 10);
	assert_client_integrity(&svs.clients[0], 0U);
	assert_client_integrity(&svs.clients[1], 1U);
}

static void test_matched_client_displaces_an_unmatched_client(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	reset_fixture();
	connect_client(1U, "Una", 1);
	connect_client(2U, "Alice", 2);
	install_and_reconcile(&entry, 1U);
	assert(svs.clients[0].userid == 1);
	assert(svs.clients[1].userid == 2);
	assert(svs.clients[2].state == cs_free);
	assert(QCX_RestoreSessionClientWaiting(&svs.clients[0]));
	assert(QCX_RestoreSessionClientPending(&svs.clients[1]));
	assert_client_integrity(&svs.clients[0], 0U);
	assert_client_integrity(&svs.clients[1], 1U);
}

static void test_unmatched_client_is_evacuated_from_available_reservation(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	reset_fixture();
	connect_client(1U, "Una", 1);
	install_and_reconcile(&entry, 1U);
	assert(svs.clients[0].userid == 1);
	assert(svs.clients[1].state == cs_free);
	assert(QCX_RestoreSessionSlotReserved(1U));
	assert(QCX_RestoreSessionClientWaiting(&svs.clients[0]));
	assert_client_integrity(&svs.clients[0], 0U);
}

static void test_reverse_reconnect_order_is_a_permutation(void)
{
	const qcx_save_roster_entry_t entries[] = {saved(3U, "Alice"), saved(0U, "Bob")};
	reset_fixture();
	connect_client(0U, "Alice", 1);
	connect_client(3U, "Bob", 2);
	install_and_reconcile(entries, 2U);
	assert(svs.clients[0].userid == 2);
	assert(svs.clients[3].userid == 1);
	assert_client_integrity(&svs.clients[0], 0U);
	assert_client_integrity(&svs.clients[3], 3U);
}

static void test_duplicate_waiting_names_claim_the_lowest_slot(void)
{
	const qcx_save_roster_entry_t entry = saved(2U, "Alice");
	reset_fixture();
	connect_client(1U, "Alice", 1);
	connect_client(3U, "Alice", 3);
	install_and_reconcile(&entry, 1U);
	assert(svs.clients[2].userid == 1);
	assert(QCX_RestoreSessionClientPending(&svs.clients[2]));
	assert(svs.clients[3].userid == 3);
	assert(QCX_RestoreSessionClientWaiting(&svs.clients[3]));
}

static void test_admission_does_not_consume_reserved_slots(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	const qcx_save_image_t image = make_image(&entry, 1U);
	reset_fixture();
	assert(QCX_RestoreSessionInstall(&image, 100.0));
	assert(QCX_RestoreSessionAdmissionSlot("aLiCe") == &svs.clients[1]);
	assert(QCX_RestoreSessionAdmissionSlot("Una") == &svs.clients[0]);
	connect_client(0U, "Used", 1);
	connect_client(2U, "Used", 2);
	connect_client(3U, "Used", 3);
	assert(QCX_RestoreSessionAdmissionSlot("Una") == NULL);
	assert(QCX_RestoreSessionSlotReserved(1U));
}

static void test_name_change_is_reconciled_only_at_the_safe_frame(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	client_t *client;
	reset_fixture();
	connect_client(3U, "Alice", 3);
	install_and_reconcile(&entry, 1U);
	client = &svs.clients[1];
	strcpy(client->name, "Una");
	strcpy(Info_Get(&client->_userinfo_ctx_, "name"), "Una");
	QCX_RestoreSessionNameChanged(client);
	assert(client == &svs.clients[1]);
	QCX_RestoreSessionFrame(102.0);
	assert(svs.clients[0].userid == 3);
	assert(svs.clients[1].state == cs_free);
	assert(QCX_RestoreSessionClientWaiting(&svs.clients[0]));
	assert(!QCX_RestoreSessionClientPending(&svs.clients[0]));
}

static void test_excess_unmatched_client_is_dropped_from_highest_slot(void)
{
	const qcx_save_roster_entry_t entry = saved(0U, "Alice");
	reset_fixture();
	connect_client(0U, "Una", 0);
	connect_client(1U, "Uno", 1);
	connect_client(2U, "Une", 2);
	connect_client(3U, "Uni", 3);
	install_and_reconcile(&entry, 1U);
	assert(drop_calls == 1U);
	assert(drop_slots[0] == 3);
	assert(svs.clients[0].state == cs_free);
	assert(QCX_RestoreSessionSlotReserved(0U));
}

static void test_inactive_and_complete_frames_do_not_compare_names(void)
{
	qcx_save_image_t empty = make_image(NULL, 0U);
	reset_fixture();
	QCX_RestoreSessionFrame(1.0);
	assert(name_comparisons == 0U && slot_visits == 0U && reset_calls == 0U);
	assert(QCX_RestoreSessionInstall(&empty, 2.0));
	name_comparisons = 0U;
	slot_visits = 0U;
	QCX_RestoreSessionFrame(3.0);
	assert(name_comparisons == 0U && slot_visits == 0U && reset_calls == 0U);
	assert(!QCX_RestoreSessionWaiting());
}

int main(void)
{
	test_moves_to_a_free_saved_slot_and_repairs_globals();
	test_two_saved_clients_can_cycle();
	test_matched_client_displaces_an_unmatched_client();
	test_unmatched_client_is_evacuated_from_available_reservation();
	test_reverse_reconnect_order_is_a_permutation();
	test_duplicate_waiting_names_claim_the_lowest_slot();
	test_admission_does_not_consume_reserved_slots();
	test_name_change_is_reconciled_only_at_the_safe_frame();
	test_excess_unmatched_client_is_dropped_from_highest_slot();
	test_inactive_and_complete_frames_do_not_compare_names();
	reset_fixture();
	return 0;
}
