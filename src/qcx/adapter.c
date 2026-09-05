#include "qwsvdef.h"

#include "progs.h"
#include "qcx/adapter.h"
#include "qcx/adapter_state.h"
#include "qcx/entities.h"
#include "qcx/globals.h"
#include "qcx/services.h"
#include "qcx/save.h"
#include "qcx/strings.h"
#if defined(QCX_TESTS)
#include "qcx/test_observer.h"
#endif
#include "qcx/transport.h"

#include <stdlib.h>
#include <string.h>

static qcx_transport_t *qcx_transport;
static qcx_adapter_state_t qcx_state;
static qcx_host_api_v1_t qcx_host;
/* True once the host accepts the first coherent QCX publication. Host
 * entity/global views may still be unbound or partially bound; terminal
 * startup cleanup must therefore remain safe throughout PR2 binding. */
static qbool qcx_published;

static const char *QCX_TransportName(qcx_transport_kind_t transport_kind)
{
	return transport_kind == QCX_TRANSPORT_NATIVE ? "native"
		: transport_kind == QCX_TRANSPORT_WASM ? "Wasm" : "unknown";
}

qbool QCX_Active(void)
{
	return QCX_AdapterStateActive(&qcx_state);
}

const qcx_game_api_v1_t *QCX_Game(void)
{
	return qcx_transport == NULL ? NULL : QCX_TransportGame(qcx_transport);
}

void QCX_LoadProgs(qcx_transport_kind_t transport_kind)
{
	qcx_program_diagnostic_v1_t diagnostic = {0};
	qcx_host = (qcx_host_api_v1_t){
		.abi_version = QCX_PLUGIN_ABI_VERSION_V1,
		.struct_size = sizeof(qcx_host),
		.unpublish = QCX_Unpublish,
		.fatal = QCX_Fatal,
	};
	QCX_BindWorldServices(&qcx_host);
	QCX_BindMovementServices(&qcx_host);
	QCX_BindNetworkServices(&qcx_host);
	qcx_published = false;
	QCX_AdapterStateSelect(&qcx_state, transport_kind);
	const qcx_plugin_status_t status = QCX_TransportOpen(transport_kind,
		fs_gamedir, sv_progsname.string, &qcx_host, &qcx_transport, &diagnostic);
	if (status != QCX_PLUGIN_OK) {
		SV_Error("QCX %s transport failed to load %s: %.*s", QCX_TransportName(transport_kind),
			sv_progsname.string, (int)diagnostic.message_size, diagnostic.message);
	}
}

void QCX_InitProg(void)
{
	const qcx_game_api_v1_t *game = QCX_Game();
	if (game == NULL) {
		SV_Error("QCX %s transport has no active game", QCX_TransportName(qcx_state.transport_kind));
	}
#if defined(QCX_TESTS)
	QCX_TestObserverInitBegin();
#endif
	QCX_AdapterStateEnter(&qcx_state);
	const qcx_guest_address_t entity_publication = game->init(game->context,
		(int32_t)(sv.time * 1000.0), (uint32_t)time(NULL) & 0x00ffffffU);
	QCX_AdapterStateLeave(&qcx_state);
#if defined(QCX_TESTS)
	QCX_TestObserverInitEnd();
#endif
	if (!QCX_ConfigureEntities(entity_publication)) {
		SV_Error("qc2cpp game did not publish compatible entity storage");
	}
	qcx_published = true;
}

void QCX_Shutdown(void)
{
	if (QCX_Active() && sv_error) {
		return;
	}
	const qcx_game_api_v1_t *game = QCX_Game();
	if (game != NULL && QCX_AdapterStateIdle(&qcx_state)) {
		QCX_AdapterStateEnter(&qcx_state);
		game->shutdown(game->context);
		QCX_AdapterStateLeave(&qcx_state);
	}
}

void QCX_UnloadProgs(void)
{
	if (QCX_Active() && sv_error) {
		return;
	}
	if (!QCX_AdapterStateIdle(&qcx_state)) {
		return;
	}
	QCX_ClearLegacyStringBorrows();
	QCX_ClearGlobals();
	QCX_ClearEntities();
	qcx_published = false;
	QCX_SaveInvalidateConnectedSnapshot();
	QCX_TransportClose(qcx_transport);
	qcx_transport = NULL;

#if defined(QCX_TESTS)
	QCX_TestObserverNormalUnpublish();
#endif
	QCX_AdapterStateReset(&qcx_state);
}

