#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "qwsvdef.h"
#include "game/plugin_api.h"
#include "qcx/entities.h"
#include "qcx/services.h"

server_t sv;
server_static_t svs;
cvar_t deathmatch;
int current_skill;

static entvars_t test_entity_state;
static edict_t test_entity;
static entvars_t test_spawned_state;
static edict_t test_spawned;
static int linked;
static int antilag_reset;
static int freed;
static int precached_model;
static int precached_sound;
static int cvar_set;
static char queued_command[MAXCMDBUF];
static char entity_model[MAX_QPATH];

void SV_Error(char *error, ...)
{
	(void)error;
	abort();
}

edict_t *QCX_SlotToEdict(qcx_entity_id_t slot) { return slot == 0U ? &test_entity : slot == 1U ? &test_spawned : NULL; }
qcx_entity_id_t QCX_EdictToSlot(const edict_t *entity) { return entity == &test_entity ? 0U : entity == &test_spawned ? 1U : QCX_INVALID_ENTITY_ID; }
void SV_AntilagReset(edict_t *entity) { assert(entity == &test_entity); ++antilag_reset; }
void SV_LinkEdict(edict_t *entity, qbool touch) { assert(entity == &test_entity); assert(!touch); ++linked; }
int SV_QC_SetModel(edict_t *entity, const char *name) { assert(entity == &test_entity); assert(!strcmp(name, entity_model)); entity->v->modelindex = 3; return 1; }
int SV_QC_PrecacheSound(const char *name) { assert(!strcmp(name, "sound/test.wav")); ++precached_sound; return 1; }
int SV_QC_PrecacheModel(const char *name) { assert(!strcmp(name, "progs/test.mdl")); ++precached_model; return 1; }
void SV_QC_LightStyle(int style, const char *value) { assert(style == 2); assert(!strcmp(value, "abc")); }
void SV_QC_MakeStatic(edict_t *entity, const char *model_name) { assert(entity == &test_entity); assert(!strcmp(model_name, entity_model)); }
void SV_QC_ChangeLevel(const char *map) { assert(!strcmp(map, "dm6")); }
edict_t *ED_Alloc(void) { test_spawned.e.free = false; return &test_spawned; }
void ED_Free(edict_t *entity) { assert(entity == &test_spawned); entity->e.free = true; ++freed; }
int QCX_SetEntityString(edict_t *entity, const char *field, const char *value) { assert(entity == &test_entity); assert(!strcmp(field, "model")); strlcpy(entity_model, value, sizeof(entity_model)); return 1; }
qcx_plugin_status_t QCX_CopyEntityString(const edict_t *entity, const char *field, char *out, uint32_t capacity, uint32_t *required) { assert(entity == &test_entity); assert(!strcmp(field, "model")); assert(capacity > strlen(entity_model)); strcpy(out, entity_model); if (required != NULL) *required = (uint32_t)strlen(entity_model) + 1U; return QCX_PLUGIN_OK; }
void *Hunk_AllocName(int size, const char *name) { static char storage[4][MAX_QPATH]; static int next; assert(size <= MAX_QPATH); assert(!strcmp(name, "qc2cpp")); return storage[next++ % 4]; }
void SV_FlushSignon(void) { }
float Cvar_Value(const char *name) { assert(!strcmp(name, "skill")); return 2.0f; }
cvar_t *Cvar_Find(const char *name) { assert(!strcmp(name, "skill")); return &deathmatch; }
void Cvar_Set(cvar_t *var, char *value) { assert(var == &deathmatch); assert(!strcmp(value, "3")); ++cvar_set; }
void Cbuf_AddText(const char *text) { strlcpy(queued_command, text, sizeof(queued_command)); }
void Con_Printf(char *format, ...) { (void)format; abort(); }

static void test_unpublish(void *context) { (void)context; }
static void test_fatal(void *context, const qcx_program_diagnostic_v1_t *diagnostic)
{ (void)context; (void)diagnostic; abort(); }

