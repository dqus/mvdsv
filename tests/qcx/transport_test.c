#include <assert.h>

#include "qcx/transport.h"

int main(int argc, char **argv)
{
	assert(argc == 2);
	qcx_transport_t *transport = (qcx_transport_t *)1;
	qcx_program_diagnostic_v1_t diagnostic = {0};
	assert(QCX_TransportOpen(99, ".", "game", NULL, &transport, &diagnostic)
	       == QCX_PLUGIN_BAD_ARGUMENT);
	assert(transport == NULL);
	assert(QCX_TransportOpen(4, ".", "game", NULL, &transport, &diagnostic)
	       == QCX_PLUGIN_BAD_ARGUMENT);
	assert(transport == NULL && diagnostic.message_size != 0U);
	assert(QCX_TransportOpen(4, "/definitely/missing", "game", NULL,
	                        &transport, &diagnostic) == QCX_PLUGIN_IO_ERROR);
	assert(transport == NULL && diagnostic.message_size != 0U);

	assert(QCX_TransportOpen(4, argv[1], "game", NULL, &transport, &diagnostic)
	       == QCX_PLUGIN_OK);
	assert(transport != NULL);
	assert(QCX_TransportGame(transport) != NULL);
	QCX_TransportClose(transport);

	assert(QCX_TransportOpen(4, argv[1], "bad", NULL, &transport, &diagnostic)
	       == QCX_PLUGIN_BAD_ABI);
	assert(transport == NULL && diagnostic.message_size != 0U);
	assert(QCX_TransportOpen(4, argv[1], "no_query", NULL, &transport, &diagnostic)
	       == QCX_PLUGIN_MISSING_CAPABILITY);
	assert(transport == NULL && diagnostic.message_size != 0U);
	assert(QCX_TransportOpen(4, argv[1], "../game", NULL, &transport, &diagnostic)
	       == QCX_PLUGIN_BAD_ARGUMENT);
	assert(transport == NULL && diagnostic.message_size != 0U);
#if defined(QCX_WASM)
	const qcx_plugin_status_t wasm_missing_status = QCX_PLUGIN_IO_ERROR;
#else
	const qcx_plugin_status_t wasm_missing_status = QCX_PLUGIN_UNAVAILABLE;
#endif
	assert(QCX_TransportOpen(5, argv[1], "game", NULL, &transport, &diagnostic)
	       == wasm_missing_status);
	assert(transport == NULL && diagnostic.message_size != 0U);
	return 0;
}
