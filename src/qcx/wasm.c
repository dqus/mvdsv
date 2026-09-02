#include "qcx/transport.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(QCX_WASM)
#include "game/wasmtime_host.h"
#endif

void QCX_TransportDiagnostic(qcx_program_diagnostic_v1_t *diagnostic,
	qcx_plugin_status_t status, const char *message);

qcx_plugin_status_t QCX_WasmOpen(const char *gamedir, const char *basename,
                               const qcx_host_api_v1_t *host, void **handle,
                               qcx_game_api_v1_t *game,
                               qcx_program_diagnostic_v1_t *diagnostic)
{

#if defined(QCX_WASM)
	char path[1024];
	int path_size;
	path_size = snprintf(path, sizeof(path), "%s/%s.wasm", gamedir, basename);
	if (path_size < 0 || (size_t)path_size >= sizeof(path)) {
		QCX_TransportDiagnostic(diagnostic, QCX_PLUGIN_BAD_ARGUMENT,
			"qc2cpp Wasm artifact path is too long");
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	FILE *input = fopen(path, "rb");
	if (input == NULL) {
		QCX_TransportDiagnostic(diagnostic, QCX_PLUGIN_IO_ERROR,
			"qc2cpp Wasm artifact could not be opened");
		return QCX_PLUGIN_IO_ERROR;
	}
	if (fseek(input, 0L, SEEK_END) != 0) {
		fclose(input);
		QCX_TransportDiagnostic(diagnostic, QCX_PLUGIN_IO_ERROR,
			"qc2cpp Wasm artifact could not be read");
		return QCX_PLUGIN_IO_ERROR;
	}
	const long length = ftell(input);
	if (length <= 0L || (unsigned long)length > UINT32_MAX || fseek(input, 0L, SEEK_SET) != 0) {
		fclose(input);
		QCX_TransportDiagnostic(diagnostic, QCX_PLUGIN_IO_ERROR,
			"qc2cpp Wasm artifact has an invalid size");
		return QCX_PLUGIN_IO_ERROR;
	}
	uint8_t *bytes = malloc((size_t)length);
	if (bytes == NULL || fread(bytes, 1U, (size_t)length, input) != (size_t)length) {
		free(bytes);
		fclose(input);
		QCX_TransportDiagnostic(diagnostic, QCX_PLUGIN_IO_ERROR,
			"qc2cpp Wasm artifact could not be read");
		return QCX_PLUGIN_IO_ERROR;
	}
	fclose(input);
	qcx_wasm_instance_t *instance = NULL;
	*game = (qcx_game_api_v1_t){
		.abi_version = QCX_PLUGIN_ABI_VERSION_V1,
		.struct_size = sizeof(*game),
	};
	const qcx_plugin_status_t status = qcx_wasm_create_v1(bytes, (qcx_byte_count_t)length,
		host, &instance, game, diagnostic);
	free(bytes);
	if (status != QCX_PLUGIN_OK) {
		return status;
	}
	*handle = instance;
	return QCX_PLUGIN_OK;
#else
	(void)gamedir;
	(void)basename;
	(void)host;
	(void)handle;
	(void)game;
	QCX_TransportDiagnostic(diagnostic, QCX_PLUGIN_UNAVAILABLE,
		"qc2cpp Wasm transport was not enabled when MVDSV was built");
	return QCX_PLUGIN_UNAVAILABLE;
#endif
}

void QCX_WasmClose(void *handle)
{

#if defined(QCX_WASM)
	qcx_wasm_destroy_v1((qcx_wasm_instance_t *)handle);
#else
	(void)handle;
#endif
}
