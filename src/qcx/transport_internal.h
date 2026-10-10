#ifndef MVDSV_QC2CPP_TRANSPORT_INTERNAL_H
#define MVDSV_QC2CPP_TRANSPORT_INTERNAL_H

#include "qcx/transport.h"

qcx_plugin_status_t QCX_NativeOpen(const char *gamedir, const char *basename,
	const qcx_host_api_v1_t *host, void **handle, qcx_game_api_v1_t *game,
	qcx_program_diagnostic_v1_t *diagnostic);
void QCX_NativeClose(void *handle);
qcx_plugin_status_t QCX_WasmOpen(const char *gamedir, const char *basename,
	const qcx_host_api_v1_t *host, void **handle, qcx_game_api_v1_t *game,
	qcx_program_diagnostic_v1_t *diagnostic);
void QCX_WasmClose(void *handle);

void QCX_TransportDiagnostic(qcx_program_diagnostic_v1_t *diagnostic,
	qcx_plugin_status_t status, const char *message);

#endif
