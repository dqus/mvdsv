#include "qcx/restore_session.h"

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

enum { test_slot_capacity = 4 };

server_t sv;
server_static_t svs;
client_t *sv_client;
edict_t *sv_player;
client_t *WatcherId;
cvar_t sv_maxspeed;
int fofs_gravity;
int fofs_maxspeed;
extern cvar_t qcx_restore_wait_timeout;

static uint32_t name_comparisons;
static uint32_t slot_visits;
static uint32_t reset_calls;
static int reset_slots[32];
static uint32_t central_swap_calls;
static client_t *central_swap_left;
static client_t *central_swap_right;
static uint32_t drop_calls;
static int drop_slots[32];
static uint32_t client_print_calls;
static char client_print_text[256];
static uint32_t clear_reliable_calls;
static uint32_t reliable_stufftext_calls;
static uint32_t reliable_reconnect_requests;
static uint32_t qcx_signon_starts;
static uint32_t pause_reason_calls;
static int last_pause_reason;
static qbool last_pause_active;
static qbool last_pause_notify;
static uint32_t game_disconnect_calls;
static qbool game_disconnect_spectator;
static qbool fallback_capacity_allowed;
static uint32_t fallback_capacity_calls;
static double test_monotonic_now;
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

qbool Info_Set(ctxinfo_t *context, const char *name, const char *value)
{
	info_t *item;
	assert(context != NULL && name != NULL && value != NULL);
	if (value[0] == '\0') return Info_Remove(context, name);
	for (item = context->info_list; item != NULL; item = item->next) {
		if (strcmp(item->name, name) == 0) {
			free(item->value);
			item->value = strdup(value);
			assert(item->value != NULL);
			return true;
		}
	}
	if (context->cur >= context->max) return false;
	item = calloc(1U, sizeof(*item));
	assert(item != NULL);
	item->name = strdup(name);
	item->value = strdup(value);
	assert(item->name != NULL && item->value != NULL);
	assert(allocated_info_count < sizeof(allocated_info) / sizeof(allocated_info[0]));
	allocated_info[allocated_info_count++] = item;
	item->next = context->info_list;
	context->info_list = item;
	++context->cur;
	return true;
}

qbool Info_SetStar(ctxinfo_t *context, const char *name, const char *value)
{
	return Info_Set(context, name, value);
}

qbool Info_Remove(ctxinfo_t *context, const char *name)
{
	info_t **item;
	assert(context != NULL && name != NULL);
	for (item = &context->info_list; *item != NULL; item = &(*item)->next) {
		if (strcmp((*item)->name, name) == 0) {
			*item = (*item)->next;
			--context->cur;
			return true;
		}
	}
	return false;
}

void Info_RemoveAll(ctxinfo_t *context)
{
	/* Allocation ownership stays with reset_fixture, as for Info_Remove. */
	memset(context->info_hash, 0, sizeof(context->info_hash));
	context->info_list = NULL;
	context->cur = 0;
}

void MVD_PlayerReset(int slot)
{
	assert(reset_calls < sizeof(reset_slots) / sizeof(reset_slots[0]));
	reset_slots[reset_calls++] = slot;
}

void SV_ClientPrintf(client_t *client, int level, char *format, ...)
{
	va_list arguments;
	(void)client;
	(void)level;
	++client_print_calls;
	va_start(arguments, format);
	vsnprintf(client_print_text, sizeof(client_print_text), format, arguments);
	va_end(arguments);
}

void SV_ClearReliable(client_t *client)
{
	++clear_reliable_calls;
	client->netchan.message.cursize = 0;
	client->num_backbuf = 0;
}

void ClientReliableWrite_Begin(client_t *client, int command, int maxsize)
{
	(void)client;
	(void)maxsize;
	if (command == svc_stufftext) ++reliable_stufftext_calls;
}

void ClientReliableWrite_String(client_t *client, char *value)
{
	(void)client;
	assert(strcmp(value, "changing\nreconnect\n") == 0);
	++reliable_reconnect_requests;
}

void SV_QCXStartClientSignon(client_t *client)
{
	(void)client;
	++qcx_signon_starts;
}

void SV_SetPauseReason(int bit, qbool active, const char *message, qbool notify_clients)
{
	(void)message;
	++pause_reason_calls;
	last_pause_reason = bit;
	last_pause_active = active;
	last_pause_notify = notify_clients;
	if (active) sv.paused |= bit;
	else sv.paused &= ~bit;
}

void Cvar_Register(cvar_t *variable) { (void)variable; }
void Cvar_SetValue(cvar_t *variable, const float value) { variable->value = value; }
void Cmd_AddCommand(const char *name, xcommand_t command)
{
	(void)name;
	(void)command;
}
void Con_Printf(char *format, ...) { (void)format; }
double Sys_DoubleTime(void) { return test_monotonic_now; }

void PR_GameClientDisconnect(int spectator)
{
	++game_disconnect_calls;
	game_disconnect_spectator = spectator != 0;
}

void ED_Free(edict_t *edict)
{
	edict->e.free = true;
}

void QCX_RefreshClientReplication(void)
{
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
	client->state = cs_zombie;
}

qbool SV_AdmitRestoreFallback(client_t *client)
{
	(void)client;
	++fallback_capacity_calls;
	return fallback_capacity_allowed;
}

