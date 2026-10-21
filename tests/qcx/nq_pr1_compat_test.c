/* Characterize the real PR1 wrappers/parser before extracting NQ behavior.
 * The test links real message encoding; only engine output/lookup boundaries
 * are substituted. Unexpected QW client/demo routing aborts. */
#include "qwsvdef.h"

server_t sv;
server_static_t svs;
qbool pr_nqprogs = true;
int pr_edict_size = sizeof(entvars_t);
int pr_argc;
static float vm_globals[128];
float *pr_globals = vm_globals;
globalvars_t *pr_global_struct = (globalvars_t *)vm_globals;
int pr_globaloffsetpatch[62];
cvar_t sv_specprint;

static char *strings[] = {"", "restart\n", "restart", "timelimit", "samelevel",
	"progs/test.mdl", "maps/b_explob.bsp", "*1", "hello%world"};
static char command[128], printed[128];
static int print_level, links, warnings;
static cmodel_t inline_model;
static entvars_t entity_states[2];
static byte reliable[4096];
static int particle_color, particle_count, particle_replacement, particle_replacement_count;
static unsigned particle_calls;

void SV_StartParticle(vec3_t origin, vec3_t direction, int color, int count,
	int replacement, int replacement_count)
{
	assert(origin[0] == 1.5f && origin[1] == -2.0f && origin[2] == 3.25f);
	assert(direction[0] == -0.5f && direction[1] == 0.25f && direction[2] == 1.0f);
	particle_color = color;
	particle_count = count;
	particle_replacement = replacement;
	particle_replacement_count = replacement_count;
	++particle_calls;
}

char *PR1_GetString(int index)
{
	assert(index >= 0 && (size_t)index < sizeof(strings) / sizeof(strings[0]));
	return strings[index];
}

int NUM_FOR_EDICT(edict_t *entity) { return (int)(entity - sv.edicts); }
edict_t *PROG_TO_EDICT(int reference)
{
	assert(reference >= 0 && reference % pr_edict_size == 0);
	assert(reference / pr_edict_size < 2);
	return &sv.edicts[reference / pr_edict_size];
}
void Cbuf_AddText(const char *text) { strlcpy(command, text, sizeof(command)); }
float Cvar_Value(const char *name) { (void)name; return 42.0f; }
void SV_LinkEdict(edict_t *entity, qbool touch)
{
	assert(entity == &sv.edicts[1] && !touch);
	++links;
}
cmodel_t *CM_InlineModel(char *name) { assert(!strcmp(name, "*1")); return &inline_model; }
void Con_Printf(char *format, ...) { (void)format; ++warnings; }
void Sys_Printf(char *format, ...) { (void)format; abort(); }
void Sys_Error(const char *format, ...) { (void)format; abort(); }
void SV_Error(char *format, ...) { (void)format; abort(); }
void PR_RunError(char *format, ...) { (void)format; abort(); }
void SV_BroadcastPrintf(int level, char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsnprintf(printed, sizeof(printed), format, args);
	va_end(args);
	print_level = level;
}
void SV_ClientPrintf(client_t *client, int level, char *format, ...)
{
	assert(client == &svs.clients[0]);
	va_list args;
	va_start(args, format);
	vsnprintf(printed, sizeof(printed), format, args);
	va_end(args);
	print_level = level;
}

/* Retained PF_Write functions contain QW branches too; reaching these means
 * the NQ routing regressed, not that the fixture should emulate a client. */
void ClientReliableCheckBlock(client_t *client, int size) { (void)client; (void)size; abort(); }
void ClientReliableWrite_Byte(client_t *client, int value) { (void)client; (void)value; abort(); }
void ClientReliableWrite_Coord(client_t *client, float value) { (void)client; (void)value; abort(); }
qbool MVDWrite_Begin(byte type, int to, int size) { (void)type; (void)to; (void)size; abort(); }
void MVD_MSG_WriteByte(const int value) { (void)value; abort(); }
void MVD_MSG_WriteCoord(const float value) { (void)value; abort(); }