int main(void)
{
	test_entity.v = &test_entity_state;
	test_spawned.v = &test_spawned_state;
	qcx_host_api_v1_t host = {
		.abi_version = QCX_PLUGIN_ABI_VERSION_V1,
		.struct_size = sizeof(host),
		.unpublish = test_unpublish,
		.fatal = test_fatal,
	};
	QCX_BindWorldServices(&host);
	assert(host.setorigin != NULL);
	assert(host.setmodel != NULL);
	assert(host.setsize != NULL);
	assert(host.spawn != NULL);
	assert(host.remove != NULL);
	assert(host.map_metadata != NULL);
	assert(host.map_admit != NULL);
	assert(host.map_time != NULL);
	assert(host.map_post_spawn != NULL);
	assert(host.precache_model != NULL);
	assert(host.precache_sound != NULL);
	assert(host.precache_model2 != NULL);
	assert(host.precache_sound2 != NULL);
	assert(host.lightstyle != NULL);
	assert(host.cvar != NULL);
	assert(host.cvar_set != NULL);
	assert(host.localcmd != NULL);
	assert(host.dprint != NULL);
	assert(host.changelevel != NULL);
	assert(host.makestatic != NULL);
	assert(host.sound == NULL);
	const float origin[3] = {10.0f, 20.0f, 30.0f};
	const float mins[3] = {-1.0f, -2.0f, -3.0f};
	const float maxs[3] = {4.0f, 5.0f, 6.0f};
	host.setorigin(host.context, 0U, origin);
	assert(test_entity.v->origin[0] == 10.0f && test_entity.v->origin[2] == 30.0f);
	assert(antilag_reset == 1 && linked == 1);
	host.setsize(host.context, 0U, mins, maxs);
	assert(test_entity.v->size[0] == 5.0f && test_entity.v->size[2] == 9.0f && linked == 2);
	host.setmodel(host.context, 0U, (const uint8_t *)"progs/test.mdl", 14U);
	assert(!strcmp(entity_model, "progs/test.mdl") && test_entity.v->modelindex == 3);
	assert(host.spawn(host.context) == 1U);
	host.remove(host.context, 1U);
	assert(freed == 1 && test_spawned.e.free);
	assert(host.precache_model(host.context, (const uint8_t *)"progs/test.mdl", 14U, NULL, 0U) == 14U);
	assert(host.precache_sound(host.context, (const uint8_t *)"sound/test.wav", 14U, NULL, 0U) == 14U);
	assert(precached_model == 1 && precached_sound == 1);
	host.lightstyle(host.context, 2.0f, (const uint8_t *)"abc", 3U);
	assert(host.cvar(host.context, (const uint8_t *)"skill", 5U) == 2.0f);
	host.cvar_set(host.context, (const uint8_t *)"skill", 5U, (const uint8_t *)"3", 1U);
	assert(cvar_set == 1);
	host.localcmd(host.context, (const uint8_t *)"status\n", 7U);
	assert(!strcmp(queued_command, "status\n"));
	host.makestatic(host.context, 0U);
	host.changelevel(host.context, (const uint8_t *)"dm6", 3U);
	assert(host.map_metadata(host.context, 0U, (const uint8_t *)"alpha", 5U,
		(const uint8_t *)"1.5", 3U) == QCX_MAP_METADATA_HANDLED);
	assert(test_entity.xv.alpha == 1.0f);
	assert(host.map_metadata(host.context, 0U, (const uint8_t *)"colormod", 8U,
		(const uint8_t *)"2 3 4", 5U) == QCX_MAP_METADATA_HANDLED);
	assert(test_entity.xv.colourmod[0] == 2.0f && test_entity.xv.colourmod[2] == 4.0f);
	assert(host.map_metadata(host.context, 0U, (const uint8_t *)"unknown", 7U,
		(const uint8_t *)"value", 5U) == QCX_MAP_METADATA_NOT_HANDLED);
	assert(host.map_admit(host.context, 0U, 0.0f) == QCX_MAP_ACCEPT);
	deathmatch.value = 1.0f;
	assert(host.map_admit(host.context, 0U, (float)SPAWNFLAG_NOT_DEATHMATCH)
		== QCX_MAP_REJECT);
	deathmatch.value = 0.0f;
	current_skill = 1;
	assert(host.map_admit(host.context, 0U, (float)SPAWNFLAG_NOT_MEDIUM)
		== QCX_MAP_REJECT);
	QCX_BindUnavailableServices(&host);
	assert(qcx_validate_host_api_v1(&host) == QCX_PLUGIN_OK);
	return 0;
}
