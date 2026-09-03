#include "qcx/transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct qcx_transport {
	qcx_transport_kind_t transport_kind;
	void *handle;
	qcx_game_api_v1_t game;
};

qcx_plugin_status_t QCX_NativeOpen(const char *gamedir, const char *basename,
	const qcx_host_api_v1_t *host, void **handle, qcx_game_api_v1_t *game,
	qcx_program_diagnostic_v1_t *diagnostic);
void QCX_NativeClose(void *handle);
qcx_plugin_status_t QCX_WasmOpen(const char *gamedir, const char *basename,
	const qcx_host_api_v1_t *host, void **handle, qcx_game_api_v1_t *game,
	qcx_program_diagnostic_v1_t *diagnostic);
void QCX_WasmClose(void *handle);

void QCX_TransportDiagnostic(qcx_program_diagnostic_v1_t *diagnostic,
	qcx_plugin_status_t status, const char *message)
{
	if (diagnostic == NULL) {
		return;
	}
	diagnostic->code = status;
	diagnostic->flags = QCX_PROGRAM_DIAGNOSTIC_V1_FLAG_STRUCTURED;
	diagnostic->message_size = (uint32_t)snprintf(
		diagnostic->message, sizeof(diagnostic->message), "%s", message);
	if (diagnostic->message_size >= sizeof(diagnostic->message)) {
		diagnostic->message_size = sizeof(diagnostic->message) - 1U;
		diagnostic->flags |= QCX_PROGRAM_DIAGNOSTIC_V1_FLAG_TRUNCATED;
	}
}

static int QCX_TransportBasenameIsValid(const char *basename)
{
	return basename != NULL && basename[0] != '\0'
		&& strcmp(basename, ".") != 0 && strcmp(basename, "..") != 0
		&& strchr(basename, '/') == NULL && strchr(basename, '\\') == NULL;
}

static int QCX_TransportGameDirIsAbsolute(const char *gamedir)
{
#if defined(_WIN32)
	return gamedir != NULL && ((gamedir[0] == '/' || gamedir[0] == '\\')
		|| (gamedir[0] != '\0' && gamedir[1] == ':'));
#else
	return gamedir != NULL && gamedir[0] == '/';
#endif
}

qcx_plugin_status_t QCX_TransportOpen(qcx_transport_kind_t transport_kind, const char *gamedir,
                                    const char *basename,
                                    const qcx_host_api_v1_t *host,
                                    qcx_transport_t **out,
                                    qcx_program_diagnostic_v1_t *diagnostic)
{
	(void)host;
	if (out != NULL) {
		*out = NULL;
	}
	if (diagnostic != NULL) {
		memset(diagnostic, 0, sizeof(*diagnostic));
	}
	if (out == NULL || (transport_kind != QCX_TRANSPORT_NATIVE
		&& transport_kind != QCX_TRANSPORT_WASM)) {
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	if (!QCX_TransportGameDirIsAbsolute(gamedir) || !QCX_TransportBasenameIsValid(basename)) {
		QCX_TransportDiagnostic(diagnostic, QCX_PLUGIN_BAD_ARGUMENT,
			"qc2cpp transport needs a game directory and artifact basename");
		return QCX_PLUGIN_BAD_ARGUMENT;
	}

	qcx_transport_t *transport = calloc(1U, sizeof(*transport));
	if (transport == NULL) {
		QCX_TransportDiagnostic(diagnostic, QCX_PLUGIN_UNAVAILABLE,
			"qc2cpp transport allocation failed");
		return QCX_PLUGIN_UNAVAILABLE;
	}
	qcx_plugin_status_t status;
	if (transport_kind == QCX_TRANSPORT_NATIVE) {
		status = QCX_NativeOpen(gamedir, basename, host, &transport->handle,
			&transport->game, diagnostic);
	} else {
		status = QCX_WasmOpen(gamedir, basename, host, &transport->handle,
			&transport->game, diagnostic);
	}
	if (status != QCX_PLUGIN_OK) {
		free(transport);
		return status;
	}
	transport->transport_kind = transport_kind;
	*out = transport;
	return QCX_PLUGIN_OK;
}

const qcx_game_api_v1_t *QCX_TransportGame(qcx_transport_t *transport)
{
	return transport == NULL ? NULL : &transport->game;
}

void QCX_TransportClose(qcx_transport_t *transport)
{
	if (transport == NULL) {
		return;
	}
	if (transport->transport_kind == QCX_TRANSPORT_NATIVE) {
		QCX_NativeClose(transport->handle);
	} else if (transport->transport_kind == QCX_TRANSPORT_WASM) {
		QCX_WasmClose(transport->handle);
	}
	free(transport);
}