static info_t *make_info(const char *name, const char *value)
{
	info_t *const item = calloc(1U, sizeof(*item));
	assert(item != NULL);
	item->name = strdup(name);
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
	client_print_calls = 0U;
	client_print_text[0] = '\0';
	clear_reliable_calls = 0U;
	reliable_stufftext_calls = 0U;
	reliable_reconnect_requests = 0U;
	qcx_signon_starts = 0U;
	pause_reason_calls = 0U;
	last_pause_reason = 0;
	last_pause_active = false;
	last_pause_notify = false;
	game_disconnect_calls = 0U;
	game_disconnect_spectator = false;
	fallback_capacity_allowed = true;
	fallback_capacity_calls = 0U;
	test_monotonic_now = 100.0;
	sv_maxspeed.value = 320.0f;
	qcx_restore_wait_timeout.value = 60.0f;
}

static client_t *connect_client(uint32_t slot, const char *name, int userid)
{
	client_t *const client = &svs.clients[slot];
	assert(slot < test_slot_capacity);
	memset(client, 0, sizeof(*client));
	client->state = cs_connected;
	client->qcx_restore_fallback_allowed = true;
	client->qcx_restore_player_allowed = true;
	client->qcx_restore_spectator_allowed = true;
	client->userid = userid;
	strcpy(client->name, name);
	client->_userinfo_ctx_.max = MAX_CLIENT_INFOS;
	client->_userinfo_ctx_.info_list = make_info("name", name);
	client->_userinfo_ctx_.cur = 1;
	client->_userinfoshort_ctx_.max = MAX_CLIENT_INFOS;
	client->_userinfoshort_ctx_.info_list = make_info("name", name);
	client->_userinfoshort_ctx_.cur = 1;
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

static void test_initial_reconciliation_queues_fresh_signon_after_matching(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	const qcx_save_image_t image = make_image(&entry, 1U);
	reset_fixture();
	connect_client(1U, "Alice", 1);
	connect_client(0U, "Una", 0);
	assert(QCX_RestoreSessionInstall(&image, 100.0));
	assert(clear_reliable_calls == 0U);
	assert(QCX_RestoreSessionClientWaiting(&svs.clients[1]));
	QCX_RestoreSessionFrame(101.0);
	assert(QCX_RestoreSessionClientPending(&svs.clients[1]));
	assert(QCX_RestoreSessionClientWaiting(&svs.clients[0]));
	assert(clear_reliable_calls == 2U);
	assert(reliable_stufftext_calls == 2U);
	assert(reliable_reconnect_requests == 2U);
	assert(svs.clients[1].state == cs_connected);
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

static void test_admission_uses_the_saved_identity_role(void)
{
	qcx_save_roster_entry_t player = saved(1U, "Alice");
	qcx_save_roster_entry_t spectator = saved(2U, "Bob");
	qcx_save_image_t image;
	qbool role = true;
	reset_fixture();
	player.role = QCX_SAVE_ROLE_PLAYER;
	spectator.role = QCX_SAVE_ROLE_SPECTATOR;
	image = make_image(&player, 1U);
	assert(QCX_RestoreSessionInstall(&image, 100.0));
	assert(QCX_RestoreSessionAdmissionRole("aLiCe", &role));
	assert(!role);
	assert(!QCX_RestoreSessionAdmissionRole("Una", &role));

	reset_fixture();
	image = make_image(&spectator, 1U);
	assert(QCX_RestoreSessionInstall(&image, 100.0));
	assert(QCX_RestoreSessionAdmissionRole("Bob", &role));
	assert(role);
}

static void set_client_role_and_team(client_t *client, qbool spectator, const char *team)
{
	client->spectator = spectator;
	strcpy(client->team, team);
	if (spectator) {
		assert(Info_SetStar(&client->_userinfo_ctx_, "*spectator", "1"));
		assert(Info_SetStar(&client->_userinfoshort_ctx_, "*spectator", "1"));
	} else {
		(void)Info_Remove(&client->_userinfo_ctx_, "*spectator");
		(void)Info_Remove(&client->_userinfoshort_ctx_, "*spectator");
	}
	assert(Info_Set(&client->_userinfo_ctx_, "team", team));
	assert(Info_Set(&client->_userinfoshort_ctx_, "team", team));
}

static void test_bound_identity_keeps_connection_owned_state(void)
{
	qcx_save_roster_entry_t entry = saved(1U, "Alice");
	char team[CLIENT_NAME_LEN];
	char userinfo_spectator[2];
	char userinfo_team[CLIENT_NAME_LEN];
	char userinfoshort_spectator[2];
	char userinfoshort_team[CLIENT_NAME_LEN];
	float spawn_parms[NUM_SPAWN_PARMS];
	qbool spectator;
	uint32_t index;
	reset_fixture();
	strcpy(entry.team, "red");
	entry.role = QCX_SAVE_ROLE_PLAYER;
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		entry.spawn_parms[index] = 200.0f + (float)index;
	}
	connect_client(3U, "aLiCe", 3);
	set_client_role_and_team(&svs.clients[3], true, "blue");
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		svs.clients[3].spawn_parms[index] = 100.0f + (float)index;
	}
	spectator = svs.clients[3].spectator;
	strcpy(team, svs.clients[3].team);
	strcpy(userinfo_spectator,
		Info_Get(&svs.clients[3]._userinfo_ctx_, "*spectator"));
	strcpy(userinfo_team, Info_Get(&svs.clients[3]._userinfo_ctx_, "team"));
	strcpy(userinfoshort_spectator,
		Info_Get(&svs.clients[3]._userinfoshort_ctx_, "*spectator"));
	strcpy(userinfoshort_team,
		Info_Get(&svs.clients[3]._userinfoshort_ctx_, "team"));
	memcpy(spawn_parms, svs.clients[3].spawn_parms, sizeof(spawn_parms));

	install_and_reconcile(&entry, 1U);
	assert(QCX_RestoreSessionClientPending(&svs.clients[1]));
	assert((svs.clients[1].spectator != 0) == spectator);
	assert(strcmp(svs.clients[1].name, "aLiCe") == 0);
	assert(strcmp(svs.clients[1].team, team) == 0);
	assert(strcmp(Info_Get(&svs.clients[1]._userinfo_ctx_, "*spectator"),
		userinfo_spectator) == 0);
	assert(strcmp(Info_Get(&svs.clients[1]._userinfo_ctx_, "team"),
		userinfo_team) == 0);
	assert(strcmp(Info_Get(&svs.clients[1]._userinfoshort_ctx_, "*spectator"),
		userinfoshort_spectator) == 0);
	assert(strcmp(Info_Get(&svs.clients[1]._userinfoshort_ctx_, "team"),
		userinfoshort_team) == 0);
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		assert(svs.clients[1].spawn_parms[index] == spawn_parms[index]);
	}
	assert(client_print_calls == 1U);
	assert(strcmp(client_print_text, "Selected saved identity Alice.\n") == 0);
}

