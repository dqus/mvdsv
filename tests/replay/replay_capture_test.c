#define _POSIX_C_SOURCE 200809L
#include "qcx/replay_probe.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void SV_QCXReplayApplyClock(const qcx_replay_clock_t *clock) { (void)clock; }

int main(void)
{
	char path[] = "/tmp/qcx-capture-test.XXXXXX";
	int fd = mkstemp(path);
	assert(fd >= 0);
	close(fd);
	char *argv[] = {"mvdsv", "-qcx-probe-record", path};
	assert(QCX_ReplayConfigure(3, argv));
	qcx_replay_tape_t header = {
		.guest_seed = 0xadc0de, .host_seed = 1, .clients = 2, .map = "povdmm4",
		.asset_hash = "fixture", .config = "deathmatch=4;sv_antilag=0;sv_speedcheck=0;sv_minping=0"
	};
	assert(QCX_ReplayCaptureStart(&header));
	qcx_replay_event_t event = {.slot = QCX_REPLAY_NO_SLOT, .clock = {1,2,3}};
	const qcx_replay_kind_t kinds[] = {
		QCX_REPLAY_MAP, QCX_REPLAY_FRAME_BEGIN, QCX_REPLAY_ACCEPT, QCX_REPLAY_ACCEPT,
		QCX_REPLAY_SETUP, QCX_REPLAY_SETUP, QCX_REPLAY_BEGIN, QCX_REPLAY_BEGIN,
		QCX_REPLAY_GROUP_BEGIN, QCX_REPLAY_COMMAND, QCX_REPLAY_COMMAND,
		QCX_REPLAY_GROUP_END, QCX_REPLAY_PHYSICS, QCX_REPLAY_FRAME_END, QCX_REPLAY_END
	};
	uint8_t info[] = "name";
	for (size_t i = 0; i < sizeof(kinds)/sizeof(kinds[0]); ++i) {
		event.kind = kinds[i];
		event.slot = (i >= 2 && i <= 7) ? (uint32_t)(i%2) :
			(i >= 8 && i <= 11) ? 0 : QCX_REPLAY_NO_SLOT;
		event.payload = i == 2 ? info : NULL;
		event.payload_size = i == 2 ? sizeof(info) : 0;
		event.command = (qcx_replay_command_t){.msec = 100, .buttons = 1, .forwardmove = 400};
		QCX_ReplayRecordEvent(&event);
		event.command.forwardmove = -400; /* RunCmd may mutate the input after capture. */
		if (i == 2) {
			info[0] = 'X';
		}
	}
	assert(QCX_ReplayCaptureCount() == 15);
	assert(QCX_ReplayCaptureFinish(1, 13));
	qcx_replay_tape_t decoded = {0};
	assert(QCX_ReplayTapeLoad(path, &decoded));
	assert(decoded.events[2].payload[0] == 'n');
	assert(decoded.events[9].command.forwardmove == 400);
	assert(decoded.events[10].command.msec == 100);
	assert(decoded.events[8].kind == QCX_REPLAY_GROUP_BEGIN);
	assert(decoded.events[11].kind == QCX_REPLAY_GROUP_END);
	assert(decoded.events[2].slot == 0 && decoded.events[3].slot == 1);
	decoded.events[7].kind = QCX_REPLAY_USERINFO; /* Accepted is not spawned. */
	assert(!QCX_ReplayTapeValidate(&decoded));
	decoded.events[7].kind = QCX_REPLAY_BEGIN;
	QCX_ReplayTapeFree(&decoded);
	unlink(path);
	puts("capture: interleaved bootstrap, pre-mutation ownership and long/recovered group preserved");
	return 0;
}