void QCX_LoadEntities(const char *data)
{
	const qcx_game_api_v1_t *game = QCX_Game();
	if (game == NULL || data == NULL) {
		SV_Error("qc2cpp game cannot load map entities");
	}
	QCX_AdapterStateEnter(&qcx_state);
	game->load_entities(game->context, (const uint8_t *)data,
		(qcx_byte_count_t)strlen(data));
	QCX_AdapterStateLeave(&qcx_state);
}

void QCX_StartFrame(float time, float frametime, qbool is_bot_frame)
{
	const qcx_game_api_v1_t *game = QCX_Game();
	if (game == NULL) {
		SV_Error("qc2cpp game cannot start a frame");
	}
#if defined(QCX_TESTS)
	QCX_TestObserverStartFrame();
#endif
	QCX_AdapterStateEnter(&qcx_state);
	game->start_frame(game->context, time, frametime, is_bot_frame ? 1U : 0U);
	QCX_AdapterStateLeave(&qcx_state);
}

void QCX_EdictTouch(qcx_entity_id_t touched, qcx_entity_id_t toucher, float time,
	float frametime)
{
	const qcx_game_api_v1_t *game = QCX_Game();
	if (game == NULL) {
		SV_Error("qc2cpp game cannot dispatch edict touch");
	}
	QCX_AdapterStateEnter(&qcx_state);
	game->edict_touch(game->context, touched, toucher, time, frametime);
	QCX_AdapterStateLeave(&qcx_state);
}

void QCX_EdictThink(qcx_entity_id_t self, float time, float frametime)
{
	const qcx_game_api_v1_t *game = QCX_Game();
	if (game == NULL) {
		SV_Error("qc2cpp game cannot dispatch edict think");
	}
	QCX_AdapterStateEnter(&qcx_state);
	game->edict_think(game->context, self, time, frametime);
	QCX_AdapterStateLeave(&qcx_state);
}

void QCX_EdictBlocked(qcx_entity_id_t pusher, qcx_entity_id_t obstacle, float time,
	float frametime)
{
	const qcx_game_api_v1_t *game = QCX_Game();
	if (game == NULL) {
		SV_Error("qc2cpp game cannot dispatch edict blocked");
	}
	QCX_AdapterStateEnter(&qcx_state);
	game->edict_blocked(game->context, pusher, obstacle, time, frametime);
	QCX_AdapterStateLeave(&qcx_state);
}

static const qcx_game_api_v1_t *QCX_RequireGame(const char *entry)
{
	const qcx_game_api_v1_t *const game = QCX_Game();
	if (game == NULL) {
		SV_Error("qc2cpp game cannot dispatch %s", entry);
	}
	return game;
}

#define QCX_CALL_CLIENT(entry, invocation) \
	do { \
		const qcx_game_api_v1_t *const game = QCX_RequireGame(entry); \
		QCX_AdapterStateEnter(&qcx_state); \
		invocation; \
		QCX_AdapterStateLeave(&qcx_state); \
	} while (0)

void QCX_ClientConnect(qcx_entity_id_t self, uint32_t spectator)
{
	QCX_CALL_CLIENT("client connect", game->client_connect(game->context, self, spectator));
	QCX_SaveInvalidateConnectedSnapshot();
#if defined(QCX_TESTS)
	QCX_TestObserverClientConnect(self);
#endif
}

void QCX_PutClientInServer(qcx_entity_id_t self, uint32_t spectator)
{
	QCX_CALL_CLIENT("put client in server", game->put_client_in_server(game->context, self, spectator));
#if defined(QCX_TESTS)
	QCX_TestObserverPutClientInServer(self, spectator);
#endif
}

void QCX_ClientDisconnect(qcx_entity_id_t self, uint32_t spectator)
{
	QCX_CALL_CLIENT("client disconnect", game->client_disconnect(game->context, self, spectator));
	QCX_SaveInvalidateConnectedSnapshot();
#if defined(QCX_TESTS)
	QCX_TestObserverClientDisconnect();
#endif
}

uint32_t QCX_ClientUserInfoChanged(qcx_entity_id_t self, uint32_t after)
{
	const qcx_game_api_v1_t *const game = QCX_RequireGame("client userinfo changed");
	QCX_AdapterStateEnter(&qcx_state);
	const uint32_t result = game->client_userinfo_changed(game->context, self, after);
	QCX_AdapterStateLeave(&qcx_state);
	return result;
}

uint32_t QCX_ClientCommand(qcx_entity_id_t self)
{
	const qcx_game_api_v1_t *const game = QCX_RequireGame("client command");
	QCX_AdapterStateEnter(&qcx_state);
	const uint32_t result = game->client_command(game->context, self);
	QCX_AdapterStateLeave(&qcx_state);
	return result;
}

