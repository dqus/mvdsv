#include "qcx/replay_probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t *schedule;
static size_t schedule_count;
static double cpu_seconds;
static uint64_t failure_event = UINT64_MAX;
double QCX_ReplayCPUSeconds(void) { return cpu_seconds; }
uint64_t QCX_ReplayFailureEvent(void) { return failure_event; }
const uint64_t *QCX_ReplaySchedule(size_t *count)
{
	*count = schedule_count;
	return schedule;
}
int QCX_ReplayInstallSchedule(const uint64_t *skips, size_t count)
{
	uint64_t *copy = malloc(count * sizeof(*copy));
	if (!copy || !skips || !count) {
		free(copy);
		return 0;
	}
	memcpy(copy, skips, count * sizeof(*copy));
	free(schedule);
	schedule = copy;
	schedule_count = count;
	return 1;
}

int QCX_ReplayExecute(const qcx_replay_tape_t *tape, qcx_replay_mode_t mode)
{
	failure_event = UINT64_MAX;
	if ((mode != QCX_REPLAY_VALIDATE && mode != QCX_REPLAY_MEASURE)
		|| !QCX_ReplayTapeValidate(tape)) {
		return 0;
	}
	if (mode == QCX_REPLAY_VALIDATE) {
		free(schedule);
		schedule = calloc(tape->count, sizeof(*schedule));
		schedule_count = 0;
		if (!schedule) {
			return 0;
		}
	}
	else if (schedule_count != tape->count || qcx_replay_observe) {
		return 0;
	}
	QCX_ReplaySeedHost(tape->host_seed);
	cpu_seconds = 0;
	double cpu_begin = 0;
	for (size_t i = 0; i < tape->count; ++i) {
		failure_event = i;
		qcx_replay_event_t event = tape->events[i];
		if (mode == QCX_REPLAY_MEASURE && i == tape->timed_begin) {
			qcx_replay_timed = 1;
			cpu_begin = SV_QCXReplayProcessCPU();
		}
		QCX_ReplayApplyClock(&event.clock);
		if (mode == QCX_REPLAY_VALIDATE) {
			uint64_t before = QCX_ReplayRngDraws();
			if (!QCX_ReplayAlignRng(event.rng_draws)) {
				return 0;
			}
			schedule[i] = event.rng_draws - before;
		}
		else {
			for (uint64_t j = 0; j < schedule[i]; ++j) {
				rand();
			}
		}
		if (event.kind != QCX_REPLAY_CHECKPOINT || mode == QCX_REPLAY_VALIDATE) {
			if (!SV_QCXReplayDispatch(&event, tape)) {
				qcx_replay_timed = 0;
			fprintf(stderr, "QCX replay failed at event %zu kind %u: %s\n", i,
				(unsigned)event.kind, QCX_ReplayProbeError());
			return 0;
			}
		}
		if (mode == QCX_REPLAY_MEASURE && i == tape->timed_end) {
			cpu_seconds = SV_QCXReplayProcessCPU() - cpu_begin;
			qcx_replay_timed = 0;
		}
	}
	schedule_count = tape->count;
	failure_event = UINT64_MAX;
	return 1;
}