static void test_bound_identity_exposes_saved_role_for_signon(void)
{
	qcx_save_roster_entry_t entry = saved(1U, "Alice");
	reset_fixture();
	entry.role = QCX_SAVE_ROLE_PLAYER;
	connect_client(1U, "Alice", 1);
	svs.clients[1].spectator = true;
	install_and_reconcile(&entry, 1U);
	assert(svs.clients[1].spectator);
	assert(!QCX_RestoreSessionEffectiveSpectator(&svs.clients[1]));

	reset_fixture();
	entry.role = QCX_SAVE_ROLE_SPECTATOR;
	connect_client(1U, "Alice", 1);
	svs.clients[1].spectator = false;
	install_and_reconcile(&entry, 1U);
	assert(!svs.clients[1].spectator);
	assert(QCX_RestoreSessionEffectiveSpectator(&svs.clients[1]));
}

static void test_begin_commits_saved_identity(qbool saved_spectator, qbool saved_spawned)
{
	qcx_save_roster_entry_t entry = saved(1U, "Alice");
	qcx_restore_session_status_t status;
	client_t *client;
	qbool restores_spawned_gameplay;
	uint32_t index;
	reset_fixture();
	entry.role = saved_spectator ? QCX_SAVE_ROLE_SPECTATOR : QCX_SAVE_ROLE_PLAYER;
	entry.spawned = saved_spawned;
	strcpy(entry.team, "red");
	client = connect_client(1U, "Alice", 1);
	set_client_role_and_team(client, !saved_spectator, "blue");
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		client->spawn_parms[index] = 100.0f + (float)index;
		entry.spawn_parms[index] = 200.0f + (float)index;
	}
	assert((client->spectator != 0) == !saved_spectator);
	assert(strcmp(client->team, "blue") == 0);
	if (!saved_spectator) {
		client->_userinfo_ctx_.max = client->_userinfo_ctx_.cur;
		client->_userinfoshort_ctx_.max = client->_userinfoshort_ctx_.cur;
	}
	install_and_reconcile(&entry, 1U);
	assert(QCX_RestoreSessionClientPending(client));
	assert(QCX_RestoreSessionPrepareSpawn(client) == saved_spawned);
	assert((client->spectator != 0) == !saved_spectator);
	assert(strcmp(client->team, "blue") == 0);
	assert(strcmp(Info_Get(&client->_userinfo_ctx_, "*spectator"),
		saved_spectator ? "" : "1") == 0);
	assert(strcmp(Info_Get(&client->_userinfoshort_ctx_, "*spectator"),
		saved_spectator ? "" : "1") == 0);
	assert(strcmp(Info_Get(&client->_userinfo_ctx_, "team"), "blue") == 0);
	assert(strcmp(Info_Get(&client->_userinfoshort_ctx_, "team"), "blue") == 0);
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		assert(client->spawn_parms[index] == 100.0f + (float)index);
	}
	QCX_RestoreSessionGetStatus(&status, 101.0);
	assert(status.bound_count == 1U && status.active_count == 0U);
	client->sendinfo = false;
	restores_spawned_gameplay = !saved_spawned;
	assert(QCX_RestoreSessionCommitBegin(client, &restores_spawned_gameplay));
	assert(client->sendinfo);
	assert(restores_spawned_gameplay == saved_spawned);
	assert((client->spectator != 0) == saved_spectator);
	assert(strcmp(client->team, "red") == 0);
	assert(strcmp(Info_Get(&client->_userinfo_ctx_, "name"), "Alice") == 0);
	assert(strcmp(Info_Get(&client->_userinfoshort_ctx_, "name"), "Alice") == 0);
	assert(strcmp(Info_Get(&client->_userinfo_ctx_, "*spectator"),
		saved_spectator ? "1" : "") == 0);
	assert(strcmp(Info_Get(&client->_userinfoshort_ctx_, "*spectator"),
		saved_spectator ? "1" : "") == 0);
	assert(strcmp(Info_Get(&client->_userinfo_ctx_, "team"), "red") == 0);
	assert(strcmp(Info_Get(&client->_userinfoshort_ctx_, "team"), "red") == 0);
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		assert(client->spawn_parms[index] == 200.0f + (float)index);
	}
	QCX_RestoreSessionGetStatus(&status, 101.0);
	assert(status.bound_count == 0U && status.active_count == 1U);
	assert(!QCX_RestoreSessionClientPending(client));
	assert(!QCX_RestoreSessionClientWaiting(client));
	assert(!QCX_RestoreSessionPrepareSpawn(client));
}

