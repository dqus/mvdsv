#include "qcx/replay_probe.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static qcx_replay_clock_t live;
void SV_QCXReplayApplyClock(const qcx_replay_clock_t *clock) { live = *clock; }

int main(void)
{
	char *record[] = {"mvdsv", "-qcx-probe-record", "tape.bin"};
	assert(QCX_ReplayConfigure(3, record));
	assert(QCX_ReplayMode() == QCX_REPLAY_RECORD);
	srand(1);
	int expected[5];
	for (int i = 0; i < 5; ++i) {
		expected[i] = rand();
	}
	QCX_ReplaySeedHost(1);
	assert(QCX_ReplayRand() == expected[0]);
	assert(QCX_ReplayRngDraws() == 1);
	assert(QCX_ReplayAlignRng(4));
	assert(QCX_ReplayRngDraws() == 4);
	assert(QCX_ReplayRand() == expected[4]);
	assert(!QCX_ReplayAlignRng(4));
	assert(QCX_ReplayGuestSeed(123) == 0xadc0de);
	qcx_replay_clock_t clock = {12.25, 15.5, 17.75};
	QCX_ReplayApplyClock(&clock);
	assert(live.sv_time == 12.25 && live.realtime == 15.5 && live.curtime == 17.75);

	char *conflict[] = {"mvdsv", "-qcx-probe-record", "a", "-qcx-probe-replay", "b"};
	assert(!QCX_ReplayConfigure(5, conflict));
	assert(QCX_ReplayMode() == QCX_REPLAY_OFF);
	char *missing[] = {"mvdsv", "-qcx-probe-record"};
	assert(!QCX_ReplayConfigure(2, missing));
	char *badmode[] = {"mvdsv", "-qcx-probe-replay", "a", "-qcx-probe-mode", "invalid"};
	assert(!QCX_ReplayConfigure(5, badmode));
	char *replay[] = {"mvdsv", "-qcx-probe-replay", "a", "-qcx-probe-mode", "measure"};
	assert(QCX_ReplayConfigure(5, replay));
	assert(QCX_ReplayMode() == QCX_REPLAY_MEASURE);
	QCX_ReplaySeedHost(1);
	assert(QCX_ReplayRand() == expected[0] && QCX_ReplayRngDraws() == 0);
	char *mode_only[] = {"mvdsv", "-qcx-probe-mode", "validate"};
	assert(!QCX_ReplayConfigure(3, mode_only));
	char *ordinary[] = {"mvdsv"};
	assert(QCX_ReplayConfigure(1, ordinary));
	assert(QCX_ReplayMode() == QCX_REPLAY_OFF && QCX_ReplayPath() == NULL);
	assert(QCX_ReplayGuestSeed(123) == 123);
	puts("clock/RNG: seed, skipped draws, excess draws, clocks and inactive mode passed");
	return 0;
}
