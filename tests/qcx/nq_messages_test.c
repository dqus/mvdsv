#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <string.h>
#include "qwsvdef.h"
#include "qcx/entities.h"
#include "nq_messages.h"

server_t sv;
server_static_t svs;
static qbool netquake = true, through_qcx;
static unsigned qw_calls, qw_operations, warnings;
static int destination;
static byte reliable[4096];
static entvars_t states[258];
static jmp_buf rejected;
static qbool expect_rejection;
static unsigned particle_calls;
static int particle_replacement, particle_replacement_count;

void SV_StartParticle(vec3_t origin, vec3_t direction, int color, int count,
	int replacement, int replacement_count)
{
	assert(origin[0] == 1.5f && origin[1] == -2.0f && origin[2] == 3.25f);
	assert(direction[0] == -0.5f && direction[1] == 0.25f && direction[2] == 1.0f);
	(void)color;
	(void)count;
	particle_replacement = replacement;
	particle_replacement_count = replacement_count;
	++particle_calls;
}

qbool PR_GameIsNetQuake(void) { return netquake; }
edict_t *QCX_SlotToEdict(qcx_entity_id_t slot)
{
	return slot == 0U || slot == 1U || slot == 257U ? &sv.edicts[slot] : NULL;
}
int NUM_FOR_EDICT(edict_t *entity) { return (int)(entity - sv.edicts); }
void Con_Printf(char *format, ...) { (void)format; ++warnings; }
void Sys_Printf(char *format, ...) { (void)format; abort(); }
void Sys_Error(const char *format, ...) { (void)format; abort(); }
void SV_Error(char *format, ...)
{
	(void)format;
	assert(expect_rejection);
	longjmp(rejected, 1);
}

static void qw_write(int to, edict_t *entity, unsigned operation)
{
	assert(!netquake && (to == 0 || to == 1));
	assert(entity == (to == 1 ? &sv.edicts[1] : NULL));
	++qw_calls;
	qw_operations |= 1U << operation;
}
void PF2_WriteByte(int to, int value, edict_t *entity)
{ (void)value; qw_write(to, entity, 0U); }
void PF2_WriteChar(int to, int value, edict_t *entity)
{ (void)value; qw_write(to, entity, 1U); }
void PF2_WriteShort(int to, int value, edict_t *entity)
{ (void)value; qw_write(to, entity, 2U); }
void PF2_WriteLong(int to, int value, edict_t *entity)
{ (void)value; qw_write(to, entity, 3U); }
void PF2_WriteCoord(int to, float value, edict_t *entity)
{ (void)value; qw_write(to, entity, 4U); }
void PF2_WriteAngle(int to, float value, edict_t *entity)
{ (void)value; qw_write(to, entity, 5U); }
void PF2_WriteString(int to, char *value, edict_t *entity)
{ (void)value; qw_write(to, entity, 6U); }
void PF2_WriteEntity(int to, int value, edict_t *entity)
{ assert(value == 257); qw_write(to, entity, 7U); }

/* Exercise the actual QCX validation/routing functions, not a copied adapter. */
#include "../../src/qcx/services_network.c"

static void test_particle(void)
{
	const float origin[3] = {1.5f, -2.0f, 3.25f};
	const float direction[3] = {-0.5f, 0.25f, 1.0f};
	const struct { float color, count; int replacement, replacement_count; } cases[] = {
		{0, 255, TE_EXPLOSION, 0}, {73, 255, TE_EXPLOSION, 0},
		{73, 8.75f, TE_BLOOD, 1}, {225, 4, TE_LIGHTNINGBLOOD, 0},
		{73.5f, 8, 0, 0}, {73, 255.5f, TE_BLOOD, 1}, {40, 8, 0, 0},
	};
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		QCX_Particle(NULL, origin, direction, cases[i].color, cases[i].count);
		assert(particle_replacement == cases[i].replacement);
		assert(particle_replacement_count == cases[i].replacement_count);
	}
	assert(particle_calls == 7U);
	expect_rejection = true;
	if (setjmp(rejected) == 0) {
		QCX_Particle(NULL, NULL, direction, 73, 8);
		assert(!"NULL origin accepted");
	}
	if (setjmp(rejected) == 0) {
		QCX_Particle(NULL, origin, NULL, 73, 8);
		assert(!"NULL direction accepted");
	}
	if (setjmp(rejected) == 0) {
		QCX_Particle(NULL, origin, direction, NAN, 8);
		assert(!"NaN color accepted");
	}
	if (setjmp(rejected) == 0) {
		QCX_Particle(NULL, origin, direction, 73, INFINITY);
		assert(!"infinite count accepted");
	}
	assert(particle_calls == 7U);
	expect_rejection = false;
}

