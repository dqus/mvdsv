#define _POSIX_C_SOURCE 200809L
#include "qcx/replay_probe.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void SV_QCXReplayApplyClock(const qcx_replay_clock_t *clock) { (void)clock; }

int main(void)
{
	char path[] = "/tmp/qcx-tape-test.XXXXXX";
	int fd = mkstemp(path);
	assert(fd >= 0);
	close(fd);
	qcx_replay_event_t events[14] = {0};
	const qcx_replay_kind_t kinds[] = {
		QCX_REPLAY_MAP, QCX_REPLAY_FRAME_BEGIN, QCX_REPLAY_ACCEPT,
		QCX_REPLAY_SETUP, QCX_REPLAY_BEGIN, QCX_REPLAY_GROUP_BEGIN,
		QCX_REPLAY_COMMAND, QCX_REPLAY_GROUP_END, QCX_REPLAY_PHYSICS,
		QCX_REPLAY_FRAME_END, QCX_REPLAY_FRAME_BEGIN, QCX_REPLAY_PHYSICS,
		QCX_REPLAY_FRAME_END, QCX_REPLAY_END
	};
	for (size_t i = 0; i < 14; ++i) {
		events[i].kind = kinds[i];
		events[i].sequence = i;
		events[i].clock = (qcx_replay_clock_t){1.0 + i * .01, 2.0 + i * .01, 3.0 + i * .01};
		events[i].rng_draws = i;
		events[i].slot = (i >= 2 && i <= 7) ? 0 : QCX_REPLAY_NO_SLOT;
	}
	events[6].command = (qcx_replay_command_t){
		.msec = 100, .buttons = 3, .impulse = 7, .angles = {90.25f, -45.0f, 0},
		.forwardmove = 400, .sidemove = -200, .upmove = 10
	};
	uint8_t payload[] = {'n', 'a', 'm', 'e'};
	events[2].payload = payload;
	events[2].payload_size = sizeof(payload);
	qcx_replay_tape_t tape = {
		.guest_seed = 0xadc0de, .host_seed = 1, .clients = 1,
		.map = "povdmm4", .asset_hash = "fixture-asset",
		.config = "deathmatch=4;sv_antilag=0;sv_speedcheck=0;sv_minping=0",
		.timed_begin = 10, .timed_end = 12, .count = 14, .events = events
	};
	assert(QCX_ReplayTapeWrite(path, &tape));
	qcx_replay_tape_t decoded = {0};
	assert(QCX_ReplayTapeLoad(path, &decoded));
	assert(decoded.count == 14 && decoded.timed_begin == 10 && decoded.timed_end == 12);
	assert(decoded.events[6].command.msec == 100);
	assert(decoded.events[6].command.sidemove == -200);
	assert(decoded.events[6].command.forwardmove == 400);
	assert(decoded.events[6].command.upmove == 10);
	assert(decoded.events[6].command.angles[0] == 90.25f);
	assert(decoded.events[6].command.angles[1] == -45.0f);
	assert(decoded.events[6].command.buttons == 3 && decoded.events[6].command.impulse == 7);
	assert(decoded.events[10].kind == QCX_REPLAY_FRAME_BEGIN);
	assert(decoded.events[11].kind == QCX_REPLAY_PHYSICS);
	assert(decoded.events[2].payload_size == 4 && !memcmp(decoded.events[2].payload, "name", 4));
	QCX_ReplayTapeFree(&decoded);
	assert(decoded.count == 0 && decoded.events == NULL);

	/* Each mutation is a runnable-input bug, not a formatting expectation. */
	events[6].slot = 32;
	assert(!QCX_ReplayTapeValidate(&tape));
	events[6].slot = 0;
	events[6].clock.sv_time = NAN;
	assert(!QCX_ReplayTapeValidate(&tape));
	events[6].clock = events[5].clock;
	events[6].kind = QCX_REPLAY_PHYSICS;
	assert(!QCX_ReplayTapeValidate(&tape));
	events[6].kind = QCX_REPLAY_COMMAND;
	events[6].sequence = 5;
	assert(!QCX_ReplayTapeValidate(&tape));
	events[6].sequence = 6;
	events[7].kind = QCX_REPLAY_FRAME_END;
	assert(!QCX_ReplayTapeValidate(&tape));
	events[7].kind = QCX_REPLAY_GROUP_END;
	tape.timed_begin = 6;
	assert(!QCX_ReplayTapeValidate(&tape));
	tape.timed_begin = 10;
	assert(QCX_ReplayTapeValidate(&tape));
	events[6].command.angles[0] = INFINITY;
	assert(!QCX_ReplayTapeValidate(&tape));
	events[6].command.angles[0] = 90.25f;
	events[6].rng_draws = 0;
	assert(!QCX_ReplayTapeValidate(&tape));
	events[6].rng_draws = 6;
	tape.config[0] = 0;
	assert(!QCX_ReplayTapeValidate(&tape));
	strcpy(tape.config, "deathmatch=4;sv_antilag=1;sv_speedcheck=0;sv_minping=0");
	assert(!QCX_ReplayTapeValidate(&tape));
	strcpy(tape.config, "deathmatch=4;sv_antilag=0;sv_speedcheck=0;sv_minping=0");
	tape.host_seed = 2;
	assert(!QCX_ReplayTapeValidate(&tape));
	tape.host_seed = 1;
	tape.guest_seed = 123;
	assert(!QCX_ReplayTapeValidate(&tape));
	tape.guest_seed = 0xadc0de;
	events[6].kind = (qcx_replay_kind_t)999;
	assert(!QCX_ReplayTapeValidate(&tape));
	events[6].kind = QCX_REPLAY_COMMAND;
	assert(QCX_ReplayTapeWrite(path, &tape));
	FILE *bad = fopen(path, "r+b");
	assert(bad != NULL);
	assert(fputc('?', bad) != EOF);
	fclose(bad);
	assert(!QCX_ReplayTapeLoad(path, &decoded));
	assert(decoded.count == 0 && decoded.events == NULL);
	assert(QCX_ReplayTapeWrite(path, &tape));
	bad = fopen(path, "ab");
	assert(bad != NULL && fputc(1, bad) != EOF);
	fclose(bad);
	assert(!QCX_ReplayTapeLoad(path, &decoded));
	assert(QCX_ReplayTapeWrite(path, &tape));

	FILE *file = fopen(path, "r+b");
	assert(file != NULL);
	assert(fseek(file, -1, SEEK_END) == 0);
	long shorter = ftell(file);
	assert(shorter > 0 && ftruncate(fileno(file), shorter) == 0);
	fclose(file);
	assert(!QCX_ReplayTapeLoad(path, &decoded));
	assert(decoded.events == NULL && decoded.count == 0);
	assert(strlen(QCX_ReplayTapeError()) > 0);
	unlink(path);
	puts("tape: roundtrip, ordering, empty frames and malformed inputs passed");
	return 0;
}
