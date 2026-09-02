#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>

#include "qwsvdef.h"
#include "qcx/entries.h"
#include "qcx/globals.h"
#include "qcx/services.h"

static entvars_t entity_states[8];
static edict_t entities[8];
static qcx_entity_id_t observed_first;
static qcx_entity_id_t observed_second;
static float observed_time;
static float observed_frametime;
static qcx_shared_global_state_v1_t globals;
static trace_t trace_result;
static float observed_yaw;
static float observed_distance;
static qbool bottom_result;
static int contents_result;
static qbool step_result;
static int touch_call_count;
static int think_call_count;
static qbool mutate_touch_time;
vec3_t vec3_origin;
server_t sv;
server_static_t svs;
globalvars_t legacy_globals;
globalvars_t *pr_global_struct = &legacy_globals;
float *pr_globals = (float *)&legacy_globals;
int pr_edict_size;
extern double sv_frametime;
client_t *sv_client;
edict_t *sv_player;
movevars_t movevars;
cvar_t sv_mintic;
cvar_t sv_maxtic;
cvar_t sv_maxfps;

void SV_Error(char *error, ...)
{
	(void)error;
	abort();
}

void Con_DPrintf(char *format, ...) { (void)format; }
void Cvar_SetROM(cvar_t *var, char *value) { (void)var; (void)value; }
int Q_atoi(const char *text) { return atoi(text); }
char *PR2_GetEntityString(string_t value) { (void)value; return ""; }
qcx_plugin_status_t QCX_CopyEntityString(const edict_t *entity, const char *field,
	char *out, uint32_t capacity, uint32_t *required)
{
	(void)entity; (void)field; (void)out; (void)capacity; (void)required;
	return QCX_PLUGIN_BAD_ARGUMENT;
}
void PR2_GameStartFrame(qbool is_bot_frame) { (void)is_bot_frame; }
void SV_LinkEdict(edict_t *entity, qbool touch) { (void)entity; (void)touch; }
void SV_PreRunCmd(void) { }
void SV_RunCmd(usercmd_t *command, qbool inside, qbool simulate)
{ (void)command; (void)inside; (void)simulate; }
void SV_PostRunCmd(void) { }
void SV_StartSound(edict_t *entity, int channel, char *sample, int volume,
	float attenuation)
{ (void)entity; (void)channel; (void)sample; (void)volume; (void)attenuation; }
edict_t *SV_TestEntityPosition(edict_t *entity) { (void)entity; return NULL; }
vec_t VectorLength(vec3_t vector)
{ return sqrtf(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]); }
void VectorMA(vec3_t a, float scale, vec3_t b, vec3_t out)
{ out[0] = a[0] + scale * b[0]; out[1] = a[1] + scale * b[1]; out[2] = a[2] + scale * b[2]; }
void CrossProduct(vec3_t a, vec3_t b, vec3_t out)
{ out[0] = a[1] * b[2] - a[2] * b[1]; out[1] = a[2] * b[0] - a[0] * b[2]; out[2] = a[0] * b[1] - a[1] * b[0]; }

qcx_entity_id_t QCX_EdictToSlot(const edict_t *entity)
{
	if (entity >= entities && entity < entities + 8) {
		return (qcx_entity_id_t)(entity - entities);
	}
	return QCX_INVALID_ENTITY_ID;
}

edict_t *QCX_SlotToEdict(qcx_entity_id_t slot)
{
	return slot < 8U ? &entities[slot] : NULL;
}

qcx_shared_global_state_v1_t *QCX_Globals(void) { return &globals; }
qbool QCX_Active(void) { return true; }

void QCX_EdictTouch(qcx_entity_id_t first, qcx_entity_id_t second, float time,
	float frametime)
{
	observed_first = first;
	observed_second = second;
	observed_time = time;
	observed_frametime = frametime;
	if (mutate_touch_time && ++touch_call_count == 1) {
		globals.time = 17.0f;
	}
}

void QCX_EdictThink(qcx_entity_id_t self, float time, float frametime)
{
	QCX_EdictTouch(self, QCX_INVALID_ENTITY_ID, time, frametime);
	++think_call_count;
}

