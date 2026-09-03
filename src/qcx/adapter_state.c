#include "qcx/adapter_state.h"

int QCX_AdapterStateActive(const qcx_adapter_state_t *state)
{
	return state->transport_kind != QCX_TRANSPORT_NONE;
}

int QCX_AdapterStateIdle(const qcx_adapter_state_t *state)
{
	return state->call_depth == 0U;
}

void QCX_AdapterStateSelect(qcx_adapter_state_t *state, qcx_transport_kind_t transport_kind)
{
	state->transport_kind = transport_kind;
}

void QCX_AdapterStateEnter(qcx_adapter_state_t *state)
{
	++state->call_depth;
}

void QCX_AdapterStateLeave(qcx_adapter_state_t *state)
{
	if (state->call_depth != 0U) {
		--state->call_depth;
	}
}

void QCX_AdapterStateReset(qcx_adapter_state_t *state)
{
	state->transport_kind = QCX_TRANSPORT_NONE;
	state->call_depth = 0U;
}