/* Real static PF_setmodel and incremental parser, no production hooks. */
#include "../../src/pr_cmds.c"

static void reset_messages(void)
{
	SZ_Init(&sv.reliable_datagram, reliable, sizeof(reliable));
	NQ_ResetMessages();
}

static void check_bytes(const byte *expected, size_t count)
{
	assert(sv.reliable_datagram.cursize == (int)count);
	assert(!memcmp(reliable, expected, count));
}

static void write_byte(int destination, int value)
{
	G_FLOAT(OFS_PARM0) = (float)destination;
	G_FLOAT(OFS_PARM1) = (float)value;
	PF_WriteByte();
}

static void write_coord(float value)
{
	G_FLOAT(OFS_PARM0) = MSG_BROADCAST;
	G_FLOAT(OFS_PARM1) = value;
	PF_WriteCoord();
}

static void test_gunshot_and_reset(void)
{
	reset_messages();
	write_byte(MSG_BROADCAST, 23); /* svc_temp_entity */
	write_byte(MSG_BROADCAST, 2); /* TE_GUNSHOT */
	write_coord(1.0f);
	write_coord(-2.0f);
	assert(sv.reliable_datagram.cursize == 0);
	write_coord(3.0f);
	const byte expected[] = {23, 2, 1, 8, 0, 240, 255, 24, 0};
	check_bytes(expected, sizeof(expected));
	reset_messages();
	write_byte(MSG_BROADCAST, 23); /* partial command must not survive reset */
	reset_messages();
	write_byte(MSG_BROADCAST, 27); /* svc_killedmonster */
	const byte killed[] = {27};
	check_bytes(killed, sizeof(killed));
	reset_messages();
	/* Preserve PR1's exact float destination tests (QCX rejects fractions). */
	G_FLOAT(OFS_PARM0) = 1.5f;
	G_FLOAT(OFS_PARM1) = 27.0f;
	PF_WriteByte();
	check_bytes(killed, sizeof(killed));
}

static void test_destinations_unknown_and_incremental(void)
{
	reset_messages();
	write_byte(MSG_ONE, 27);
	write_byte(MSG_INIT, 27);
	assert(sv.reliable_datagram.cursize == 0);
	write_byte(MSG_ALL, 255);
	write_byte(MSG_ALL, 27);
	assert(warnings == 1 && sv.reliable_datagram.cursize == 0);
	reset_messages();
	write_byte(MSG_ALL, 32); /* cdtrack: discard NQ loop track */
	write_byte(MSG_ALL, 3);
	assert(sv.reliable_datagram.cursize == 0);
	write_byte(MSG_ALL, 9);
	const byte track[] = {32, 3};
	check_bytes(track, sizeof(track));
}

static void test_intermission_and_lightning(void)
{
	reset_messages();
	VectorSet(svs.clients[0].edict->v->origin, 1, -2, 3);
	VectorSet(svs.clients[0].edict->v->angles, 0, 90, 180);
	write_byte(MSG_ALL, 30);
	const byte intermission[] = {30, 8, 0, 240, 255, 24, 0, 0, 64, 128};
	check_bytes(intermission, sizeof(intermission));
	reset_messages();
	/* Build a complete NQ packet with real scalar encoders. */
	NQ_WriteByte(MSG_ALL, 23);
	NQ_WriteByte(MSG_ALL, 5); /* TE_LIGHTNING1 */
	NQ_WriteShort(MSG_ALL, 257);
	for (int index = 1; index <= 6; ++index) {
		NQ_WriteCoord(MSG_ALL, (float)index);
	}
	const byte lightning[] = {23, 5, 1, 1, 8, 0, 16, 0, 24, 0, 32, 0, 40, 0, 48, 0};
	check_bytes(lightning, sizeof(lightning));
	reset_messages();
	NQ_WriteByte(MSG_ALL, 3); /* NQ svc_updatestat -> QW long stat */
	NQ_WriteChar(MSG_ALL, -1);
	NQ_WriteLong(MSG_ALL, -123);
	const byte stat[] = {38, 255, 133, 255, 255, 255};
	check_bytes(stat, sizeof(stat));
}