static void test_begin_rejects_full_userinfo_without_partial_commit(
	qbool short_context, qbool missing_spectator)
{
	qcx_save_roster_entry_t entry = saved(1U, "Alice");
	qcx_restore_session_status_t status;
	client_t *client;
	client_t before;
	ctxinfo_t *full_context;
	qbool restores_spawned_gameplay = true;
	uint32_t index;
	reset_fixture();
	entry.role = missing_spectator ? QCX_SAVE_ROLE_SPECTATOR : QCX_SAVE_ROLE_PLAYER;
	strcpy(entry.team, "red");
	client = connect_client(1U, "Alice", 1);
	set_client_role_and_team(client, false, "blue");
	full_context = short_context ? &client->_userinfoshort_ctx_ : &client->_userinfo_ctx_;
	if (!missing_spectator) assert(Info_Remove(full_context, "team"));
	full_context->max = full_context->cur;
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		client->spawn_parms[index] = 100.0f + (float)index;
		entry.spawn_parms[index] = 200.0f + (float)index;
	}
	install_and_reconcile(&entry, 1U);
	client->sendinfo = false;
	memcpy(&before, client, sizeof(before));
	assert(!QCX_RestoreSessionCommitBegin(client, &restores_spawned_gameplay));
	assert(!restores_spawned_gameplay);
	assert(memcmp(client, &before, sizeof(before)) == 0);
	assert(strcmp(Info_Get(&client->_userinfo_ctx_, "*spectator"), "") == 0);
	assert(strcmp(Info_Get(&client->_userinfoshort_ctx_, "*spectator"), "") == 0);
	assert(strcmp(Info_Get(&client->_userinfo_ctx_, "team"),
		!short_context && !missing_spectator ? "" : "blue") == 0);
	assert(strcmp(Info_Get(&client->_userinfoshort_ctx_, "team"),
		short_context && !missing_spectator ? "" : "blue") == 0);
	QCX_RestoreSessionGetStatus(&status, 101.0);
	assert(status.bound_count == 1U && status.active_count == 0U);
	assert(QCX_RestoreSessionClientPending(client));
	/* A rejected attempt leaves the identity available for a later commit. */
	++full_context->max;
	assert(QCX_RestoreSessionCommitBegin(client, &restores_spawned_gameplay));
	assert(restores_spawned_gameplay);
	assert(client->sendinfo);
	assert((client->spectator != 0) == missing_spectator);
	assert(strcmp(client->team, "red") == 0);
	assert(strcmp(Info_Get(&client->_userinfo_ctx_, "team"), "red") == 0);
	assert(strcmp(Info_Get(&client->_userinfoshort_ctx_, "team"), "red") == 0);
}

static void test_begin_can_replace_team_with_spectator_in_full_userinfo(void)
{
	qcx_save_roster_entry_t entry = saved(1U, "Alice");
	client_t *client;
	reset_fixture();
	entry.role = QCX_SAVE_ROLE_SPECTATOR;
	client = connect_client(1U, "Alice", 1);
	set_client_role_and_team(client, false, "blue");
	client->_userinfo_ctx_.max = client->_userinfo_ctx_.cur;
	client->_userinfoshort_ctx_.max = client->_userinfoshort_ctx_.cur;
	install_and_reconcile(&entry, 1U);
	assert(QCX_RestoreSessionCommitBegin(client, NULL));
	assert(client->spectator && client->team[0] == '\0');
	assert(strcmp(Info_Get(&client->_userinfo_ctx_, "*spectator"), "1") == 0);
	assert(strcmp(Info_Get(&client->_userinfoshort_ctx_, "*spectator"), "1") == 0);
	assert(strcmp(Info_Get(&client->_userinfo_ctx_, "team"), "") == 0);
	assert(strcmp(Info_Get(&client->_userinfoshort_ctx_, "team"), "") == 0);
	assert(client->_userinfo_ctx_.cur == client->_userinfo_ctx_.max);
	assert(client->_userinfoshort_ctx_.cur == client->_userinfoshort_ctx_.max);
}

static void test_bound_client_without_fallback_is_dropped_on_continue(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	const qcx_save_image_t image = make_image(&entry, 1U);
	reset_fixture();
	assert(QCX_RestoreSessionInstall(&image, 100.0));
	connect_client(1U, "Alice", 1);
	svs.clients[1].qcx_restore_fallback_allowed = false;
	QCX_RestoreSessionObserveClient(&svs.clients[1]);
	QCX_RestoreSessionFrame(101.0);
	assert(QCX_RestoreSessionClientPending(&svs.clients[1]));
	QCX_RestoreSessionContinue();
	QCX_RestoreSessionFrame(102.0);
	assert(drop_calls == 1U);
	assert(drop_slots[0] == 1);
	assert(svs.clients[1].state == cs_zombie);
}

