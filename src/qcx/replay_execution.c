#include "qcx/replay_probe.h"
#include <stdio.h>

int QCX_ReplayExecute(const qcx_replay_tape_t *tape, qcx_replay_mode_t mode)
{
	if (mode != QCX_REPLAY_VALIDATE || !QCX_ReplayTapeValidate(tape)) {
		return 0;
	}
	QCX_ReplaySeedHost(tape->host_seed);
	for (size_t i = 0; i < tape->count; ++i) {
		qcx_replay_event_t event = tape->events[i];
		QCX_ReplayApplyClock(&event.clock);
		if (!QCX_ReplayAlignRng(event.rng_draws)
			|| !SV_QCXReplayDispatch(&event, tape)) {
			fprintf(stderr, "QCX replay failed at event %zu kind %u: %s\n", i,
				(unsigned)event.kind, QCX_ReplayProbeError());
			return 0;
		}
	}
	return 1;
}
