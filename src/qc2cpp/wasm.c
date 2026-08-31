#include "qc2cpp/transport.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(MVDSV_QC2CPP_WASM)
#include "game/wasmtime_host.h"
#endif

void QC_TransportDiagnostic(qc_program_diagnostic_v1_t *diagnostic,
	qc_plugin_status_t status, const char *message);

qc_plugin_status_t QC_WasmOpen(const char *gamedir, const char *basename,
                               const qc_host_api_v1_t *host, void **handle,
                               qc_game_api_v1_t *game,
                               qc_program_diagnostic_v1_t *diagnostic)
{

#if defined(MVDSV_QC2CPP_WASM)
	char path[1024];
	int path_size;
	path_size = snprintf(path, sizeof(path), "%s/%s.wasm", gamedir, basename);
	if (path_size < 0 || (size_t)path_size >= sizeof(path)) {
		QC_TransportDiagnostic(diagnostic, QC_PLUGIN_BAD_ARGUMENT,
			"qc2cpp Wasm artifact path is too long");
		return QC_PLUGIN_BAD_ARGUMENT;
	}
	FILE *input = fopen(path, "rb");
	if (input == NULL) {
		QC_TransportDiagnostic(diagnostic, QC_PLUGIN_IO_ERROR,
			"qc2cpp Wasm artifact could not be opened");
		return QC_PLUGIN_IO_ERROR;
	}
	if (fseek(input, 0L, SEEK_END) != 0) {
		fclose(input);
		QC_TransportDiagnostic(diagnostic, QC_PLUGIN_IO_ERROR,
			"qc2cpp Wasm artifact could not be read");
		return QC_PLUGIN_IO_ERROR;
	}
	const long length = ftell(input);
	if (length <= 0L || (unsigned long)length > UINT32_MAX || fseek(input, 0L, SEEK_SET) != 0) {
		fclose(input);
		QC_TransportDiagnostic(diagnostic, QC_PLUGIN_IO_ERROR,
			"qc2cpp Wasm artifact has an invalid size");
		return QC_PLUGIN_IO_ERROR;
	}
	uint8_t *bytes = malloc((size_t)length);
	if (bytes == NULL || fread(bytes, 1U, (size_t)length, input) != (size_t)length) {
		free(bytes);
		fclose(input);
		QC_TransportDiagnostic(diagnostic, QC_PLUGIN_IO_ERROR,
			"qc2cpp Wasm artifact could not be read");
		return QC_PLUGIN_IO_ERROR;
	}
	fclose(input);
	qc_wasm_instance_t *instance = NULL;
	const qc_plugin_status_t status = qc_wasm_create_v1(bytes, (qc_byte_count_t)length,
		host, &instance, game, diagnostic);
	free(bytes);
	if (status != QC_PLUGIN_OK) {
		return status;
	}
	*handle = instance;
	return QC_PLUGIN_OK;
#else
	(void)gamedir;
	(void)basename;
	(void)host;
	(void)handle;
	(void)game;
	QC_TransportDiagnostic(diagnostic, QC_PLUGIN_UNAVAILABLE,
		"qc2cpp Wasm transport was not enabled when MVDSV was built");
	return QC_PLUGIN_UNAVAILABLE;
#endif
}

void QC_WasmClose(void *handle)
{
	#if defined(MVDSV_QC2CPP_WASM)
	qc_wasm_destroy_v1((qc_wasm_instance_t *)handle);
	#else
	(void)handle;
	#endif
}
