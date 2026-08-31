#include <assert.h>

#include "qc2cpp/transport.h"

int main(int argc, char **argv)
{
	assert(argc == 2);
	qc_transport_t *transport = (qc_transport_t *)1;
	qc_program_diagnostic_v1_t diagnostic = {0};
	assert(QC_TransportOpen(99, ".", "game", NULL, &transport, &diagnostic)
	       == QC_PLUGIN_BAD_ARGUMENT);
	assert(transport == NULL);
	assert(QC_TransportOpen(4, ".", "game", NULL, &transport, &diagnostic)
	       == QC_PLUGIN_BAD_ARGUMENT);
	assert(transport == NULL && diagnostic.message_size != 0U);
	assert(QC_TransportOpen(4, "/definitely/missing", "game", NULL,
	                        &transport, &diagnostic) == QC_PLUGIN_IO_ERROR);
	assert(transport == NULL && diagnostic.message_size != 0U);

	assert(QC_TransportOpen(4, argv[1], "game", NULL, &transport, &diagnostic)
	       == QC_PLUGIN_OK);
	assert(transport != NULL);
	assert(QC_TransportGame(transport) != NULL);
	QC_TransportClose(transport);

	assert(QC_TransportOpen(4, argv[1], "bad", NULL, &transport, &diagnostic)
	       == QC_PLUGIN_BAD_ABI);
	assert(transport == NULL && diagnostic.message_size != 0U);
	assert(QC_TransportOpen(4, argv[1], "no_query", NULL, &transport, &diagnostic)
	       == QC_PLUGIN_MISSING_CAPABILITY);
	assert(transport == NULL && diagnostic.message_size != 0U);
	assert(QC_TransportOpen(4, argv[1], "../game", NULL, &transport, &diagnostic)
	       == QC_PLUGIN_BAD_ARGUMENT);
	assert(transport == NULL && diagnostic.message_size != 0U);
#if defined(MVDSV_QC2CPP_WASM)
	const qc_plugin_status_t wasm_missing_status = QC_PLUGIN_IO_ERROR;
#else
	const qc_plugin_status_t wasm_missing_status = QC_PLUGIN_UNAVAILABLE;
#endif
	assert(QC_TransportOpen(5, argv[1], "game", NULL, &transport, &diagnostic)
	       == wasm_missing_status);
	assert(transport == NULL && diagnostic.message_size != 0U);
	return 0;
}
