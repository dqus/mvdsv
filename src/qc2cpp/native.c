#include "qc2cpp/transport.h"

#include <stdio.h>
#include <string.h>

#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#define QC_NATIVE_POSIX_LOADER 1
#endif

void QC_TransportDiagnostic(qc_program_diagnostic_v1_t *diagnostic,
	qc_plugin_status_t status, const char *message);

typedef qc_plugin_status_t (*qc_plugin_query_v1_t)(const qc_host_api_v1_t *,
	qc_game_api_v1_t *);

#if defined(__APPLE__)
#define QC_NATIVE_SUFFIX ".dylib"
#elif defined(__linux__)
#define QC_NATIVE_SUFFIX ".so"
#else
#define QC_NATIVE_SUFFIX NULL
#endif

qc_plugin_status_t QC_NativeOpen(const char *gamedir, const char *basename,
                                 const qc_host_api_v1_t *host, void **handle,
                                 qc_game_api_v1_t *game,
                                 qc_program_diagnostic_v1_t *diagnostic)
{
#if !defined(MVDSV_QC2CPP_NATIVE) || !defined(QC_NATIVE_POSIX_LOADER)
	(void)gamedir;
	(void)basename;
	(void)host;
	(void)handle;
	(void)game;
	QC_TransportDiagnostic(diagnostic, QC_PLUGIN_UNAVAILABLE,
		"qc2cpp native transport was not enabled when MVDSV was built");
	return QC_PLUGIN_UNAVAILABLE;
#else
	char path[1024];
	if (QC_NATIVE_SUFFIX == NULL) {
		QC_TransportDiagnostic(diagnostic, QC_PLUGIN_UNAVAILABLE,
			"qc2cpp native transport is unsupported on this platform");
		return QC_PLUGIN_UNAVAILABLE;
	}
	const int path_size = snprintf(path, sizeof(path), "%s/%s%s", gamedir,
		basename, QC_NATIVE_SUFFIX);
	if (path_size < 0 || (size_t)path_size >= sizeof(path)) {
		QC_TransportDiagnostic(diagnostic, QC_PLUGIN_BAD_ARGUMENT,
			"qc2cpp native artifact path is too long");
		return QC_PLUGIN_BAD_ARGUMENT;
	}
	void *native_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (native_handle == NULL) {
		QC_TransportDiagnostic(diagnostic, QC_PLUGIN_IO_ERROR,
			"qc2cpp native artifact could not be loaded");
		return QC_PLUGIN_IO_ERROR;
	}
	dlerror();
	void *symbol = dlsym(native_handle, "qc_game_plugin_query_v1");
	if (dlerror() != NULL || symbol == NULL) {
		dlclose(native_handle);
		QC_TransportDiagnostic(diagnostic, QC_PLUGIN_MISSING_CAPABILITY,
			"qc2cpp native artifact has no qc_game_plugin_query_v1 export");
		return QC_PLUGIN_MISSING_CAPABILITY;
	}
	qc_plugin_query_v1_t query = NULL;
	memcpy(&query, &symbol, sizeof(query));
	memset(game, 0, sizeof(*game));
	const qc_plugin_status_t status = query(host, game);
	if (status != QC_PLUGIN_OK) {
		dlclose(native_handle);
		QC_TransportDiagnostic(diagnostic, status,
			"qc2cpp native artifact query failed");
		return status;
	}
	const qc_plugin_status_t validation = qc_validate_game_api_v1(game);
	if (validation != QC_PLUGIN_OK) {
		dlclose(native_handle);
		QC_TransportDiagnostic(diagnostic, validation,
			"qc2cpp native artifact returned an incompatible game API");
		return validation;
	}
	*handle = native_handle;
	return QC_PLUGIN_OK;
#endif
}

void QC_NativeClose(void *handle)
{
#if defined(MVDSV_QC2CPP_NATIVE) && defined(QC_NATIVE_POSIX_LOADER)
	if (handle != NULL) {
		dlclose(handle);
	}
#else
	(void)handle;
#endif
}
