#include "qcx/transport.h"

#include <stdio.h>
#include <string.h>

#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#define QCX_NATIVE_POSIX_LOADER 1
#endif

void QCX_TransportDiagnostic(qcx_program_diagnostic_v1_t *diagnostic,
	qcx_plugin_status_t status, const char *message);

typedef qcx_plugin_status_t (*qcx_plugin_query_v1_t)(const qcx_host_api_v1_t *,
	qcx_game_api_v1_t *);

#if defined(__APPLE__)
#define QCX_NATIVE_SUFFIX ".dylib"
#elif defined(__linux__)
#define QCX_NATIVE_SUFFIX ".so"
#else
#define QCX_NATIVE_SUFFIX NULL
#endif

qcx_plugin_status_t QCX_NativeOpen(const char *gamedir, const char *basename,
                                 const qcx_host_api_v1_t *host, void **handle,
                                 qcx_game_api_v1_t *game,
                                 qcx_program_diagnostic_v1_t *diagnostic)
{
#if !defined(QCX_NATIVE) || !defined(QCX_NATIVE_POSIX_LOADER)
	(void)gamedir;
	(void)basename;
	(void)host;
	(void)handle;
	(void)game;
	QCX_TransportDiagnostic(diagnostic, QCX_PLUGIN_UNAVAILABLE,
		"qc2cpp native transport was not enabled when MVDSV was built");
	return QCX_PLUGIN_UNAVAILABLE;
#else
	char path[1024];
	if (QCX_NATIVE_SUFFIX == NULL) {
		QCX_TransportDiagnostic(diagnostic, QCX_PLUGIN_UNAVAILABLE,
			"qc2cpp native transport is unsupported on this platform");
		return QCX_PLUGIN_UNAVAILABLE;
	}
	const int path_size = snprintf(path, sizeof(path), "%s/%s%s", gamedir,
		basename, QCX_NATIVE_SUFFIX);
	if (path_size < 0 || (size_t)path_size >= sizeof(path)) {
		QCX_TransportDiagnostic(diagnostic, QCX_PLUGIN_BAD_ARGUMENT,
			"qc2cpp native artifact path is too long");
		return QCX_PLUGIN_BAD_ARGUMENT;
	}
	void *native_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (native_handle == NULL) {
		const char *const loader_error = dlerror();
		char message[sizeof(diagnostic->message)];
		snprintf(message, sizeof(message),
			"qc2cpp native artifact %.48s could not be loaded: %.64s", path,
			loader_error == NULL ? "unknown dynamic-loader error" : loader_error);
		QCX_TransportDiagnostic(diagnostic, QCX_PLUGIN_IO_ERROR,
			message);
		return QCX_PLUGIN_IO_ERROR;
	}
	dlerror();
	void *symbol = dlsym(native_handle, "qcx_game_plugin_query_v1");
	if (dlerror() != NULL || symbol == NULL) {
		dlclose(native_handle);
		QCX_TransportDiagnostic(diagnostic, QCX_PLUGIN_MISSING_CAPABILITY,
			"qc2cpp native artifact has no qcx_game_plugin_query_v1 export");
		return QCX_PLUGIN_MISSING_CAPABILITY;
	}
	qcx_plugin_query_v1_t query = NULL;
	memcpy(&query, &symbol, sizeof(query));
	*game = (qcx_game_api_v1_t){
		.abi_version = QCX_PLUGIN_ABI_VERSION_V1,
		.struct_size = sizeof(*game),
	};
	const qcx_plugin_status_t status = query(host, game);
	if (status != QCX_PLUGIN_OK) {
		char message[sizeof(diagnostic->message)];
		snprintf(message, sizeof(message),
			"qc2cpp native artifact query failed with plugin status %u", (unsigned)status);
		dlclose(native_handle);
		QCX_TransportDiagnostic(diagnostic, status,
			message);
		return status;
	}
	const qcx_plugin_status_t validation = qcx_validate_game_api_v1(game);
	if (validation != QCX_PLUGIN_OK) {
		dlclose(native_handle);
		QCX_TransportDiagnostic(diagnostic, validation,
			"qc2cpp native artifact returned an incompatible game API");
		return validation;
	}
	*handle = native_handle;
	return QCX_PLUGIN_OK;
#endif
}

void QCX_NativeClose(void *handle)
{
#if defined(QCX_NATIVE) && defined(QCX_NATIVE_POSIX_LOADER)
	if (handle != NULL) {
		dlclose(handle);
	}
#else
	(void)handle;
#endif
}
