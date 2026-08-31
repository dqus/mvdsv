#include "qc2cpp/transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct qc_transport {
	int mode;
	void *handle;
	qc_game_api_v1_t game;
};

qc_plugin_status_t QC_NativeOpen(const char *gamedir, const char *basename,
	const qc_host_api_v1_t *host, void **handle, qc_game_api_v1_t *game,
	qc_program_diagnostic_v1_t *diagnostic);
void QC_NativeClose(void *handle);
qc_plugin_status_t QC_WasmOpen(const char *gamedir, const char *basename,
	const qc_host_api_v1_t *host, void **handle, qc_game_api_v1_t *game,
	qc_program_diagnostic_v1_t *diagnostic);
void QC_WasmClose(void *handle);

void QC_TransportDiagnostic(qc_program_diagnostic_v1_t *diagnostic,
	qc_plugin_status_t status, const char *message)
{
	if (diagnostic == NULL) {
		return;
	}
	diagnostic->code = status;
	diagnostic->flags = QC_PROGRAM_DIAGNOSTIC_V1_FLAG_STRUCTURED;
	diagnostic->message_size = (uint32_t)snprintf(
		diagnostic->message, sizeof(diagnostic->message), "%s", message);
	if (diagnostic->message_size >= sizeof(diagnostic->message)) {
		diagnostic->message_size = sizeof(diagnostic->message) - 1U;
		diagnostic->flags |= QC_PROGRAM_DIAGNOSTIC_V1_FLAG_TRUNCATED;
	}
}

static int QC_TransportBasenameIsValid(const char *basename)
{
	return basename != NULL && basename[0] != '\0'
		&& strcmp(basename, ".") != 0 && strcmp(basename, "..") != 0
		&& strchr(basename, '/') == NULL && strchr(basename, '\\') == NULL;
}

static int QC_TransportGameDirIsAbsolute(const char *gamedir)
{
#if defined(_WIN32)
	return gamedir != NULL && ((gamedir[0] == '/' || gamedir[0] == '\\')
		|| (gamedir[0] != '\0' && gamedir[1] == ':'));
#else
	return gamedir != NULL && gamedir[0] == '/';
#endif
}

qc_plugin_status_t QC_TransportOpen(int mode, const char *gamedir,
                                    const char *basename,
                                    const qc_host_api_v1_t *host,
                                    qc_transport_t **out,
                                    qc_program_diagnostic_v1_t *diagnostic)
{
	(void)host;
	if (out != NULL) {
		*out = NULL;
	}
	if (diagnostic != NULL) {
		memset(diagnostic, 0, sizeof(*diagnostic));
	}
	if (out == NULL || (mode != 4 && mode != 5)) {
		return QC_PLUGIN_BAD_ARGUMENT;
	}
	if (!QC_TransportGameDirIsAbsolute(gamedir) || !QC_TransportBasenameIsValid(basename)) {
		QC_TransportDiagnostic(diagnostic, QC_PLUGIN_BAD_ARGUMENT,
			"qc2cpp transport needs a game directory and artifact basename");
		return QC_PLUGIN_BAD_ARGUMENT;
	}

	qc_transport_t *transport = calloc(1U, sizeof(*transport));
	if (transport == NULL) {
		QC_TransportDiagnostic(diagnostic, QC_PLUGIN_UNAVAILABLE,
			"qc2cpp transport allocation failed");
		return QC_PLUGIN_UNAVAILABLE;
	}
	qc_plugin_status_t status;
	if (mode == 4) {
		status = QC_NativeOpen(gamedir, basename, host, &transport->handle,
			&transport->game, diagnostic);
	} else {
		status = QC_WasmOpen(gamedir, basename, host, &transport->handle,
			&transport->game, diagnostic);
	}
	if (status != QC_PLUGIN_OK) {
		free(transport);
		return status;
	}
	transport->mode = mode;
	*out = transport;
	return QC_PLUGIN_OK;
}

const qc_game_api_v1_t *QC_TransportGame(qc_transport_t *transport)
{
	return transport == NULL ? NULL : &transport->game;
}

void QC_TransportClose(qc_transport_t *transport)
{
	if (transport == NULL) {
		return;
	}
	if (transport->mode == 4) {
		QC_NativeClose(transport->handle);
	} else if (transport->mode == 5) {
		QC_WasmClose(transport->handle);
	}
	free(transport);
}