static void emit_byte(int value)
{
	if (through_qcx) {
		QCX_WriteByte(NULL, (float)destination, (float)value, 1U);
	} else {
		NQ_WriteByte(destination, value);
	}
}
static void emit_char(int value)
{
	if (through_qcx) {
		QCX_WriteChar(NULL, (float)destination, (float)value, 1U);
	} else {
		NQ_WriteChar(destination, value);
	}
}
static void emit_short(int value)
{
	if (through_qcx) {
		QCX_WriteShort(NULL, (float)destination, (float)value, 1U);
	} else {
		NQ_WriteShort(destination, value);
	}
}
static void emit_long(int value)
{
	if (through_qcx) {
		QCX_WriteLong(NULL, (float)destination, (float)value, 1U);
	} else {
		NQ_WriteLong(destination, value);
	}
}
static void emit_coord(float value)
{
	if (through_qcx) {
		QCX_WriteCoord(NULL, (float)destination, value, 1U);
	} else {
		NQ_WriteCoord(destination, value);
	}
}
static void emit_angle(float value)
{
	if (through_qcx) {
		QCX_WriteAngle(NULL, (float)destination, value, 1U);
	} else {
		NQ_WriteAngle(destination, value);
	}
}
static void emit_string(const char *value)
{
	if (through_qcx) {
		QCX_WriteString(NULL, (float)destination, (const uint8_t *)value,
			(qcx_byte_count_t)strlen(value), 1U);
	} else {
		NQ_WriteString(destination, value);
	}
}
static void emit_entity(void)
{
	if (through_qcx) {
		QCX_WriteEntity(NULL, (float)destination, 257U, 1U);
	} else {
		NQ_WriteEntity(destination, 257);
	}
}
static void reset(void)
{
	SZ_Init(&sv.reliable_datagram, reliable, sizeof(reliable));
	NQ_ResetMessages();
	destination = 0;
}
static void check(const byte *expected, size_t size)
{
	assert(sv.reliable_datagram.cursize == (int)size);
	assert(!memcmp(reliable, expected, size));
}
static void test_packets(void)
{
	reset();
	emit_byte(23);
	emit_byte(2);
	emit_coord(1);
	emit_coord(-2);
	assert(sv.reliable_datagram.cursize == 0);
	emit_coord(3);
	const byte gunshot[] = {23, 2, 1, 8, 0, 240, 255, 24, 0};
	check(gunshot, sizeof(gunshot));
	reset();
	emit_byte(3);
	emit_char(-1);
	emit_long(-123);
	const byte stat[] = {38, 255, 133, 255, 255, 255};
	check(stat, sizeof(stat));
	reset();
	emit_byte(14);
	emit_byte(3);
	emit_short(-123);
	const byte frags[] = {14, 3, 133, 255};
	check(frags, sizeof(frags));
	reset();
	emit_byte(23);
	emit_byte(5);
	emit_entity();
	for (int i = 1; i <= 6; ++i) {
		emit_coord((float)i);
	}
	const byte lightning[] = {23, 5, 1, 1, 8, 0, 16, 0, 24, 0, 32, 0, 40, 0, 48, 0};
	check(lightning, sizeof(lightning));
	reset();
	emit_byte(23);
	emit_byte(12); /* NQ_TE_EXPLOSION2 becomes an ordinary QW explosion. */
	for (int i = 1; i <= 3; ++i) {
		emit_coord((float)i);
	}
	emit_angle(90);
	emit_angle(180);
	const byte explosion[] = {23, 3, 8, 0, 16, 0, 24, 0};
	check(explosion, sizeof(explosion));
	reset();
	emit_byte(8);
	emit_string("hello");
	const byte print[] = {8, 2, 'h', 'e', 'l', 'l', 'o', 0};
	check(print, sizeof(print));
	reset();
	emit_byte(32);
	emit_byte(3);
	emit_byte(9);
	const byte cdtrack[] = {32, 3};
	check(cdtrack, sizeof(cdtrack));
	reset();
	emit_byte(30);
	const byte intermission[] = {30, 8, 0, 240, 255, 24, 0, 0, 64, 128};
	check(intermission, sizeof(intermission));
}
static void test_explosion2_message_boundary(void)
{
	reset();
	/* Exact NQ effect from AD ad_e1m1, Wasm sequence 2171. */
	emit_byte(23);
	emit_byte(12);
	emit_coord(-925.625f);
	emit_coord(-26.375f);
	emit_coord(-32.0f);
	assert(sv.reliable_datagram.cursize == 0);
	emit_byte(35);
	assert(sv.reliable_datagram.cursize == 0);
	emit_byte(8);
	const byte explosion[] = {23, 3, 19, 227, 45, 255, 0, 255};
	check(explosion, sizeof(explosion));
	/* Both palette bytes must be consumed, not treated as subsequent opcodes. */
	emit_byte(27);
	const byte followed[] = {23, 3, 19, 227, 45, 255, 0, 255, 27};
	check(followed, sizeof(followed));
}
static void test_centerprint_message_boundary(void)
{
	for (unsigned to = 0U; to < 2U; ++to) {
		reset();
		destination = to == 0U ? 0 : 2;
		const unsigned before = warnings;
		emit_byte(svc_centerprint);
		assert(sv.reliable_datagram.cursize == 0 && warnings == before);
		emit_string("hello");
		const byte text[] = {svc_centerprint, 'h', 'e', 'l', 'l', 'o', 0};
		check(text, sizeof(text));
		emit_byte(svc_foundsecret);
		const byte followed[] = {svc_centerprint, 'h', 'e', 'l', 'l', 'o', 0,
			svc_foundsecret};
		check(followed, sizeof(followed));

		reset();
		destination = to == 0U ? 0 : 2;
		emit_byte(svc_centerprint);
		emit_char('o');
		emit_char('k');
		assert(sv.reliable_datagram.cursize == 0);
		emit_byte(0);
		const byte fragmented[] = {svc_centerprint, 'o', 'k', 0};
		check(fragmented, sizeof(fragmented));
		emit_byte(svc_centerprint);
		emit_string("");
		emit_byte(svc_killedmonster);
		const byte cleared[] = {svc_centerprint, 'o', 'k', 0,
			svc_centerprint, 0, svc_killedmonster};
		check(cleared, sizeof(cleared));
		assert(warnings == before);
	}
}
static void test_reset_and_ignored_destinations(void)
{
	reset();
	emit_byte(23);
	reset();
	emit_byte(27);
	const byte killed[] = {27};
	check(killed, sizeof(killed));
	reset();
	const unsigned before = warnings;
	emit_byte(255);
	emit_byte(27);
	assert(warnings == before + 1U && sv.reliable_datagram.cursize == 0);
	reset();
	emit_byte(27);
	check(killed, sizeof(killed));
	for (unsigned i = 0U; i < 2U; ++i) {
		reset();
		destination = i == 0U ? 1 : 3;
		emit_byte(27); emit_char(-1); emit_short(-123); emit_long(-123);
		emit_coord(1); emit_angle(90); emit_string("ignored"); emit_entity();
		assert(sv.reliable_datagram.cursize == 0);
	}
}
int main(void)
{
	test_particle();
	sv.edicts[0].v = &states[0];
	sv.edicts[1].v = &states[1];
	sv.edicts[257].v = &states[257];
	svs.clients[0].edict = &sv.edicts[1];
	VectorSet(states[1].origin, 1, -2, 3);
	VectorSet(states[1].angles, 0, 90, 180);
	for (unsigned route = 0U; route < 2U; ++route) {
		through_qcx = route != 0U;
		test_packets();
		test_explosion2_message_boundary();
		test_centerprint_message_boundary();
		test_reset_and_ignored_destinations();
	}
	through_qcx = true;
	netquake = false;
	reset();
	emit_byte(27); emit_char(-1); emit_short(-123); emit_long(-123);
	emit_coord(1); emit_angle(90); emit_string("direct"); emit_entity();
	assert(qw_calls == 8U && qw_operations == 255U && sv.reliable_datagram.cursize == 0);
	destination = 1;
	emit_byte(27);
	assert(qw_calls == 9U);
	netquake = true;
	reset();
	expect_rejection = true;
	if (setjmp(rejected) == 0) {
		QCX_WriteByte(NULL, 0.5f, 27, 0U);
		assert(!"fractional destination accepted");
	}
	if (setjmp(rejected) == 0) {
		QCX_WriteLong(NULL, 0, NAN, 0U);
		assert(!"nonfinite integer accepted");
	}
	if (setjmp(rejected) == 0) {
		QCX_WriteEntity(NULL, 0, UINT32_MAX, 0U);
		assert(!"invalid entity accepted");
	}
	assert(sv.reliable_datagram.cursize == 0);
	return 0;
}
