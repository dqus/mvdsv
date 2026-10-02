#include "qcx/replay_tape.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static char error[192];

static int Fail(const char *message)
{
	snprintf(error, sizeof(error), "%s", message);
	return 0;
}

const char *QCX_ReplayTapeError(void) { return error; }

/* Capture writes canonical key=value pairs separated by ';'. These four
 * policies are part of this probe, not arbitrary inputs to its simulation. */
static int ConfigNumber(const char *config, const char *key, double expected)
{
	size_t length = strlen(key);
	int found = 0;
	for (const char *p = config; *p;) {
		const char *next = strchr(p, ';');
		if (!next) {
			next = p + strlen(p);
		}
		if ((size_t)(next - p) > length && !strncmp(p, key, length) && p[length] == '=') {
			char *end;
			double value = strtod(p + length + 1, &end);
			if (found || end == p + length + 1 || end != next || value != expected) {
				return 0;
			}
			found = 1;
		}
		p = *next ? next + 1 : next;
	}
	return found;
}

int QCX_ReplayTapeValidate(const qcx_replay_tape_t *tape)
{
	unsigned phase[QCX_REPLAY_MAX_CLIENTS] = {0};
	int frame = 0, physics = 0;
	uint32_t group = QCX_REPLAY_NO_SLOT;
	size_t commands = 0;
	error[0] = 0;
	if (!tape || !tape->events || tape->count < 4 || tape->count > QCX_REPLAY_MAX_EVENTS
		|| !tape->clients || tape->clients > QCX_REPLAY_MAX_CLIENTS
		|| tape->guest_seed > 0xffffffU
		|| !memchr(tape->map, 0, sizeof(tape->map)) || !tape->map[0]
		|| !memchr(tape->asset_hash, 0, sizeof(tape->asset_hash)) || !tape->asset_hash[0]
		|| !memchr(tape->config, 0, sizeof(tape->config)) || !tape->config[0]) {
		return Fail("invalid tape header");
	}
	if (strcmp(tape->map, "povdmm4") || tape->guest_seed != 0x00adc0deU || tape->host_seed != 1U
		|| !ConfigNumber(tape->config, "deathmatch", 4)
		|| !ConfigNumber(tape->config, "sv_speedcheck", 0)
		|| !ConfigNumber(tape->config, "sv_antilag", 0)
		|| !ConfigNumber(tape->config, "sv_minping", 0)) {
		return Fail("unsupported replay map, seeds or simulation policies");
	}
	if (tape->timed_begin >= tape->timed_end || tape->timed_end >= tape->count
		|| tape->events[tape->timed_begin].kind != QCX_REPLAY_FRAME_BEGIN
		|| tape->events[tape->timed_end].kind != QCX_REPLAY_FRAME_END) {
		return Fail("timed segment must span complete frames");
	}
	for (size_t i = 0; i < tape->count; ++i) {
		const qcx_replay_event_t *e = &tape->events[i];
		if (e->sequence != i || !isfinite(e->clock.sv_time)
			|| !isfinite(e->clock.realtime) || !isfinite(e->clock.curtime)
			|| e->clock.sv_time < 0 || e->clock.realtime < 0 || e->clock.curtime < 0
			|| e->payload_size > QCX_REPLAY_MAX_PAYLOAD || (e->payload_size && !e->payload)) {
			return Fail("invalid event sequence, clock or payload");
		}
		if (i && (e->rng_draws < tape->events[i-1].rng_draws
			|| e->clock.sv_time < tape->events[i-1].clock.sv_time
			|| e->clock.realtime < tape->events[i-1].clock.realtime
			|| e->clock.curtime < tape->events[i-1].clock.curtime)) {
			return Fail("event clocks or RNG ordinals went backwards");
		}
		int client_event = (e->kind >= QCX_REPLAY_ACCEPT && e->kind <= QCX_REPLAY_BEGIN)
			|| (e->kind >= QCX_REPLAY_GROUP_BEGIN && e->kind <= QCX_REPLAY_GROUP_END)
			|| e->kind == QCX_REPLAY_USERINFO || e->kind == QCX_REPLAY_OUTPUT;
		if ((client_event && e->slot >= tape->clients)
			|| (!client_event && e->slot != QCX_REPLAY_NO_SLOT)) {
			return Fail("invalid event client slot");
		}
		switch (e->kind) {
		case QCX_REPLAY_MAP:
			if (i != 0) {
				return Fail("map must be the first event");
			}
			break;
		case QCX_REPLAY_FRAME_BEGIN:
			if (!i || frame) {
				return Fail("nested or premature frame");
			}
			frame = 1;
			physics = 0;
			break;
		case QCX_REPLAY_ACCEPT: case QCX_REPLAY_SETUP: case QCX_REPLAY_BEGIN: {
			unsigned next = e->kind - QCX_REPLAY_ACCEPT + 1;
			if (!frame || physics || group != QCX_REPLAY_NO_SLOT || phase[e->slot] + 1 != next) {
				snprintf(error, sizeof(error), "invalid bootstrap order at %zu kind %u slot %u phase %u frame %u physics %u",
					i, (unsigned)e->kind, e->slot, phase[e->slot], frame, physics);
				return 0;
			}
			phase[e->slot] = next;
			break;
		}
		case QCX_REPLAY_GROUP_BEGIN:
			if (!frame || physics || group != QCX_REPLAY_NO_SLOT || phase[e->slot] != 3) {
				return Fail("group outside spawned-client command phase");
			}
			group = e->slot;
			commands = 0;
			break;
		case QCX_REPLAY_USERINFO:
			if (!frame || physics || group != QCX_REPLAY_NO_SLOT || phase[e->slot] == 0) {
				return Fail("userinfo outside admitted-client input phase");
			}
			break;
		case QCX_REPLAY_COMMAND:
			if (group != e->slot) {
				return Fail("command outside its move group");
			}
			for (int axis = 0; axis < 3; ++axis) {
				if (!isfinite(e->command.angles[axis])) {
					return Fail("nonfinite command angle");
				}
			}
			++commands;
			break;
		case QCX_REPLAY_GROUP_END:
			if (group != e->slot || !commands) {
				return Fail("unmatched or empty move group");
			}
			group = QCX_REPLAY_NO_SLOT;
			break;
		case QCX_REPLAY_PHYSICS:
			if (!frame || physics || group != QCX_REPLAY_NO_SLOT) {
				return Fail("physics inside group or outside frame");
			}
			physics = 1;
			break;
		case QCX_REPLAY_FRAME_END:
			if (!frame || !physics || group != QCX_REPLAY_NO_SLOT) {
				return Fail("incomplete frame");
			}
			frame = 0;
			break;
		case QCX_REPLAY_OUTPUT:
			if (!frame || !physics || group != QCX_REPLAY_NO_SLOT || phase[e->slot] != 3
				|| e->payload_size != 1 || e->payload[0] < 1 || e->payload[0] > 2) {
				return Fail("invalid client output bookkeeping");
			}
			break;
		case QCX_REPLAY_CHECKPOINT:
			if (!i || group != QCX_REPLAY_NO_SLOT || e->payload_size != 144) {
				return Fail("checkpoint inside move group");
			}
			break;
		case QCX_REPLAY_END:
			if (i + 1 != tape->count || frame || group != QCX_REPLAY_NO_SLOT) {
				return Fail("missing frame end or trailing events");
			}
			for (uint32_t slot = 0; slot < tape->clients; ++slot) {
				if (phase[slot] != 3) {
					return Fail("incomplete client bootstrap");
				}
			}
			break;
		default:
			return Fail("unknown event kind");
		}
	}
	if (tape->events[0].kind != QCX_REPLAY_MAP
		|| tape->events[tape->count-1].kind != QCX_REPLAY_END) {
		return Fail("missing map or end marker");
	}
	return 1;
}