void QCX_EdictBlocked(qcx_entity_id_t first, qcx_entity_id_t second, float time,
	float frametime)
{
	QCX_EdictTouch(first, second, time, frametime);
}

void QCX_ClientConnect(qcx_entity_id_t self, uint32_t spectator)
{ (void)self; (void)spectator; }
void QCX_PutClientInServer(qcx_entity_id_t self, uint32_t spectator)
{ (void)self; (void)spectator; }
void QCX_ClientDisconnect(qcx_entity_id_t self, uint32_t spectator)
{ (void)self; (void)spectator; }
uint32_t QCX_ClientUserInfoChanged(qcx_entity_id_t self, uint32_t after)
{ (void)self; (void)after; return 0U; }
uint32_t QCX_ClientCommand(qcx_entity_id_t self) { (void)self; return 0U; }
void QCX_ClientKill(qcx_entity_id_t self) { (void)self; }
uint32_t QCX_ClientSay(qcx_entity_id_t self, uint32_t team, const uint8_t *text,
	qcx_byte_count_t size)
{ (void)self; (void)team; (void)text; (void)size; return 0U; }
void QCX_ClientPreThink(qcx_entity_id_t self, float time, float frametime,
	uint32_t spectator)
{ (void)self; (void)time; (void)frametime; (void)spectator; }
void QCX_ClientPostThink(qcx_entity_id_t self, float time, uint32_t spectator)
{ (void)self; (void)time; (void)spectator; }
void QCX_SetNewParms(float out_parms[16]) { (void)out_parms; }
void QCX_SetChangeParms(qcx_entity_id_t self, float out_parms[16])
{ (void)self; (void)out_parms; }

trace_t SV_Trace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int type,
	edict_t *passedict)
{
	(void)start; (void)mins; (void)maxs; (void)end; (void)type; (void)passedict;
	return trace_result;
}

edict_t *SV_QC_CheckClient(edict_t *self)
{
	assert(self == &entities[3]);
	return &entities[4];
}

float SV_QC_WalkMove(edict_t *entity, float yaw, float distance)
{
	assert(entity == &entities[3]);
	observed_yaw = yaw;
	observed_distance = distance;
	globals.self = 6U;
	globals.other = 7U;
	globals.time = 29.0f;
	return 0.75f;
}

float SV_QC_DropToFloor(edict_t *entity)
{
	assert(entity == &entities[3]);
	return 1.0f;
}

qbool SV_CheckBottom(edict_t *entity)
{
	assert(entity == &entities[3]);
	return bottom_result;
}

int SV_PointContents(vec3_t point)
{
	assert(point[0] == 1.0f && point[1] == 2.0f && point[2] == 3.0f);
	return contents_result;
}

qbool SV_StepDirection(edict_t *entity, float yaw, float distance)
{
	assert(entity == &entities[3]);
	observed_yaw = yaw;
	observed_distance = distance;
	return step_result;
}

void PR2_EdictTouch(func_t function)
{
	(void)function;
	QCX_DispatchEdictTouch(QCX_SlotToEdict((qcx_entity_id_t)PR_Global_self_word()),
		QCX_SlotToEdict((qcx_entity_id_t)PR_Global_other_word()), *PR_Global_time(),
		*PR_Global_frametime());
}

void PR2_EdictThink(func_t function)
{
	(void)function;
	QCX_DispatchEdictThink(QCX_SlotToEdict((qcx_entity_id_t)PR_Global_self_word()),
		*PR_Global_time(), *PR_Global_frametime());
}

void PR2_EdictBlocked(func_t function) { (void)function; }

