#include "qwsvdef.h"

#include "progs.h"
#include "qc2cpp/adapter.h"
#include "qc2cpp/adapter_state.h"
#include "qc2cpp/transport.h"

static qc_transport_t *qc_transport;
static qc_adapter_state_t qc_state;

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
	QC_AdapterStateSelect(&qc_state, (int)sv_progtype.value);
	const qc_plugin_status_t status = QC_TransportOpen((int)sv_progtype.value,
		fs_gamedir, sv_progsname.string, NULL, &qc_transport, &diagnostic);
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
	QC_AdapterStateEnter(&qc_state);
	(void)game->init(game->context, (int32_t)(sv.time * 1000.0), (uint32_t)time(NULL));
	QC_AdapterStateLeave(&qc_state);
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
	QC_TransportClose(qc_transport);
	qc_transport = NULL;
	QC_AdapterStateReset(&qc_state);
}

void QC_Unpublish(void *context)
{
	(void)context;
}

void QC_Fatal(void *context, const qc_program_diagnostic_v1_t *diagnostic)
{
	(void)context;
	SV_Error("qc2cpp fatal: %.*s", diagnostic == NULL ? 0 : (int)diagnostic->message_size,
		diagnostic == NULL ? "" : diagnostic->message);
}
