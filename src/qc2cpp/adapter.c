#include "qwsvdef.h"

#include "progs.h"
#include "qc2cpp/adapter.h"
#include "qc2cpp/adapter_state.h"
#include "qc2cpp/entities.h"
#include "qc2cpp/globals.h"
#include "qc2cpp/services.h"
#if defined(MVDSV_QC2CPP_TESTS)
#include "qc2cpp/test_observer.h"
#endif
#include "qc2cpp/transport.h"

#include <string.h>

static qc_transport_t *qc_transport;
static qc_adapter_state_t qc_state;
static qc_host_api_v1_t qc_host;

qbool QC_Active(void)
{
	return QC_AdapterStateActive(&qc_state);
}

const qc_game_api_v1_t *QC_Game(void)
{
	return qc_transport == NULL ? NULL : QC_TransportGame(qc_transport);
}

void QC_LoadProgs(void)
{
	qc_program_diagnostic_v1_t diagnostic = {0};
	qc_host = (qc_host_api_v1_t){
		.abi_version = QC_PLUGIN_ABI_VERSION_V1,
		.struct_size = sizeof(qc_host),
		.unpublish = QC_Unpublish,
		.fatal = QC_Fatal,
	};
	QC_BindWorldServices(&qc_host);
	QC_BindMovementServices(&qc_host);
	QC_BindNetworkServices(&qc_host);
	QC_BindUnavailableServices(&qc_host);
	QC_AdapterStateSelect(&qc_state, (int)sv_progtype.value);
	const qc_plugin_status_t status = QC_TransportOpen((int)sv_progtype.value,
		fs_gamedir, sv_progsname.string, &qc_host, &qc_transport, &diagnostic);
	if (status != QC_PLUGIN_OK) {
		SV_Error("qc2cpp mode %d failed to load %s: %.*s", (int)sv_progtype.value,
			sv_progsname.string, (int)diagnostic.message_size, diagnostic.message);
	}
}

void QC_InitProg(void)
{
	const qc_game_api_v1_t *game = QC_Game();
	if (game == NULL) {
		SV_Error("qc2cpp mode %d has no active game", qc_state.mode);
	}
#if defined(MVDSV_QC2CPP_TESTS)
	QC_TestObserverInitBegin();
#endif
	QC_AdapterStateEnter(&qc_state);
	const qc_guest_address_t entity_publication = game->init(game->context,
		(int32_t)(sv.time * 1000.0), (uint32_t)time(NULL) & 0x00ffffffU);
	QC_AdapterStateLeave(&qc_state);
#if defined(MVDSV_QC2CPP_TESTS)
	QC_TestObserverInitEnd();
#endif
	if (!QC_ConfigureEntities(entity_publication) || !QC_BindEntities()) {
		SV_Error("qc2cpp game did not publish compatible entity storage");
	}
	if (!QC_ConfigureGlobals(deathmatch.value, coop.value, teamplay.value)) {
		SV_Error("qc2cpp game did not publish valid shared globals");
	}
}

void QC_Shutdown(void)
{
	const qc_game_api_v1_t *game = QC_Game();
	if (game != NULL && QC_AdapterStateIdle(&qc_state)) {
		QC_AdapterStateEnter(&qc_state);
		game->shutdown(game->context);
		QC_AdapterStateLeave(&qc_state);
	}
}

void QC_UnloadProgs(void)
{
	if (!QC_AdapterStateIdle(&qc_state)) {
		return;
	}
	QC_ClearGlobals();
	QC_ClearEntities();
	QC_TransportClose(qc_transport);
	qc_transport = NULL;

#if defined(MVDSV_QC2CPP_TESTS)
	QC_TestObserverNormalUnpublish();
#endif
	QC_AdapterStateReset(&qc_state);
}

void QC_LoadEntities(const char *data)
{
	const qc_game_api_v1_t *game = QC_Game();
	if (game == NULL || data == NULL) {
		SV_Error("qc2cpp game cannot load map entities");
	}
	QC_AdapterStateEnter(&qc_state);
	game->load_entities(game->context, (const uint8_t *)data,
		(qc_byte_count_t)strlen(data));
	QC_AdapterStateLeave(&qc_state);
}

void QC_StartFrame(float time, float frametime, qbool is_bot_frame)
{
	const qc_game_api_v1_t *game = QC_Game();
	if (game == NULL) {
		SV_Error("qc2cpp game cannot start a frame");
	}
#if defined(MVDSV_QC2CPP_TESTS)
	QC_TestObserverStartFrame();
#endif
	QC_AdapterStateEnter(&qc_state);
	game->start_frame(game->context, time, frametime, is_bot_frame ? 1U : 0U);
	QC_AdapterStateLeave(&qc_state);
}

void QC_EdictTouch(qc_entity_id_t touched, qc_entity_id_t toucher, float time,
	float frametime)
{
	const qc_game_api_v1_t *game = QC_Game();
	if (game == NULL) {
		SV_Error("qc2cpp game cannot dispatch edict touch");
	}
	QC_AdapterStateEnter(&qc_state);
	game->edict_touch(game->context, touched, toucher, time, frametime);
	QC_AdapterStateLeave(&qc_state);
}

