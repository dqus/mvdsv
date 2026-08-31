#ifndef MVDSV_QC2CPP_ADAPTER_STATE_H
#define MVDSV_QC2CPP_ADAPTER_STATE_H

#include <stdint.h>

typedef struct qc_adapter_state_s {
	int mode;
	uint32_t call_depth;
} qc_adapter_state_t;

int QC_AdapterStateActive(const qc_adapter_state_t *state);
int QC_AdapterStateIdle(const qc_adapter_state_t *state);
void QC_AdapterStateSelect(qc_adapter_state_t *state, int mode);
void QC_AdapterStateEnter(qc_adapter_state_t *state);
void QC_AdapterStateLeave(qc_adapter_state_t *state);
void QC_AdapterStateReset(qc_adapter_state_t *state);

#endif