static void test_bound_client_without_fallback_capacity_is_dropped_on_continue(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	const qcx_save_image_t image = make_image(&entry, 1U);
	reset_fixture();
	assert(QCX_RestoreSessionInstall(&image, 100.0));
	connect_client(1U, "Alice", 1);
	fallback_capacity_allowed = false;
	QCX_RestoreSessionObserveClient(&svs.clients[1]);
	QCX_RestoreSessionFrame(101.0);
	assert(QCX_RestoreSessionClientPending(&svs.clients[1]));
	QCX_RestoreSessionContinue();
	QCX_RestoreSessionFrame(102.0);
	assert(fallback_capacity_calls == 1U);
	assert(drop_calls == 1U);
	assert(drop_slots[0] == 1);
	assert(svs.clients[1].state == cs_zombie);
}

static void test_roster_list_prints_only_available_saved_identities(void)
{
	qcx_save_roster_entry_t entries[2] = { saved(1U, "Alice"), saved(2U, "Bob") };
	reset_fixture();
	entries[0].role = QCX_SAVE_ROLE_PLAYER;
	entries[1].role = QCX_SAVE_ROLE_SPECTATOR;
	strcpy(entries[1].team, "blue");
	connect_client(0U, "Una", 0);
	install_and_reconcile(entries, 2U);
	client_print_calls = 0U;
	QCX_RestoreSessionPrintRoster(&svs.clients[0]);
	assert(client_print_calls == 5U);
	assert(strstr(client_print_text, "Otherwise you will join") != NULL);

	strcpy(svs.clients[0].name, "Alice");
	QCX_RestoreSessionNameChanged(&svs.clients[0]);
	QCX_RestoreSessionFrame(102.0);
	client_print_calls = 0U;
	client_print_text[0] = '\0';
	QCX_RestoreSessionPrintRoster(&svs.clients[1]);
	assert(client_print_calls == 4U);
	assert(strstr(client_print_text, "Otherwise you will join") != NULL);
}

static void test_bound_client_preserves_its_edict_through_begin_and_drop(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	edict_t before;
	reset_fixture();
	connect_client(1U, "Alice", 1);
	sv.edicts[2].e.free = false;
	sv.edicts[2].e.freetime = 9.0f;
	install_and_reconcile(&entry, 1U);
	before = sv.edicts[2];
	assert(QCX_RestoreSessionPrepareSpawn(&svs.clients[1]));
	assert(memcmp(&before, &sv.edicts[2], sizeof(before)) == 0);
	assert(svs.clients[1].entgravity == 1.0f);
	assert(svs.clients[1].maxspeed == 320.0f);
	assert(QCX_RestoreSessionCommitBegin(&svs.clients[1], NULL));
	assert(QCX_RestoreSessionClientRoleLocked(&svs.clients[1]));
	assert(!QCX_RestoreSessionClientPending(&svs.clients[1]));
	assert(QCX_RestoreSessionClientDropped(&svs.clients[1]));
	assert(!QCX_RestoreSessionClientRoleLocked(&svs.clients[1]));
	svs.clients[1].state = cs_free;
	assert(QCX_RestoreSessionAdmissionSlot("Alice") == &svs.clients[1]);
	assert(memcmp(&before, &sv.edicts[2], sizeof(before)) == 0);
}

static void test_unspawned_saved_client_uses_the_ordinary_spawn_path(void)
{
	qcx_save_roster_entry_t entry = saved(1U, "Alice");
	reset_fixture();
	entry.spawned = 0U;
	connect_client(1U, "Alice", 1);
	install_and_reconcile(&entry, 1U);
	assert(QCX_RestoreSessionClientPending(&svs.clients[1]));
	assert(!QCX_RestoreSessionPrepareSpawn(&svs.clients[1]));
	assert(QCX_RestoreSessionCommitBegin(&svs.clients[1], NULL));
}

static void test_fresh_observed_client_waits_before_reconciliation(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	const qcx_save_image_t image = make_image(&entry, 1U);
	client_t *client;
	reset_fixture();
	assert(QCX_RestoreSessionInstall(&image, 100.0));
	client = connect_client(0U, "Una", 0);
	client->state = cs_preconnected;
	assert(!QCX_RestoreSessionClientWaiting(client));
	QCX_RestoreSessionObserveClient(client);
	assert(QCX_RestoreSessionClientWaiting(client));
}

static void test_active_identity_cannot_claim_another_saved_slot(void)
{
	qcx_save_roster_entry_t entries[2] = { saved(1U, "Alice"), saved(2U, "Bob") };
	reset_fixture();
	connect_client(1U, "Alice", 1);
	install_and_reconcile(entries, 2U);
	assert(QCX_RestoreSessionCommitBegin(&svs.clients[1], NULL));
	connect_client(0U, "Una", 0);
	QCX_RestoreSessionObserveClient(&svs.clients[0]);
	QCX_RestoreSessionFrame(102.0);
	assert(svs.clients[1].qcx_restore_roster_index == 0);
	assert(!QCX_RestoreSessionClientPending(&svs.clients[1]));
	assert(!QCX_RestoreSessionClientWaiting(&svs.clients[1]));
	assert(QCX_RestoreSessionClientWaiting(&svs.clients[0]));

	strcpy(svs.clients[1].name, "Bob");
	QCX_RestoreSessionNameChanged(&svs.clients[1]);
	QCX_RestoreSessionFrame(103.0);
	assert(svs.clients[1].qcx_restore_roster_index == 0);
	assert(!QCX_RestoreSessionClientPending(&svs.clients[1]));
	assert(!QCX_RestoreSessionClientWaiting(&svs.clients[1]));
	assert(QCX_RestoreSessionAdmissionSlot("Bob") == &svs.clients[2]);
}

