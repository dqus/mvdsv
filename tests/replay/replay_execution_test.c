#include "qcx/replay_probe.h"
#include <assert.h>
#include <stdio.h>

static unsigned frames, physics, pre, post, commands;
static int in_group;
static qcx_replay_clock_t clock_cell;
void SV_QCXReplayApplyClock(const qcx_replay_clock_t *clock) { clock_cell = *clock; }
int SV_QCXReplayDispatch(qcx_replay_event_t *event, const qcx_replay_tape_t *tape)
{
	(void)tape;
	assert(clock_cell.sv_time == event->clock.sv_time);
	switch (event->kind) {
	case QCX_REPLAY_FRAME_BEGIN:
		++frames;
		QCX_ReplayRand();
		break;
	case QCX_REPLAY_GROUP_BEGIN:
		assert(!in_group);
		in_group = 1;
		++pre;
		break;
	case QCX_REPLAY_COMMAND:
		assert(in_group);
		++commands;
		assert(event->command.msec == 100 && event->command.forwardmove == 400);
		event->command.msec = 50; /* Model mutation: never corrupt the tape. */
		event->command.forwardmove = -400;
		break;
	case QCX_REPLAY_GROUP_END:
		assert(in_group);
		in_group = 0;
		++post;
		break;
	case QCX_REPLAY_PHYSICS:
		assert(!in_group);
		++physics;
		break;
	default:
		break;
	}
	return 1;
}

int main(void)
{
	char *argv[] = {"mvdsv", "-qcx-probe-replay", "fixture"};
	assert(QCX_ReplayConfigure(3, argv));
	qcx_replay_event_t events[15] = {0};
	qcx_replay_kind_t kinds[] = {QCX_REPLAY_MAP, QCX_REPLAY_FRAME_BEGIN,
		QCX_REPLAY_ACCEPT, QCX_REPLAY_SETUP, QCX_REPLAY_BEGIN,
		QCX_REPLAY_GROUP_BEGIN, QCX_REPLAY_COMMAND, QCX_REPLAY_COMMAND,
		QCX_REPLAY_GROUP_END, QCX_REPLAY_PHYSICS, QCX_REPLAY_FRAME_END,
		QCX_REPLAY_FRAME_BEGIN, QCX_REPLAY_PHYSICS, QCX_REPLAY_FRAME_END, QCX_REPLAY_END};
	for (size_t i = 0; i < 15; ++i) {
		events[i].kind = kinds[i];
		events[i].sequence = i;
		events[i].slot = i >= 2 && i <= 8 ? 0 : QCX_REPLAY_NO_SLOT;
		events[i].clock = (qcx_replay_clock_t){1 + .01*i, 2 + .01*i, 3 + .01*i};
		events[i].rng_draws = i < 2 ? 0 : i < 12 ? 1 : 2;
		events[i].command = (qcx_replay_command_t){.msec = 100, .forwardmove = 400};
	}
	qcx_replay_tape_t tape = {.guest_seed=0xadc0de, .host_seed=1, .clients=1,
		.map="povdmm4", .asset_hash="fixture", .config="deathmatch=4;sv_speedcheck=0;sv_antilag=0;sv_minping=0",
		.timed_begin=11, .timed_end=13, .count=15, .events=events};
	assert(QCX_ReplayExecute(&tape, QCX_REPLAY_VALIDATE));
	assert(frames == 2 && physics == 2 && pre == 1 && post == 1 && commands == 2);
	assert(events[6].command.msec == 100 && events[6].command.forwardmove == 400);
	assert(QCX_ReplayRngDraws() == 2);
	events[7].kind = QCX_REPLAY_PHYSICS;
	assert(!QCX_ReplayExecute(&tape, QCX_REPLAY_VALIDATE));
	puts("execution: empty frames, real boundaries and immutable tape command copies");
}
