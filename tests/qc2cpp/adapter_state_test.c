#include <assert.h>

#include "qc2cpp/adapter_state.h"

int main(void)
{
	qc_adapter_state_t state = {0};
	assert(!QC_AdapterStateActive(&state));
	assert(QC_AdapterStateIdle(&state));

	/* Selection happens before opening the artifact.  If opening fails and
	 * server teardown starts, PR1 must not become the accidental fallback. */
	QC_AdapterStateSelect(&state, 4);
	assert(QC_AdapterStateActive(&state));
	assert(QC_AdapterStateIdle(&state));

	QC_AdapterStateEnter(&state);
	assert(!QC_AdapterStateIdle(&state));
	QC_AdapterStateLeave(&state);
	assert(QC_AdapterStateIdle(&state));

	QC_AdapterStateReset(&state);
	assert(!QC_AdapterStateActive(&state));
	return 0;
}