static void test_bound_drop_reopens_the_saved_identity(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	edict_t before;
	reset_fixture();
	connect_client(1U, "Alice", 1);
	sv.edicts[2].e.free = false;
	sv.edicts[2].e.freetime = 9.0f;
	install_and_reconcile(&entry, 1U);
	before = sv.edicts[2];
	assert(QCX_RestoreSessionClientDropped(&svs.clients[1]));
	svs.clients[1].state = cs_free;
	assert(QCX_RestoreSessionAdmissionSlot("Alice") == &svs.clients[1]);
	assert(memcmp(&before, &sv.edicts[2], sizeof(before)) == 0);
}

static void test_late_name_claim_restarts_signon_at_the_safe_frame(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	client_t *client;
	reset_fixture();
	connect_client(0U, "Una", 0);
	install_and_reconcile(&entry, 1U);
	assert(QCX_RestoreSessionClientWaiting(&svs.clients[0]));
	client = &svs.clients[0];
	strcpy(client->name, "Alice");
	strcpy(Info_Get(&client->_userinfo_ctx_, "name"), "Alice");
	QCX_RestoreSessionNameChanged(client);
	QCX_RestoreSessionFrame(102.0);
	assert(svs.clients[1].userid == 0);
	assert(QCX_RestoreSessionClientPending(&svs.clients[1]));
	/* The initial client receives a normal map-change handshake.  A late
	 * claimant instead enters Cmd_New_f directly after its safe-frame swap. */
	assert(clear_reliable_calls == 2U);
	assert(reliable_stufftext_calls == 1U);
	assert(reliable_reconnect_requests == 1U);
	assert(qcx_signon_starts == 1U);
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

static void test_timeout_abandons_bound_and_waiting_clients(void)
{
	qcx_save_roster_entry_t entry = saved(1U, "Alice");
	uint32_t index;
	reset_fixture();
	connect_client(1U, "Alice", 1);
	connect_client(0U, "Una", 0);
	set_client_role_and_team(&svs.clients[1], true, "blue");
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		svs.clients[1].spawn_parms[index] = 100.0f + (float)index;
		entry.spawn_parms[index] = 200.0f + (float)index;
	}
	sv.edicts[2].e.free = false;
	install_and_reconcile(&entry, 1U);
	assert(QCX_RestoreSessionWaiting());
	assert((sv.paused & SV_PAUSE_RESTORE) != 0);
	QCX_RestoreSessionFrame(159.9);
	assert(QCX_RestoreSessionWaiting());
	assert(game_disconnect_calls == 0U);
	QCX_RestoreSessionFrame(160.0);
	assert(!QCX_RestoreSessionWaiting());
	assert(game_disconnect_calls == 1U);
	assert(!game_disconnect_spectator);
	assert(sv.edicts[2].e.free);
	assert(clear_reliable_calls == 4U);
	assert(!QCX_RestoreSessionClientPending(&svs.clients[1]));
	assert(!QCX_RestoreSessionClientWaiting(&svs.clients[0]));
	assert(svs.clients[1].spectator);
	assert(strcmp(svs.clients[1].team, "blue") == 0);
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		assert(svs.clients[1].spawn_parms[index] == 100.0f + (float)index);
	}
	assert((sv.paused & SV_PAUSE_RESTORE) == 0);
	assert(last_pause_reason == SV_PAUSE_RESTORE && !last_pause_active && last_pause_notify);
}

static void test_manual_continuation_keeps_a_bound_client_identity(void)
{
	qcx_save_roster_entry_t entry = saved(1U, "Alice");
	uint32_t index;
	reset_fixture();
	connect_client(1U, "Alice", 1);
	set_client_role_and_team(&svs.clients[1], true, "blue");
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		svs.clients[1].spawn_parms[index] = 100.0f + (float)index;
		entry.spawn_parms[index] = 200.0f + (float)index;
	}
	install_and_reconcile(&entry, 1U);
	QCX_RestoreSessionContinue();
	QCX_RestoreSessionFrame(102.0);
	assert(!QCX_RestoreSessionWaiting());
	assert(svs.clients[1].spectator);
	assert(strcmp(svs.clients[1].team, "blue") == 0);
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		assert(svs.clients[1].spawn_parms[index] == 100.0f + (float)index);
	}
}

static void test_zero_and_negative_timeout_wait_for_manual_continuation(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	reset_fixture();
	qcx_restore_wait_timeout.value = 0.0f;
	connect_client(0U, "Una", 0);
	install_and_reconcile(&entry, 1U);
	QCX_RestoreSessionFrame(10000.0);
	assert(QCX_RestoreSessionWaiting());
	QCX_RestoreSessionContinue();
	QCX_RestoreSessionFrame(10001.0);
	assert(!QCX_RestoreSessionWaiting());
	assert(clear_reliable_calls == 2U);

	reset_fixture();
	qcx_restore_wait_timeout.value = -1.0f;
	connect_client(0U, "Una", 0);
	install_and_reconcile(&entry, 1U);
	QCX_RestoreSessionFrame(10000.0);
	assert(QCX_RestoreSessionWaiting());
	assert(qcx_restore_wait_timeout.value == 0.0f);
}

static void test_all_active_completes_without_abandonment(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	reset_fixture();
	connect_client(1U, "Alice", 1);
	install_and_reconcile(&entry, 1U);
	assert(QCX_RestoreSessionCommitBegin(&svs.clients[1], NULL));
	QCX_RestoreSessionFrame(102.0);
	assert(!QCX_RestoreSessionWaiting());
	assert(game_disconnect_calls == 0U);
	assert((sv.paused & SV_PAUSE_RESTORE) == 0);
}

