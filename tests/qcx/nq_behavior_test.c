/* Real QCX boundary + PF2 setmodel; substitute only engine storage/output. */
#include "qwsvdef.h"
#include "qcx/adapter.h"
#include "vm.h"

server_t sv;
cvar_t deathmatch;
int current_skill;
qbool sv_error;
static entvars_t state;
static edict_t entity;
static cmodel_t inline_model;
static qbool netquake = true;
static float game_deathmatch;
static int links;
static char command[MAXCMDBUF];
static char model_name[MAX_QPATH];

qbool PR_GameIsNetQuake(void) { return netquake; }
qbool QCX_Active(void) { return true; }
float QCX_GameDeathmatch(void) { return game_deathmatch; }
edict_t *QCX_SlotToEdict(qcx_entity_id_t slot) { return slot == 1U ? &entity : NULL; }
void SV_Error(char *message, ...) { (void)message; abort(); }
void Con_Printf(char *message, ...) { (void)message; abort(); }
void PR1_SetString(string_t *target, char *text) { (void)target; (void)text; abort(); }
intptr_t PR2_EntityStringLocation(string_t offset, int size)
{ (void)offset; (void)size; abort(); }
intptr_t VM_ExplicitPtr2VM(vm_t *vm, void *ptr)
{ (void)vm; (void)ptr; abort(); }
void PR2_SetEntityString(edict_t *edict, string_t *target, char *text)
{
	assert(edict == &entity && target == &state.model);
	strlcpy(model_name, text, sizeof(model_name));
}
cmodel_t *CM_InlineModel(char *name)
{ assert(!strcmp(name, "*1")); return &inline_model; }
void SV_LinkEdict(edict_t *edict, qbool touch)
{ assert(edict == &entity && !touch); ++links; }
float Cvar_Value(const char *name) { (void)name; return 42.0f; }
void Cbuf_AddText(const char *text) { strlcpy(command, text, sizeof(command)); }
char *va(const char *format, ...)
{
	static char text[MAXCMDBUF];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	return text;
}

#include "../../src/qcx/services_world.c"

static void test_models(void)
{
	sv.model_precache[0] = "";
	sv.model_precache[1] = "progs/test.mdl";
	sv.model_precache[2] = "maps/b_explob.bsp";
	sv.model_precache[3] = "*1";
	entity.v = &state;
	QCX_SetModel(NULL, 1U, (const uint8_t *)"progs/test.mdl", 14U);
	assert(!strcmp(model_name, "progs/test.mdl") && state.modelindex == 1);
	assert(state.mins[0] == -16 && state.maxs[2] == 16 && state.size[1] == 32);
	assert(links == 1);
	QCX_SetModel(NULL, 1U, (const uint8_t *)"maps/b_explob.bsp", 17U);
	assert(state.mins[0] == 0 && state.maxs[2] == 64 && state.size[2] == 64);
	assert(links == 2);
	VectorSet(inline_model.mins, -10, -20, -30);
	VectorSet(inline_model.maxs, 40, 50, 60);
	QCX_SetModel(NULL, 1U, (const uint8_t *)"*1", 2U);
	assert(state.mins[1] == -20 && state.maxs[2] == 60 && state.size[2] == 90);
	assert(links == 3); /* The inline PF2 path already relinks exactly once. */
	netquake = false;
	QCX_SetModel(NULL, 1U, (const uint8_t *)"progs/test.mdl", 14U);
	assert(state.mins[1] == -20 && links == 3); /* QW leaves non-inline bounds alone. */
}

static void test_cvars_and_commands(void)
{
	netquake = true;
	deathmatch.value = 8; /* Deliberately disagrees with the live game value. */
	game_deathmatch = 0;
	assert(QCX_Cvar(NULL, (const uint8_t *)"timelimit", 9U) == 0);
	assert(QCX_Cvar(NULL, (const uint8_t *)"samelevel", 9U) == 0);
	assert(QCX_Cvar(NULL, (const uint8_t *)"skill", 5U) == 42);
	game_deathmatch = 1;
	deathmatch.value = 0;
	assert(QCX_Cvar(NULL, (const uint8_t *)"samelevel", 9U) == 42);
	assert(QCX_Cvar(NULL, (const uint8_t *)"PR_CHECKEXTENSION", 17U) == 1);
	strlcpy(sv.mapname, "e1m2", sizeof(sv.mapname));
	QCX_LocalCmd(NULL, (const uint8_t *)"restart\n", 8U);
	assert(!strcmp(command, "map e1m2\n"));
	QCX_LocalCmd(NULL, (const uint8_t *)"restart", 7U);
	assert(!strcmp(command, "restart"));
	netquake = false;
	game_deathmatch = 0;
	assert(QCX_Cvar(NULL, (const uint8_t *)"samelevel", 9U) == 42);
	assert(QCX_Cvar(NULL, (const uint8_t *)"PR_CHECKEXTENSION", 17U) == 42);
	QCX_LocalCmd(NULL, (const uint8_t *)"restart\n", 8U);
	assert(!strcmp(command, "restart\n"));
}

int main(void)
{
	test_models();
	test_cvars_and_commands();
	return 0;
}