/* Fixed little-endian words; floating point bits are copied, never type-punned. */
static int Word(FILE *f, uint64_t *value, unsigned bytes, int write)
{
	if (!write) {
		*value = 0;
	}
	for (unsigned i = 0; i < bytes; ++i) {
		if (write) {
			if (fputc((int)((*value >> (8*i)) & 255), f) == EOF) {
				return 0;
			}
		}
		else {
			int c = fgetc(f);
			if (c == EOF) {
				return 0;
			}
			*value |= (uint64_t)(unsigned)c << (8*i);
		}
	}
	return 1;
}

static int U32(FILE *f, uint32_t *v, int w)
{
	uint64_t word = *v;
	if (!Word(f, &word, 4, w)) {
		return 0;
	}
	*v = (uint32_t)word;
	return 1;
}

static int FloatWord(FILE *f, void *v, unsigned bytes, int w)
{
	uint64_t word = 0;
	if (w) {
		if (bytes == 4) {
			uint32_t bits;
			memcpy(&bits, v, 4);
			word = bits;
		}
		else {
			memcpy(&word, v, 8);
		}
	}
	if (!Word(f, &word, bytes, w)) {
		return 0;
	}
	if (!w) {
		if (bytes == 4) {
			uint32_t bits = (uint32_t)word;
			memcpy(v, &bits, 4);
		}
		else {
			memcpy(v, &word, 8);
		}
	}
	return 1;
}

static int Text(FILE *f, char *text, size_t capacity, int w)
{
	uint32_t size = w ? (uint32_t)strlen(text) : 0;
	if (!U32(f, &size, w) || size >= capacity) {
		return 0;
	}
	if (w) {
		return fwrite(text, 1, size, f) == size;
	}
	if (fread(text, 1, size, f) != size || memchr(text, 0, size)) {
		return 0;
	}
	text[size] = 0;
	return 1;
}

