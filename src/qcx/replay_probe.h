#ifndef MVDSV_QCX_REPLAY_PROBE_H
#define MVDSV_QCX_REPLAY_PROBE_H

#include "qcx/replay_tape.h"

typedef enum {
	QCX_REPLAY_OFF, QCX_REPLAY_RECORD, QCX_REPLAY_VALIDATE, QCX_REPLAY_MEASURE
} qcx_replay_mode_t;

int QCX_ReplayConfigure(int argc, char **argv);
qcx_replay_mode_t QCX_ReplayMode(void);
const char *QCX_ReplayPath(void);
const char *QCX_ReplayProbeError(void);
void QCX_ReplaySeedHost(unsigned seed);
int QCX_ReplayRand(void);
uint64_t QCX_ReplayRngDraws(void);
int QCX_ReplayAlignRng(uint64_t draws);
unsigned QCX_ReplayGuestSeed(unsigned ordinary_seed);
void QCX_ReplayApplyClock(const qcx_replay_clock_t *clock);
/* Engine clock write, provided by sv_main.c; tests supply real scalar cells. */
void SV_QCXReplayApplyClock(const qcx_replay_clock_t *clock);

#endif