static void test_manual_pause_survives_restore_completion(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	reset_fixture();
	SV_SetPauseReason(SV_PAUSE_MANUAL, true, NULL, false);
	connect_client(0U, "Una", 0);
	install_and_reconcile(&entry, 1U);
	assert((sv.paused & (SV_PAUSE_MANUAL | SV_PAUSE_RESTORE))
		== (SV_PAUSE_MANUAL | SV_PAUSE_RESTORE));
	QCX_RestoreSessionContinue();
	QCX_RestoreSessionFrame(102.0);
	assert(sv.paused == SV_PAUSE_MANUAL);
}

static void test_cancelling_a_bound_identity_keeps_its_connection_role(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	uint32_t index;
	reset_fixture();
	connect_client(1U, "Alice", 1);
	set_client_role_and_team(&svs.clients[1], true, "blue");
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		svs.clients[1].spawn_parms[index] = 100.0f + (float)index;
	}
	install_and_reconcile(&entry, 1U);
	assert(QCX_RestoreSessionClientPending(&svs.clients[1]));
	assert(svs.clients[1].spectator);
	QCX_RestoreSessionCancel();
	assert(!QCX_RestoreSessionWaiting());
	assert(!QCX_RestoreSessionClientPending(&svs.clients[1]));
	assert(!QCX_RestoreSessionClientWaiting(&svs.clients[1]));
	assert(svs.clients[1].qcx_restore_roster_index == -1);
	assert(svs.clients[1].spectator);
	assert(strcmp(svs.clients[1].team, "blue") == 0);
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		assert(svs.clients[1].spawn_parms[index] == 100.0f + (float)index);
	}
	assert((sv.paused & SV_PAUSE_RESTORE) == 0);
}

static void test_active_cancel_keeps_committed_identity_and_changed_spawn_parms(void)
{
	qcx_save_roster_entry_t entries[] = {saved(1U, "Alice"), saved(2U, "Bob")};
	qcx_restore_session_status_t status;
	client_t *client;
	uint32_t index;
	reset_fixture();
	strcpy(entries[0].team, "red");
	client = connect_client(1U, "Alice", 1);
	set_client_role_and_team(client, true, "blue");
	install_and_reconcile(entries, 2U);
	assert(QCX_RestoreSessionCommitBegin(client, NULL));
	client->state = cs_spawned;
	QCX_RestoreSessionFrame(102.0);
	QCX_RestoreSessionGetStatus(&status, 102.0);
	assert(status.waiting && status.active_count == 1U && status.available_count == 1U);
	/* SV_SaveSpawnparms runs before cancellation during a map transition. */
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		client->spawn_parms[index] = 300.0f + (float)index;
	}
	client->qcx_restore_fallback_allowed = false;
	clear_reliable_calls = 0U;
	QCX_RestoreSessionCancel();
	assert(client->state == cs_spawned && !client->spectator);
	assert(strcmp(client->team, "red") == 0);
	assert(strcmp(Info_Get(&client->_userinfo_ctx_, "team"), "red") == 0);
	assert(strcmp(Info_Get(&client->_userinfoshort_ctx_, "team"), "red") == 0);
	assert(strcmp(Info_Get(&client->_userinfo_ctx_, "*spectator"), "") == 0);
	assert(strcmp(Info_Get(&client->_userinfoshort_ctx_, "*spectator"), "") == 0);
	for (index = 0U; index < NUM_SPAWN_PARMS; ++index) {
		assert(client->spawn_parms[index] == 300.0f + (float)index);
	}
	assert(client->qcx_restore_roster_index == -1);
	assert(!client->qcx_restore_pending && !client->qcx_restore_waiting);
	assert(drop_calls == 0U && clear_reliable_calls == 0U);
	assert(game_disconnect_calls == 0U);
	QCX_RestoreSessionGetStatus(&status, 10000.0);
	assert(!status.waiting && status.remaining_seconds == 0.0);
	assert((sv.paused & SV_PAUSE_RESTORE) == 0);
}

static void test_fresh_fallback_on_abandonment(qbool allowed, int ending)
{
	qcx_save_roster_entry_t entry = saved(1U, "Alice");
	qcx_save_image_t image;
	client_t *client;
	reset_fixture();
	entry.role = QCX_SAVE_ROLE_SPECTATOR;
	image = make_image(&entry, 1U);
	assert(QCX_RestoreSessionInstall(&image, 100.0));
	QCX_RestoreSessionFrame(101.0);
	client = connect_client(1U, "Alice", 1);
	set_client_role_and_team(client, false, "blue");
	client->spawn_parms[0] = 123.0f;
	client->qcx_restore_fallback_allowed = allowed;
	QCX_RestoreSessionObserveClient(client);
	QCX_RestoreSessionFrame(102.0);
	assert(QCX_RestoreSessionClientPending(client));
	clear_reliable_calls = reliable_reconnect_requests = qcx_signon_starts = 0U;
	if (ending == 0) QCX_RestoreSessionCancel();
	else {
		if (ending == 1) QCX_RestoreSessionContinue();
		QCX_RestoreSessionFrame(ending == 1 ? 103.0 : 160.0);
	}
	assert(!QCX_RestoreSessionWaiting());
	assert(!client->qcx_restore_pending && !client->qcx_restore_waiting);
	assert(client->qcx_restore_roster_index == -1);
	assert(!client->spectator && strcmp(client->team, "blue") == 0);
	assert(client->spawn_parms[0] == 123.0f);
	assert(drop_calls == (allowed ? 0U : 1U));
	assert(client->state == (allowed ? cs_connected : cs_zombie));
	assert(reliable_reconnect_requests == (allowed ? 1U : 0U));
	if (ending != 0) {
		assert(game_disconnect_calls == 1U && game_disconnect_spectator);
		assert(sv.edicts[2].e.free);
	}
}

