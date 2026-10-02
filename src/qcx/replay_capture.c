#include "qcx/replay_probe.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static qcx_replay_tape_t capture;
static size_t capacity;
static int active;
static char error[192];

int QCX_ReplayCaptureStart(const qcx_replay_tape_t *header)
{
	if (QCX_ReplayMode() != QCX_REPLAY_RECORD || !QCX_ReplayPath()) {
		return 0;
	}
	QCX_ReplayTapeFree(&capture);
	capture = *header;
	capture.events = NULL;
	capture.count = 0;
	capacity = 0;
	active = 1;
	error[0] = 0;
	return 1;
}
void QCX_ReplayRecordEvent(const qcx_replay_event_t *event)
{
	if (!active || error[0]) {
		return;
	}
	if (capture.count == QCX_REPLAY_MAX_EVENTS || event->payload_size > QCX_REPLAY_MAX_PAYLOAD
		|| (event->payload_size && !event->payload)) {
		snprintf(error, sizeof(error), "capture event/payload limit or invalid payload");
		return;
	}
	if (capture.count == capacity) {
		size_t next = capacity ? capacity * 2 : 1024;
		if (next > QCX_REPLAY_MAX_EVENTS) {
			next = QCX_REPLAY_MAX_EVENTS;
		}
		qcx_replay_event_t *events = realloc(capture.events, next * sizeof(*events));
		if (!events) {
			snprintf(error, sizeof(error), "could not allocate capture events");
			return;
		}
		capture.events = events;
		capacity = next;
	}
	qcx_replay_event_t copy = *event;
	copy.sequence = capture.count;
	copy.payload = NULL;
	if (copy.payload_size) {
		copy.payload = malloc(copy.payload_size);
		if (!copy.payload) {
			snprintf(error, sizeof(error), "could not allocate capture payload");
			return;
		}
		memcpy(copy.payload, event->payload, copy.payload_size);
	}
	capture.events[capture.count++] = copy;
}
int QCX_ReplayCaptureFinish(uint64_t timed_begin, uint64_t timed_end)
{
	if (!active || error[0]) {
		return 0;
	}
	capture.timed_begin = timed_begin;
	capture.timed_end = timed_end;
	int ok = QCX_ReplayTapeWrite(QCX_ReplayPath(), &capture);
	if (!ok) {
		snprintf(error, sizeof(error), "%s", QCX_ReplayTapeError());
	}
	active = 0;
	QCX_ReplayTapeFree(&capture);
	return ok;
}
size_t QCX_ReplayCaptureCount(void) { return capture.count; }
const char *QCX_ReplayCaptureError(void) { return error; }
const qcx_replay_tape_t *QCX_ReplayCapturedTape(void) { return &capture; }
void QCX_ReplayCaptureIdentity(uint32_t clients, const char *asset_identity)
{
	capture.clients = clients;
	snprintf(capture.asset_hash, sizeof(capture.asset_hash), "%s", asset_identity);
}
