#ifndef MVDSV_QCX_REPLAY_PROBE_H
#define MVDSV_QCX_REPLAY_PROBE_H

#include "qcx/replay_tape.h"

typedef enum {
	QCX_REPLAY_OFF, QCX_REPLAY_RECORD, QCX_REPLAY_VALIDATE, QCX_REPLAY_MEASURE
} qcx_replay_mode_t;

enum qcx_replay_work_kind { QCX_WORK_FRAME, QCX_WORK_GROUP, QCX_WORK_COMMAND,
	QCX_WORK_PHYSICS, QCX_WORK_STARTFRAME, QCX_WORK_PRETHINK, QCX_WORK_POSTTHINK,
	QCX_WORK_THINK, QCX_WORK_TOUCH, QCX_WORK_BLOCKED, QCX_WORK_CONNECT,
	QCX_WORK_PUT, QCX_WORK_ALLOC, QCX_WORK_FREE, QCX_WORK_NEWPARMS, QCX_WORK_COUNT };
extern int qcx_replay_observe;
extern int qcx_replay_timed;
extern uint64_t qcx_replay_work[QCX_WORK_COUNT];
#define QCX_REPLAY_COUNT(kind) do { if (qcx_replay_observe) { ++qcx_replay_work[kind]; } } while (0)

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

int QCX_ReplayCaptureStart(const qcx_replay_tape_t *header);
void QCX_ReplayRecordEvent(const qcx_replay_event_t *event);
int QCX_ReplayCaptureFinish(uint64_t timed_begin, uint64_t timed_end);
size_t QCX_ReplayCaptureCount(void);
const char *QCX_ReplayCaptureError(void);
const qcx_replay_tape_t *QCX_ReplayCapturedTape(void);
void QCX_ReplayCaptureIdentity(uint32_t clients, const char *asset_identity);
int QCX_ReplayExecute(const qcx_replay_tape_t *tape, qcx_replay_mode_t mode);
double SV_QCXReplayProcessCPU(void);
double QCX_ReplayCPUSeconds(void);
uint64_t QCX_ReplayFailureEvent(void);
const uint64_t *QCX_ReplaySchedule(size_t *count);
int QCX_ReplayInstallSchedule(const uint64_t *skips, size_t count);
/* The engine dispatches existing gameplay paths; standalone tests use a trace. */
int SV_QCXReplayDispatch(qcx_replay_event_t *event, const qcx_replay_tape_t *tape);

#endif