void QCX_ClientKill(qcx_entity_id_t self)
{
	QCX_CALL_CLIENT("client kill", game->client_kill(game->context, self));
#if defined(QCX_TESTS)
	QCX_TestObserverClientKill(self);
#endif
}

uint32_t QCX_ClientSay(qcx_entity_id_t self, uint32_t team, const uint8_t *text,
	qcx_byte_count_t size)
{
	const qcx_game_api_v1_t *const game = QCX_RequireGame("client say");
	QCX_AdapterStateEnter(&qcx_state);
	const uint32_t result = game->client_say(game->context, self, team, text, size);
	QCX_AdapterStateLeave(&qcx_state);
	return result;
}

void QCX_ClientPreThink(qcx_entity_id_t self, float time, float frametime,
	uint32_t spectator)
{
	QCX_CALL_CLIENT("client prethink", game->client_prethink(game->context, self,
		time, frametime, spectator));
#if defined(QCX_TESTS)
	QCX_TestObserverClientPreThink();
#endif
}

void QCX_ClientPostThink(qcx_entity_id_t self, float time, uint32_t spectator)
{
	const qcx_game_api_v1_t *const game = QCX_RequireGame("client postthink");
	const qcx_shared_global_state_v1_t *const globals = QCX_Globals();
	if (globals == NULL) {
		SV_Error("qc2cpp client postthink has no shared globals");
	}
	QCX_AdapterStateEnter(&qcx_state);
	if (spectator != 0U) {
		game->spectator_think(game->context, self, time, globals->frametime);
	} else {
		game->client_postthink(game->context, self, time, globals->frametime, 0U);
	}
	QCX_AdapterStateLeave(&qcx_state);
#if defined(QCX_TESTS)
	QCX_TestObserverClientPostThink(self, spectator);
#endif
}

void QCX_SetNewParms(float out_parms[16])
{
	QCX_CALL_CLIENT("set new parms", game->set_new_parms(game->context, out_parms));
}

void QCX_SetChangeParms(qcx_entity_id_t self, float out_parms[16])
{
	QCX_CALL_CLIENT("set change parms", game->set_change_parms(game->context, self,
		out_parms));
}

qcx_restore_status_t QCX_SetSaveSelection(const uint8_t *bitmap, qcx_byte_count_t size)
{
	const qcx_game_api_v1_t *const game = QCX_RequireGame("set save selection");
	QCX_AdapterStateEnter(&qcx_state);
	const qcx_restore_status_t result = game->set_save_selection(game->context, bitmap, size);
	QCX_AdapterStateLeave(&qcx_state);
	return result;
}

qcx_byte_count_t QCX_SaveGuest(uint8_t *out, qcx_byte_count_t capacity)
{
	const qcx_game_api_v1_t *const game = QCX_RequireGame("save");
	QCX_AdapterStateEnter(&qcx_state);
	const qcx_byte_count_t result = game->save(game->context, out, capacity);
	QCX_AdapterStateLeave(&qcx_state);
	return result;
}

qcx_restore_status_t QCX_ValidateGuestRestore(const uint8_t *data, qcx_byte_count_t size,
	const uint8_t *selection, qcx_byte_count_t selection_size)
{
	const qcx_game_api_v1_t *const game = QCX_RequireGame("validate restore");
	QCX_AdapterStateEnter(&qcx_state);
	const qcx_restore_status_t result = game->validate_restore(game->context, data, size,
		selection, selection_size);
	QCX_AdapterStateLeave(&qcx_state);
	return result;
}

qcx_restore_status_t QCX_RestoreGuest(const uint8_t *data, qcx_byte_count_t size)
{
	const qcx_game_api_v1_t *const game = QCX_RequireGame("restore");
	QCX_AdapterStateEnter(&qcx_state);
	const qcx_restore_status_t result = game->restore(game->context, data, size);
	QCX_AdapterStateLeave(&qcx_state);
	return result;
}

void QCX_Unpublish(void *context)
{
	(void)context;
	if (!qcx_published) {
		return;
	}
	QCX_ClearLegacyStringBorrows();
	QCX_ClearGlobals();
	QCX_ClearEntities();
	qcx_published = false;
#if defined(QCX_TESTS)
	QCX_TestObserverTerminalUnpublish();
#endif
}

void QCX_Fatal(void *context, const qcx_program_diagnostic_v1_t *diagnostic)
{
	(void)context;
	QCX_Unpublish(NULL);
	SV_Error("qc2cpp fatal: %.*s", diagnostic == NULL ? 0 : (int)diagnostic->message_size,
		diagnostic == NULL ? "" : diagnostic->message);
	abort();
}