static void test_particle(void)
{
	VectorSet(G_VECTOR(OFS_PARM0), 1.5f, -2.0f, 3.25f);
	VectorSet(G_VECTOR(OFS_PARM1), -0.5f, 0.25f, 1.0f);
	const struct { float color, count; int replacement, replacement_count; } cases[] = {
		{0, 255, TE_EXPLOSION, 0}, {73, 255, TE_EXPLOSION, 0},
		{73, 8.75f, TE_BLOOD, 1}, {225, 4, TE_LIGHTNINGBLOOD, 0},
		{73.5f, 8, 0, 0}, {73, 255.5f, TE_BLOOD, 1}, {40, 8, 0, 0},
	};
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		pr_argc = 4;
		G_FLOAT(OFS_PARM2) = cases[i].color;
		G_FLOAT(OFS_PARM3) = cases[i].count;
		PF_particle();
		assert(particle_color == (int)cases[i].color && particle_count == (int)cases[i].count);
		assert(particle_replacement == cases[i].replacement);
		assert(particle_replacement_count == cases[i].replacement_count);
	}
	pr_argc = 5;
	G_FLOAT(OFS_PARM4) = TE_GUNSHOT;
	PF_particle();
	assert(particle_replacement == TE_GUNSHOT && particle_replacement_count == 1);
	pr_argc = 6;
	G_FLOAT(OFS_PARM5) = 7.75f;
	PF_particle();
	assert(particle_replacement == TE_GUNSHOT && particle_replacement_count == 7);
	G_FLOAT(OFS_PARM4) = 0;
	PF_particle();
	assert(particle_replacement == 0 && particle_replacement_count == 7);
	assert(particle_calls == 10U);
}

static void test_builtins(void)
{
	G_INT(OFS_PARM0) = 1;
	PF_localcmd();
	assert(!strcmp(command, "map e1m2\n"));
	G_INT(OFS_PARM0) = 2;
	PF_localcmd();
	assert(!strcmp(command, "restart"));
	for (int index = 3; index <= 4; ++index) {
		G_INT(OFS_PARM0) = index;
		pr_globals[35] = 0;
		PF_cvar();
		assert(G_FLOAT(OFS_RETURN) == 0);
		pr_globals[35] = 1;
		PF_cvar();
		assert(G_FLOAT(OFS_RETURN) == 42);
	}
	pr_argc = 1;
	G_INT(OFS_PARM0) = 8;
	PF_bprint();
	assert(print_level == 2 && !strcmp(printed, "hello%world"));
	pr_argc = 2;
	G_INT(OFS_PARM0) = pr_edict_size;
	G_INT(OFS_PARM1) = 8;
	PF_sprint();
	assert(print_level == 2 && !strcmp(printed, "hello%world"));
	for (int index = 5; index <= 7; ++index) {
		links = 0;
		G_INT(OFS_PARM0) = pr_edict_size;
		G_INT(OFS_PARM1) = index;
		PF_setmodel();
		assert(links == 1 && entity_states[1].modelindex == index - 4);
		assert(entity_states[1].mins[0] == (index == 5 ? -16 : 0));
		assert(entity_states[1].maxs[2] == (index == 5 ? 16 : index == 6 ? 64 : 8));
		assert(entity_states[1].size[2] == (index == 5 ? 32 : index == 6 ? 64 : 8));
	}
}

int main(void)
{
	sv.edicts[0].v = &entity_states[0];
	sv.edicts[1].v = &entity_states[1];
	svs.clients[0].edict = &sv.edicts[1];
	strlcpy(sv.mapname, "e1m2", sizeof(sv.mapname));
	VectorSet(inline_model.maxs, 4, 6, 8);
	sv.model_precache[0] = "";
	sv.model_precache[1] = strings[5];
	sv.model_precache[2] = strings[6];
	sv.model_precache[3] = strings[7];
	test_gunshot_and_reset();
	test_destinations_unknown_and_incremental();
	test_intermission_and_lightning();
	test_builtins();
	test_particle();
	return 0;
}
