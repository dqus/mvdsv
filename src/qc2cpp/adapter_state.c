#include "qc2cpp/adapter_state.h"

int QC_AdapterStateActive(const qc_adapter_state_t *state)
{
	return state->mode != 0;
}

int QC_AdapterStateIdle(const qc_adapter_state_t *state)
{
	return state->call_depth == 0U;
}

void QC_AdapterStateSelect(qc_adapter_state_t *state, int mode)
{
	state->mode = mode;
}

void QC_AdapterStateEnter(qc_adapter_state_t *state)
{
	++state->call_depth;
}

void QC_AdapterStateLeave(qc_adapter_state_t *state)
{
	if (state->call_depth != 0U) {
		--state->call_depth;
	}
}

void QC_AdapterStateReset(qc_adapter_state_t *state)
{
	state->mode = 0;
	state->call_depth = 0U;
}
