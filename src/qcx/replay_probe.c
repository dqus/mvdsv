#include "qcx/replay_probe.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static qcx_replay_mode_t mode;
static const char *path;
static char error[192];
static uint64_t rng_draws;
int qcx_replay_observe;
int qcx_replay_timed;
uint64_t qcx_replay_work[QCX_WORK_COUNT];

static int Fail(const char *message)
{
	mode = QCX_REPLAY_OFF;
	path = NULL;
	snprintf(error, sizeof(error), "%s", message);
	return 0;
}

int QCX_ReplayConfigure(int argc, char **argv)
{
	const char *record = NULL, *replay = NULL, *requested_mode = NULL;
	mode = QCX_REPLAY_OFF;
	path = NULL;
	error[0] = 0;
	qcx_replay_observe = 0;
	qcx_replay_timed = 0;
	memset(qcx_replay_work, 0, sizeof(qcx_replay_work));
	for (int i = 1; i < argc; ++i) {
		const char **value = NULL;
		if (!strcmp(argv[i], "-qcx-probe-record")) {
			value = &record;
		}
		else if (!strcmp(argv[i], "-qcx-probe-replay")) {
			value = &replay;
		}
		else if (!strcmp(argv[i], "-qcx-probe-mode")) {
			value = &requested_mode;
		}
		if (value) {
			if (*value || i + 1 >= argc || !argv[i+1][0] || argv[i+1][0] == '-') {
				return Fail("duplicate or missing replay option value");
			}
			*value = argv[++i];
		}
	}
	if ((record && replay) || (requested_mode && !replay)) {
		return Fail("conflicting replay options");
	}
	if (record) {
		mode = QCX_REPLAY_RECORD;
		path = record;
	}
	else if (replay) {
		path = replay;
		if (!requested_mode || !strcmp(requested_mode, "validate")) {
			mode = QCX_REPLAY_VALIDATE;
		}
		else if (!strcmp(requested_mode, "measure")) {
			mode = QCX_REPLAY_MEASURE;
		}
		else {
			return Fail("replay mode must be validate or measure");
		}
	}
	qcx_replay_observe = mode == QCX_REPLAY_RECORD || mode == QCX_REPLAY_VALIDATE;
	return 1;
}
qcx_replay_mode_t QCX_ReplayMode(void) { return mode; }
const char *QCX_ReplayPath(void) { return path; }
const char *QCX_ReplayProbeError(void) { return error; }
void QCX_ReplaySeedHost(unsigned seed)
{
	srand(seed);
	rng_draws = 0;
}
int QCX_ReplayRand(void)
{
	if (mode == QCX_REPLAY_RECORD || mode == QCX_REPLAY_VALIDATE) {
		++rng_draws;
	}
	return rand();
}
uint64_t QCX_ReplayRngDraws(void) { return rng_draws; }
int QCX_ReplayAlignRng(uint64_t draws)
{
	if (mode != QCX_REPLAY_VALIDATE && mode != QCX_REPLAY_RECORD) {
		return Fail("ordinal alignment requires counted validation mode");
	}
	if (rng_draws > draws || draws - rng_draws > 10000000U) {
		/* Do not reset the active mode on a divergence: preserve evidence. */
		snprintf(error, sizeof(error), "host RNG ordinal exceeded or skipped-draw limit exceeded");
		return 0;
	}
	while (rng_draws < draws) {
		QCX_ReplayRand();
	}
	return 1;
}
unsigned QCX_ReplayGuestSeed(unsigned ordinary_seed)
{
	return mode == QCX_REPLAY_OFF ? ordinary_seed : 0x00adc0deU;
}
void QCX_ReplayApplyClock(const qcx_replay_clock_t *clock)
{
	SV_QCXReplayApplyClock(clock);
}
