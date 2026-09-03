#ifndef MVDSV_QC2CPP_ADAPTER_STATE_H
#define MVDSV_QC2CPP_ADAPTER_STATE_H

#include "qcx/transport.h"

typedef struct qcx_adapter_state_s {
	qcx_transport_kind_t transport_kind;
	uint32_t call_depth;
} qcx_adapter_state_t;

int QCX_AdapterStateActive(const qcx_adapter_state_t *state);
int QCX_AdapterStateIdle(const qcx_adapter_state_t *state);
void QCX_AdapterStateSelect(qcx_adapter_state_t *state, qcx_transport_kind_t transport_kind);
void QCX_AdapterStateEnter(qcx_adapter_state_t *state);
void QCX_AdapterStateLeave(qcx_adapter_state_t *state);
void QCX_AdapterStateReset(qcx_adapter_state_t *state);

#endif
