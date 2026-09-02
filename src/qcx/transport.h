#ifndef MVDSV_QC2CPP_TRANSPORT_H
#define MVDSV_QC2CPP_TRANSPORT_H

#include <game/plugin_api.h>

typedef struct qcx_transport qcx_transport_t;

qcx_plugin_status_t QCX_TransportOpen(int mode, const char *gamedir,
                                    const char *basename,
                                    const qcx_host_api_v1_t *host,
                                    qcx_transport_t **out,
                                    qcx_program_diagnostic_v1_t *diagnostic);
const qcx_game_api_v1_t *QCX_TransportGame(qcx_transport_t *transport);
void QCX_TransportClose(qcx_transport_t *transport);

#endif
