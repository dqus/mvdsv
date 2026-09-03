#include <assert.h>

#include "qcx/adapter_state.h"

int main(void)
{
	qcx_adapter_state_t state = {0};
	assert(!QCX_AdapterStateActive(&state));
	assert(QCX_AdapterStateIdle(&state));

	/* Selection happens before opening the artifact.  If opening fails and
	 * server teardown starts, PR1 must not become the accidental fallback. */
	QCX_AdapterStateSelect(&state, QCX_TRANSPORT_NATIVE);
	assert(QCX_AdapterStateActive(&state));
	assert(QCX_AdapterStateIdle(&state));

	QCX_AdapterStateEnter(&state);
	assert(!QCX_AdapterStateIdle(&state));
	QCX_AdapterStateLeave(&state);
	assert(QCX_AdapterStateIdle(&state));
	QCX_AdapterStateSelect(&state, QCX_TRANSPORT_WASM);
	assert(QCX_AdapterStateActive(&state));

	QCX_AdapterStateReset(&state);
	assert(!QCX_AdapterStateActive(&state));
	assert(state.transport_kind == QCX_TRANSPORT_NONE);
	return 0;
}
