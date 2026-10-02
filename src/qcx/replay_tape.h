#ifndef MVDSV_QCX_REPLAY_TAPE_H
#define MVDSV_QCX_REPLAY_TAPE_H

#include <stdint.h>
#include <stddef.h>

/* Diagnostic format, deliberately unrelated to QCX save/ABI compatibility. */
#define QCX_REPLAY_NO_SLOT UINT32_MAX
#define QCX_REPLAY_MAX_CLIENTS 32U
#define QCX_REPLAY_MAX_PAYLOAD 8192U
#define QCX_REPLAY_MAX_EVENTS 1000000U

typedef enum {
	QCX_REPLAY_MAP = 1, QCX_REPLAY_ACCEPT, QCX_REPLAY_SETUP, QCX_REPLAY_BEGIN,
	QCX_REPLAY_FRAME_BEGIN, QCX_REPLAY_GROUP_BEGIN, QCX_REPLAY_COMMAND,
	QCX_REPLAY_GROUP_END, QCX_REPLAY_PHYSICS, QCX_REPLAY_FRAME_END,
	QCX_REPLAY_CHECKPOINT, QCX_REPLAY_END, QCX_REPLAY_USERINFO
} qcx_replay_kind_t;

typedef struct {
	double sv_time, realtime, curtime;
} qcx_replay_clock_t;

typedef struct {
	uint8_t msec, buttons, impulse;
	float angles[3];
	int16_t forwardmove, sidemove, upmove;
} qcx_replay_command_t;

typedef struct {
	qcx_replay_kind_t kind;
	uint32_t slot;
	uint64_t sequence, rng_draws;
	qcx_replay_clock_t clock;
	qcx_replay_command_t command;
	uint32_t payload_size;
	uint8_t *payload;
} qcx_replay_event_t;

typedef struct {
	uint32_t guest_seed, host_seed, clients;
	char map[64], asset_hash[65], config[8192];
	uint64_t timed_begin, timed_end;
	size_t count;
	qcx_replay_event_t *events;
} qcx_replay_tape_t;

int QCX_ReplayTapeWrite(const char *path, const qcx_replay_tape_t *tape);
int QCX_ReplayTapeLoad(const char *path, qcx_replay_tape_t *out);
int QCX_ReplayTapeValidate(const qcx_replay_tape_t *tape);
void QCX_ReplayTapeFree(qcx_replay_tape_t *tape);
const char *QCX_ReplayTapeError(void);

#endif