static int Command(FILE *f, qcx_replay_command_t *cmd, int w)
{
	uint8_t *small[] = {&cmd->msec, &cmd->buttons, &cmd->impulse};
	int16_t *movement[] = {&cmd->forwardmove, &cmd->sidemove, &cmd->upmove};
	for (int i = 0; i < 3; ++i) {
		uint64_t word = *small[i];
		if (!Word(f, &word, 1, w) || !FloatWord(f, &cmd->angles[i], 4, w)) {
			return 0;
		}
		*small[i] = (uint8_t)word;
		word = (uint16_t)*movement[i];
		if (!Word(f, &word, 2, w)) {
			return 0;
		}
		*movement[i] = (int16_t)(word >= 32768 ? (int)word - 65536 : (int)word);
	}
	return 1;
}

static int Codec(FILE *f, qcx_replay_tape_t *tape, int w)
{
	uint64_t magic = UINT64_C(0x3159504c52584351), count = tape->count;
	if (!Word(f, &magic, 8, w) || magic != UINT64_C(0x3159504c52584351)
		|| !U32(f, &tape->guest_seed, w) || !U32(f, &tape->host_seed, w)
		|| !U32(f, &tape->clients, w) || !Text(f, tape->map, sizeof(tape->map), w)
		|| !Text(f, tape->asset_hash, sizeof(tape->asset_hash), w)
		|| !Text(f, tape->config, sizeof(tape->config), w)
		|| !Word(f, &tape->timed_begin, 8, w) || !Word(f, &tape->timed_end, 8, w)
		|| !Word(f, &count, 8, w) || count > QCX_REPLAY_MAX_EVENTS) {
		return Fail("invalid or truncated tape header");
	}
	if (!w) {
		tape->count = (size_t)count;
		tape->events = calloc(tape->count, sizeof(*tape->events));
		if (!tape->events) {
			return Fail("could not allocate tape events");
		}
	}
	size_t payload_total = 0;
	for (size_t i = 0; i < tape->count; ++i) {
		qcx_replay_event_t *e = &tape->events[i];
		uint32_t kind = e->kind;
		if (!U32(f, &kind, w) || !U32(f, &e->slot, w)
			|| !Word(f, &e->sequence, 8, w) || !Word(f, &e->rng_draws, 8, w)
			|| !FloatWord(f, &e->clock.sv_time, 8, w)
			|| !FloatWord(f, &e->clock.realtime, 8, w)
			|| !FloatWord(f, &e->clock.curtime, 8, w)) {
			return Fail("truncated event boundary");
		}
		e->kind = (qcx_replay_kind_t)kind;
		if ((e->kind == QCX_REPLAY_COMMAND || e->kind == QCX_REPLAY_GROUP_END)
			&& !Command(f, &e->command, w)) {
			return Fail("truncated move command");
		}
		if (!U32(f, &e->payload_size, w) || e->payload_size > QCX_REPLAY_MAX_PAYLOAD) {
			return Fail("invalid event payload length");
		}
		payload_total += e->payload_size;
		if (payload_total > 64U*1024U*1024U) {
			return Fail("tape payload limit exceeded");
		}
		if (e->payload_size) {
			if (!w) {
				e->payload = malloc(e->payload_size);
				if (!e->payload) {
					return Fail("could not allocate event payload");
				}
			}
			size_t bytes = w ? fwrite(e->payload, 1, e->payload_size, f)
				: fread(e->payload, 1, e->payload_size, f);
			if (bytes != e->payload_size) {
				return Fail("truncated event payload");
			}
		}
	}
	return 1;
}

int QCX_ReplayTapeWrite(const char *path, const qcx_replay_tape_t *tape)
{
	if (!QCX_ReplayTapeValidate(tape)) {
		return 0;
	}
	FILE *f = fopen(path, "wb");
	if (!f) {
		return Fail("could not open tape output");
	}
	/* The codec's write branch never changes logical values. */
	qcx_replay_tape_t header = *tape;
	int ok = Codec(f, &header, 1);
	if (fclose(f) != 0) {
		ok = Fail("could not close tape output");
	}
	return ok;
}
int QCX_ReplayTapeLoad(const char *path, qcx_replay_tape_t *out)
{
	memset(out, 0, sizeof(*out));
	FILE *f = fopen(path, "rb");
	if (!f) {
		return Fail("could not open tape input");
	}
	int ok = Codec(f, out, 0);
	if (ok && (fgetc(f) != EOF || ferror(f))) {
		ok = Fail("trailing tape data or read error");
	}
	fclose(f);
	if (ok) {
		ok = QCX_ReplayTapeValidate(out);
	}
	if (!ok) {
		QCX_ReplayTapeFree(out);
		fprintf(stderr, "QCX replay tape: %s\n", error);
	}
	return ok;
}
void QCX_ReplayTapeFree(qcx_replay_tape_t *tape)
{
	if (tape->events) {
		for (size_t i = 0; i < tape->count; ++i) {
			free(tape->events[i].payload);
		}
	}
	free(tape->events);
	memset(tape, 0, sizeof(*tape));
}