void QC_EdictThink(qc_entity_id_t self, float time, float frametime)
{
	const qc_game_api_v1_t *game = QC_Game();
	if (game == NULL) {
		SV_Error("qc2cpp game cannot dispatch edict think");
	}
	QC_AdapterStateEnter(&qc_state);
	game->edict_think(game->context, self, time, frametime);
	QC_AdapterStateLeave(&qc_state);
}

void QC_EdictBlocked(qc_entity_id_t pusher, qc_entity_id_t obstacle, float time,
	float frametime)
{
	const qc_game_api_v1_t *game = QC_Game();
	if (game == NULL) {
		SV_Error("qc2cpp game cannot dispatch edict blocked");
	}
	QC_AdapterStateEnter(&qc_state);
	game->edict_blocked(game->context, pusher, obstacle, time, frametime);
	QC_AdapterStateLeave(&qc_state);
}

static const qc_game_api_v1_t *QC_RequireGame(const char *entry)
{
	const qc_game_api_v1_t *const game = QC_Game();
	if (game == NULL) {
		SV_Error("qc2cpp game cannot dispatch %s", entry);
	}
	return game;
}

#define QC_CALL_CLIENT(entry, invocation) \
	do { \
		const qc_game_api_v1_t *const game = QC_RequireGame(entry); \
		QC_AdapterStateEnter(&qc_state); \
		invocation; \
		QC_AdapterStateLeave(&qc_state); \
	} while (0)

void QC_ClientConnect(qc_entity_id_t self, uint32_t spectator)
{
	QC_CALL_CLIENT("client connect", game->client_connect(game->context, self, spectator));
}

void QC_PutClientInServer(qc_entity_id_t self, uint32_t spectator)
{
	QC_CALL_CLIENT("put client in server", game->put_client_in_server(game->context, self, spectator));
}

void QC_ClientDisconnect(qc_entity_id_t self, uint32_t spectator)
{
	QC_CALL_CLIENT("client disconnect", game->client_disconnect(game->context, self, spectator));
}

uint32_t QC_ClientUserInfoChanged(qc_entity_id_t self, uint32_t after)
{
	const qc_game_api_v1_t *const game = QC_RequireGame("client userinfo changed");
	QC_AdapterStateEnter(&qc_state);
	const uint32_t result = game->client_userinfo_changed(game->context, self, after);
	QC_AdapterStateLeave(&qc_state);
	return result;
}

uint32_t QC_ClientCommand(qc_entity_id_t self)
{
	const qc_game_api_v1_t *const game = QC_RequireGame("client command");
	QC_AdapterStateEnter(&qc_state);
	const uint32_t result = game->client_command(game->context, self);
	QC_AdapterStateLeave(&qc_state);
	return result;
}

void QC_ClientKill(qc_entity_id_t self)
{
	QC_CALL_CLIENT("client kill", game->client_kill(game->context, self));
}

uint32_t QC_ClientSay(qc_entity_id_t self, uint32_t team, const uint8_t *text,
	qc_byte_count_t size)
{
	const qc_game_api_v1_t *const game = QC_RequireGame("client say");
	QC_AdapterStateEnter(&qc_state);
	const uint32_t result = game->client_say(game->context, self, team, text, size);
	QC_AdapterStateLeave(&qc_state);
	return result;
}

void QC_ClientPreThink(qc_entity_id_t self, float time, float frametime,
	uint32_t spectator)
{
	QC_CALL_CLIENT("client prethink", game->client_prethink(game->context, self,
		time, frametime, spectator));
}

void QC_ClientPostThink(qc_entity_id_t self, float time, uint32_t spectator)
{
	const qc_game_api_v1_t *const game = QC_RequireGame("client postthink");
	const qc_shared_global_state_v1_t *const globals = QC_Globals();
	if (globals == NULL) {
		SV_Error("qc2cpp client postthink has no shared globals");
	}
	QC_AdapterStateEnter(&qc_state);
	if (spectator != 0U) {
		game->spectator_think(game->context, self, time, globals->frametime);
	} else {
		game->client_postthink(game->context, self, time, globals->frametime, 0U);
	}
	QC_AdapterStateLeave(&qc_state);
}

void QC_SetNewParms(float out_parms[16])
{
	QC_CALL_CLIENT("set new parms", game->set_new_parms(game->context, out_parms));
}

void QC_SetChangeParms(qc_entity_id_t self, float out_parms[16])
{
	QC_CALL_CLIENT("set change parms", game->set_change_parms(game->context, self,
		out_parms));
}

void QC_Unpublish(void *context)
{
	(void)context;
	QC_ClearGlobals();
	QC_ClearEntities();
}

void QC_Fatal(void *context, const qc_program_diagnostic_v1_t *diagnostic)
{
	(void)context;
	SV_Error("qc2cpp fatal: %.*s", diagnostic == NULL ? 0 : (int)diagnostic->message_size,
		diagnostic == NULL ? "" : diagnostic->message);
}