int main(void)
{
	for (int index = 0; index < 8; ++index) {
		entities[index].v = &entity_states[index];
		entities[index].e.entnum = index;
	}
	sv.max_edicts = 8;
	QCX_DispatchEdictTouch(&entities[3], &entities[4], 17.0f, 0.125f);
	assert(observed_first == 3U && observed_second == 4U);
	assert(observed_time == 17.0f && observed_frametime == 0.125f);

	QCX_DispatchEdictThink(&entities[5], 19.0f, 0.25f);
	assert(observed_first == 5U && observed_second == QCX_INVALID_ENTITY_ID);
	assert(observed_time == 19.0f && observed_frametime == 0.25f);

	QCX_DispatchEdictBlocked(&entities[6], &entities[7], 23.0f, 0.5f);
	assert(observed_first == 6U && observed_second == 7U);
	assert(observed_time == 23.0f && observed_frametime == 0.5f);

	qcx_host_api_v1_t host = {0};
	QCX_BindMovementServices(&host);
	assert(host.traceline != NULL && host.checkclient != NULL && host.walkmove != NULL);
	assert(host.droptofloor != NULL && host.checkbottom != NULL);
	assert(host.pointcontents != NULL && host.aim != NULL && host.step_direction != NULL);
	trace_result.allsolid = 1.0f;
	trace_result.fraction = 0.25f;
	trace_result.e.ent = &entities[4];
	trace_result.endpos[2] = 8.0f;
	trace_result.plane.normal[1] = 1.0f;
	trace_result.plane.dist = 9.0f;
	qcx_trace_result_v1_t result = {0};
	const float start[3] = {0.0f, 0.0f, 0.0f};
	const float end[3] = {1.0f, 2.0f, 3.0f};
	host.traceline(NULL, start, end, 0.0f, 3U, &result, sizeof(result));
	assert(result.allsolid == 1.0f && result.fraction == 0.25f && result.entity == 4U);
	assert(result.endpos[2] == 8.0f && result.plane_normal[1] == 1.0f);
	assert(result.plane_dist == 9.0f);
	assert(host.checkclient(NULL, 3U) == 4U);
	globals.self = 3U;
	globals.other = 2U;
	globals.time = 11.0f;
	assert(host.walkmove(NULL, 3U, 45.0f, 12.0f) == 0.75f);
	assert(observed_yaw == 45.0f && observed_distance == 12.0f);
	assert(globals.self == 3U && globals.other == 7U && globals.time == 29.0f);
	assert(host.droptofloor(NULL, 3U) == 1.0f);
	bottom_result = true;
	assert(host.checkbottom(NULL, 3U) == 1.0f);
	contents_result = -3;
	const float point[3] = {1.0f, 2.0f, 3.0f};
	assert(host.pointcontents(NULL, point) == -3.0f);
	float aimed[3] = {0.0f, 0.0f, 0.0f};
	const float forward[3] = {4.0f, 5.0f, 6.0f};
	host.aim(NULL, 3U, 0.0f, forward, aimed, sizeof(aimed));
	assert(aimed[0] == 4.0f && aimed[2] == 6.0f);
	step_result = true;
	assert(host.step_direction(NULL, 3U, 90.0f, 20.0f) == 1U);
	assert(observed_yaw == 90.0f && observed_distance == 20.0f);

	globals.self = 1U;
	globals.other = 2U;
	globals.time = 0.0f;
	globals.frametime = 0.125f;
	sv.time = 3.0;
	entities[3].v->touch = 1;
	entities[3].v->solid = SOLID_BBOX;
	entities[4].v->touch = 1;
	entities[4].v->solid = SOLID_BBOX;
	touch_call_count = 0;
	mutate_touch_time = true;
	SV_Impact(&entities[3], &entities[4]);
	mutate_touch_time = false;
	assert(touch_call_count == 2);
	assert(observed_first == 4U && observed_second == 3U);
	assert(observed_time == 17.0f && observed_frametime == 0.125f);
	assert(globals.self == 1U && globals.other == 2U && globals.time == 17.0f);

	entities[5].v->nextthink = 1.0f;
	entities[5].v->think = 1;
	sv_frametime = 0.5;
	think_call_count = 0;
	assert(SV_RunThink(&entities[5]));
	assert(think_call_count == 1 && entities[5].v->nextthink == 0.0f);
	assert(observed_first == 5U && observed_time == 3.0f);
	assert(globals.self == 5U && globals.other == 0U && globals.time == 3.0f);
	return 0;
}