static void test_carried_client_has_ordinary_fallback(void)
{
	const qcx_save_roster_entry_t entry = saved(1U, "Alice");
	client_t *client;
	reset_fixture();
	client = connect_client(1U, "Alice", 1);
	client->state = cs_spawned;
	/* This connection committed an authorized identity in a previous restore. */
	client->qcx_restore_fallback_allowed = false;
	install_and_reconcile(&entry, 1U);
	QCX_RestoreSessionContinue();
	QCX_RestoreSessionFrame(102.0);
	assert(client->state == cs_connected);
	assert(drop_calls == 0U);
	assert(reliable_reconnect_requests == 2U);
}

static void test_abandonment_frees_only_nonactive_saved_edicts(void)
{
	qcx_save_roster_entry_t entries[] = {
		saved(0U, "Alice"), saved(1U, "Bob"), saved(2U, "Carol")};
	reset_fixture();
	entries[1].spawned = 0U;
	entries[2].role = QCX_SAVE_ROLE_SPECTATOR;
	connect_client(0U, "Alice", 1);
	connect_client(1U, "Bob", 2);
	install_and_reconcile(entries, 3U);
	assert(QCX_RestoreSessionCommitBegin(&svs.clients[0], NULL));
	svs.clients[0].state = cs_spawned;
	svs.clients[0].qcx_restore_fallback_allowed = false;
	sv_client = &svs.clients[0];
	sv_player = &sv.edicts[1];
	QCX_RestoreSessionContinue();
	QCX_RestoreSessionFrame(102.0);
	assert(!sv.edicts[1].e.free);
	assert(sv.edicts[2].e.free && sv.edicts[3].e.free);
	assert(game_disconnect_calls == 1U && game_disconnect_spectator);
	assert(svs.clients[0].state == cs_spawned && drop_calls == 0U);
	assert(sv_client == &svs.clients[0] && sv_player == &sv.edicts[1]);
	QCX_RestoreSessionFrame(200.0);
	assert(game_disconnect_calls == 1U);
}

int main(void)
{
	test_initial_reconciliation_queues_fresh_signon_after_matching();
	test_moves_to_a_free_saved_slot_and_repairs_globals();
	test_two_saved_clients_can_cycle();
	test_matched_client_displaces_an_unmatched_client();
	test_unmatched_client_is_evacuated_from_available_reservation();
	test_reverse_reconnect_order_is_a_permutation();
	test_duplicate_waiting_names_claim_the_lowest_slot();
	test_admission_does_not_consume_reserved_slots();
	test_admission_uses_the_saved_identity_role();
	test_bound_client_without_fallback_is_dropped_on_continue();
	test_bound_client_without_fallback_capacity_is_dropped_on_continue();
	test_bound_identity_keeps_connection_owned_state();
	test_bound_identity_exposes_saved_role_for_signon();
	test_begin_commits_saved_identity(false, true);
	test_begin_commits_saved_identity(true, true);
	test_begin_commits_saved_identity(false, false);
	test_begin_commits_saved_identity(true, false);
	test_begin_rejects_full_userinfo_without_partial_commit(false, true);
	test_begin_rejects_full_userinfo_without_partial_commit(true, true);
	test_begin_rejects_full_userinfo_without_partial_commit(false, false);
	test_begin_rejects_full_userinfo_without_partial_commit(true, false);
	test_begin_can_replace_team_with_spectator_in_full_userinfo();
	test_roster_list_prints_only_available_saved_identities();
	test_bound_client_preserves_its_edict_through_begin_and_drop();
	test_unspawned_saved_client_uses_the_ordinary_spawn_path();
	test_fresh_observed_client_waits_before_reconciliation();
	test_active_identity_cannot_claim_another_saved_slot();
	test_bound_drop_reopens_the_saved_identity();
	test_late_name_claim_restarts_signon_at_the_safe_frame();
	test_name_change_is_reconciled_only_at_the_safe_frame();
	test_excess_unmatched_client_is_dropped_from_highest_slot();
	test_inactive_and_complete_frames_do_not_compare_names();
	test_timeout_abandons_bound_and_waiting_clients();
	test_manual_continuation_keeps_a_bound_client_identity();
	test_zero_and_negative_timeout_wait_for_manual_continuation();
	test_all_active_completes_without_abandonment();
	test_manual_pause_survives_restore_completion();
	test_cancelling_a_bound_identity_keeps_its_connection_role();
	test_active_cancel_keeps_committed_identity_and_changed_spawn_parms();
	test_fresh_fallback_on_abandonment(false, 0);
	test_fresh_fallback_on_abandonment(true, 0);
	test_fresh_fallback_on_abandonment(false, 1);
	test_fresh_fallback_on_abandonment(true, 1);
	test_fresh_fallback_on_abandonment(false, 2);
	test_fresh_fallback_on_abandonment(true, 2);
	test_carried_client_has_ordinary_fallback();
	test_abandonment_frees_only_nonactive_saved_edicts();
	reset_fixture();
	return 0;
}
