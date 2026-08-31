#ifndef MVDSV_QC2CPP_TRANSPORT_H
#define MVDSV_QC2CPP_TRANSPORT_H

#include <game/plugin_api.h>

typedef struct qc_transport qc_transport_t;

qc_plugin_status_t QC_TransportOpen(int mode, const char *gamedir,
                                    const char *basename,
                                    const qc_host_api_v1_t *host,
                                    qc_transport_t **out,
                                    qc_program_diagnostic_v1_t *diagnostic);
const qc_game_api_v1_t *QC_TransportGame(qc_transport_t *transport);
void QC_TransportClose(qc_transport_t *transport);

#endif
