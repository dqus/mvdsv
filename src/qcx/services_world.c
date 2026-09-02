#include "qwsvdef.h"

#include "progs.h"
#include "qcx/entities.h"
#include "qcx/services.h"

#include <stdint.h>

static void QCX_Unavailable(const char *name)
{
	SV_Error("qc2cpp host service %s is not implemented", name);
}

static edict_t *QCX_RequireEdict(qcx_entity_id_t slot)
{
	edict_t *const entity = QCX_SlotToEdict(slot);
	if (entity == NULL || entity->v == NULL) {
		SV_Error("qc2cpp invalid entity slot %u", slot);
	}
	return entity;
}

static int QCX_CopyText(const uint8_t *bytes, qcx_byte_count_t size, char *out,
	size_t capacity, const char *what)
{
	if ((bytes == NULL && size != 0U) || size >= capacity
		|| (size != 0U && memchr(bytes, '\0', size) != NULL)) {
		SV_Error("qc2cpp invalid %s string", what);
		return 0;
	}
	memcpy(out, bytes, size);
	out[size] = '\0';
	return 1;
}

static char *QCX_CopyPersistentText(const uint8_t *bytes, qcx_byte_count_t size,
	const char *what)
{
	char local[MAX_QPATH];
	if (!QCX_CopyText(bytes, size, local, sizeof(local), what)) {
		return NULL;
	}
	char *const result = Hunk_AllocName((int)size + 1, "qc2cpp");
	memcpy(result, local, (size_t)size + 1U);
	return result;
}

static void QCX_SetOrigin(void *context, qcx_entity_id_t slot, const float origin[3])
{
	(void)context;
	if (origin == NULL) {
		SV_Error("qc2cpp setorigin requires origin");
	}
	edict_t *const entity = QCX_RequireEdict(slot);
	VectorCopy(origin, entity->v->origin);
	SV_AntilagReset(entity);
	SV_LinkEdict(entity, false);
}

static void QCX_SetModel(void *context, qcx_entity_id_t slot, const uint8_t *name,
	qcx_byte_count_t name_size)
{
	(void)context;
	char local[MAX_QPATH];
	edict_t *const entity = QCX_RequireEdict(slot);
	if (!QCX_CopyText(name, name_size, local, sizeof(local), "model")
		|| !QCX_SetEntityString(entity, "model", local)
		|| !SV_QC_SetModel(entity, local)) {
		SV_Error("qc2cpp setmodel failed for %s", local);
	}
}

static void QCX_SetSize(void *context, qcx_entity_id_t slot, const float mins[3],
	const float maxs[3])
{
	(void)context;
	if (mins == NULL || maxs == NULL) {
		SV_Error("qc2cpp setsize requires bounds");
	}
	edict_t *const entity = QCX_RequireEdict(slot);
	VectorCopy(mins, entity->v->mins);
	VectorCopy(maxs, entity->v->maxs);
	VectorSubtract(maxs, mins, entity->v->size);
	SV_LinkEdict(entity, false);
}

static qcx_entity_id_t QCX_Spawn(void *context)
{
	(void)context;
	const qcx_entity_id_t slot = QCX_EdictToSlot(ED_Alloc());
	if (slot == QCX_INVALID_ENTITY_ID) {
		SV_Error("qc2cpp spawn returned invalid entity");
	}
	return slot;
}

static void QCX_Remove(void *context, qcx_entity_id_t slot)
{
	(void)context;
	ED_Free(QCX_RequireEdict(slot));
}

static uint32_t QCX_MapMetadata(void *context, qcx_entity_id_t slot,
	const uint8_t *key, qcx_byte_count_t key_size, const uint8_t *value,
	qcx_byte_count_t value_size)
{
	(void)context;
	(void)QCX_RequireEdict(slot);
	if ((key == NULL && key_size != 0U) || (value == NULL && value_size != 0U)
		|| (key_size != 0U && memchr(key, '\0', key_size) != NULL)
		|| (value_size != 0U && memchr(value, '\0', value_size) != NULL)) {
		SV_Error("qc2cpp invalid map metadata");
	}
	char local_key[MAX_QPATH];
	char local_value[MAX_INFO_STRING];
	if (!QCX_CopyText(key, key_size, local_key, sizeof(local_key), "map key")
		|| !QCX_CopyText(value, value_size, local_value, sizeof(local_value), "map value")) {
		return QCX_MAP_METADATA_ERROR;
	}
	edict_t *const entity = QCX_RequireEdict(slot);
	if (!strcmp(local_key, "alpha")) {
		entity->xv.alpha = bound(0.0f, atof(local_value), 1.0f);
		return QCX_MAP_METADATA_HANDLED;
	}
	if (!strcmp(local_key, "colormod")) {
		float colour[3];
		if (sscanf(local_value, "%f %f %f", &colour[0], &colour[1], &colour[2]) == 3
			&& colour[0] > 0.0f && colour[1] > 0.0f && colour[2] > 0.0f) {
			VectorCopy(colour, entity->xv.colourmod);
		}
		return QCX_MAP_METADATA_HANDLED;
	}
	/* QC owns its schema; M handles only its reserved engine metadata. */
	return QCX_MAP_METADATA_NOT_HANDLED;
}

static uint32_t QCX_MapAdmit(void *context, qcx_entity_id_t slot, float spawnflags)
{
	(void)context;
	(void)QCX_RequireEdict(slot);
	const int flags = (int)spawnflags;
	if ((int)deathmatch.value != 0) {
		return (flags & SPAWNFLAG_NOT_DEATHMATCH) != 0 ? QCX_MAP_REJECT : QCX_MAP_ACCEPT;
	}
	if ((current_skill == 0 && (flags & SPAWNFLAG_NOT_EASY) != 0)
		|| (current_skill == 1 && (flags & SPAWNFLAG_NOT_MEDIUM) != 0)
		|| (current_skill >= 2 && (flags & SPAWNFLAG_NOT_HARD) != 0)) {
		return QCX_MAP_REJECT;
	}
	return QCX_MAP_ACCEPT;
}

static float QCX_MapTime(void *context)
{
	(void)context;
	return (float)sv.time;
}

static void QCX_MapPostSpawn(void *context, qcx_entity_id_t slot)
{
	(void)context;
	(void)QCX_RequireEdict(slot);
	SV_FlushSignon();
}

static qcx_byte_count_t QCX_Precache(void *context, const uint8_t *name,
	qcx_byte_count_t name_size, uint8_t *out, qcx_byte_count_t out_capacity,
	int (*operation)(const char *), const char *what)
{
	(void)context;
	char *const persistent = QCX_CopyPersistentText(name, name_size, what);
	if (persistent == NULL || !operation(persistent)) {
		SV_Error("qc2cpp %s precache failed", what);
	}
	if (out != NULL && out_capacity >= name_size) {
		memcpy(out, name, name_size);
	}
	return name_size;
}

static qcx_byte_count_t QCX_PrecacheModel(void *context, const uint8_t *name,
	qcx_byte_count_t name_size, uint8_t *out, qcx_byte_count_t out_capacity)
{
	return QCX_Precache(context, name, name_size, out, out_capacity,
		SV_QC_PrecacheModel, "model");
}

static qcx_byte_count_t QCX_PrecacheSound(void *context, const uint8_t *name,
	qcx_byte_count_t name_size, uint8_t *out, qcx_byte_count_t out_capacity)
{
	return QCX_Precache(context, name, name_size, out, out_capacity,
		SV_QC_PrecacheSound, "sound");
}

static void QCX_LightStyle(void *context, float style, const uint8_t *value,
	qcx_byte_count_t value_size)
{
	(void)context;
	if (style < 0.0f || style >= (float)MAX_LIGHTSTYLES) {
		SV_Error("qc2cpp lightstyle index out of range");
	}
	char *const persistent = QCX_CopyPersistentText(value, value_size, "lightstyle");
	if (persistent == NULL) {
		return;
	}
	SV_QC_LightStyle((int)style, persistent);
}

static float QCX_Cvar(void *context, const uint8_t *name, qcx_byte_count_t name_size)
{
	(void)context;
	char local[MAX_QPATH];
	if (!QCX_CopyText(name, name_size, local, sizeof(local), "cvar")) {
		return 0.0f;
	}
	return Cvar_Value(local);
}

static void QCX_CvarSet(void *context, const uint8_t *name, qcx_byte_count_t name_size,
	const uint8_t *value, qcx_byte_count_t value_size)
{
	(void)context;
	char local_name[MAX_QPATH];
	char local_value[MAX_INFO_STRING];
	if (!QCX_CopyText(name, name_size, local_name, sizeof(local_name), "cvar")
		|| !QCX_CopyText(value, value_size, local_value, sizeof(local_value), "cvar value")) {
		return;
	}
	cvar_t *const var = Cvar_Find(local_name);
	if (var != NULL) {
		Cvar_Set(var, local_value);
	}
}

static void QCX_LocalCmd(void *context, const uint8_t *text, qcx_byte_count_t text_size)
{
	(void)context;
	char local[MAXCMDBUF];
	if (QCX_CopyText(text, text_size, local, sizeof(local), "localcmd")) {
		Cbuf_AddText(local);
	}
}

static void QCX_DPrint(void *context, const uint8_t *text, qcx_byte_count_t text_size)
{
	(void)context;
	char local[MAX_INFO_STRING];
	if (QCX_CopyText(text, text_size, local, sizeof(local), "dprint")) {
		Con_Printf("%s", local);
	}
}

static void QCX_MakeStatic(void *context, qcx_entity_id_t slot)
{
	(void)context;
	edict_t *const entity = QCX_RequireEdict(slot);
	char model[MAX_QPATH];
	uint32_t required = 0U;
	if (QCX_CopyEntityString(entity, "model", model, sizeof(model), &required)
		!= QCX_PLUGIN_OK) {
		SV_Error("qc2cpp makestatic requires model");
	}
	SV_QC_MakeStatic(entity, model);
}

static void QCX_ChangeLevel(void *context, const uint8_t *map, qcx_byte_count_t map_size)
{
	(void)context;
	char local[MAX_QPATH];
	if (QCX_CopyText(map, map_size, local, sizeof(local), "map")) {
		SV_QC_ChangeLevel(local);
	}
}

void QCX_BindWorldServices(qcx_host_api_v1_t *host)
{
	if (host == NULL) return;
	host->setorigin = QCX_SetOrigin;
	host->setmodel = QCX_SetModel;
	host->setsize = QCX_SetSize;
	host->spawn = QCX_Spawn;
	host->remove = QCX_Remove;
	host->map_metadata = QCX_MapMetadata;
	host->map_admit = QCX_MapAdmit;
	host->map_time = QCX_MapTime;
	host->map_post_spawn = QCX_MapPostSpawn;
	host->precache_sound = QCX_PrecacheSound;
	host->precache_model = QCX_PrecacheModel;
	host->dprint = QCX_DPrint;
	host->lightstyle = QCX_LightStyle;
	host->cvar = QCX_Cvar;
	host->localcmd = QCX_LocalCmd;
	host->makestatic = QCX_MakeStatic;
	host->changelevel = QCX_ChangeLevel;
	host->cvar_set = QCX_CvarSet;
	host->precache_model2 = QCX_PrecacheModel;
	host->precache_sound2 = QCX_PrecacheSound;
}

#define QCX_VOID_UNAVAILABLE(name, parameters, arguments) \
	static void QCX_##name parameters { QCX_Unavailable(#name); }
#define QCX_FLOAT_UNAVAILABLE(name, parameters, arguments) \
	static float QCX_##name parameters { QCX_Unavailable(#name); return 0.0f; }
#define QCX_ENTITY_UNAVAILABLE(name, parameters, arguments) \
	static qcx_entity_id_t QCX_##name parameters { QCX_Unavailable(#name); return QCX_INVALID_ENTITY_ID; }
#define QCX_COUNT_UNAVAILABLE(name, parameters, arguments) \
	static qcx_byte_count_t QCX_##name parameters { QCX_Unavailable(#name); return 0U; }
#define QCX_FLAG_UNAVAILABLE(name, parameters, arguments) \
	static uint32_t QCX_##name parameters { QCX_Unavailable(#name); return 0U; }

QCX_VOID_UNAVAILABLE(Sound, (void *c, qcx_entity_id_t e, float ch, const uint8_t *s, qcx_byte_count_t n, float v, float a), (c, e, ch, s, n, v, a))
QCX_ENTITY_UNAVAILABLE(CheckClient, (void *c, qcx_entity_id_t e), (c, e))
QCX_VOID_UNAVAILABLE(TraceLine, (void *c, const float a[3], const float b[3], float n, qcx_entity_id_t p, qcx_trace_result_v1_t *r, qcx_byte_count_t z), (c, a, b, n, p, r, z))
QCX_VOID_UNAVAILABLE(StuffCmd, (void *c, qcx_entity_id_t e, const uint8_t *s, qcx_byte_count_t n), (c, e, s, n))
QCX_VOID_UNAVAILABLE(BPrint, (void *c, float l, const uint8_t *s, qcx_byte_count_t n), (c, l, s, n))
QCX_VOID_UNAVAILABLE(SPrint, (void *c, qcx_entity_id_t e, float l, const uint8_t *s, qcx_byte_count_t n), (c, e, l, s, n))
QCX_FLOAT_UNAVAILABLE(WalkMove, (void *c, qcx_entity_id_t e, float y, float d), (c, e, y, d))
QCX_FLOAT_UNAVAILABLE(DropToFloor, (void *c, qcx_entity_id_t e), (c, e))
QCX_FLOAT_UNAVAILABLE(CheckBottom, (void *c, qcx_entity_id_t e), (c, e))
QCX_FLOAT_UNAVAILABLE(PointContents, (void *c, const float p[3]), (c, p))
QCX_VOID_UNAVAILABLE(Aim, (void *c, qcx_entity_id_t e, float s, const float f[3], float o[3], qcx_byte_count_t n), (c, e, s, f, o, n))
QCX_VOID_UNAVAILABLE(WriteByte, (void *c, float d, float v, qcx_entity_id_t e), (c, d, v, e))
QCX_VOID_UNAVAILABLE(WriteChar, (void *c, float d, float v, qcx_entity_id_t e), (c, d, v, e))
QCX_VOID_UNAVAILABLE(WriteShort, (void *c, float d, float v, qcx_entity_id_t e), (c, d, v, e))
QCX_VOID_UNAVAILABLE(WriteLong, (void *c, float d, float v, qcx_entity_id_t e), (c, d, v, e))
QCX_VOID_UNAVAILABLE(WriteCoord, (void *c, float d, float v, qcx_entity_id_t e), (c, d, v, e))
QCX_VOID_UNAVAILABLE(WriteAngle, (void *c, float d, float v, qcx_entity_id_t e), (c, d, v, e))
QCX_VOID_UNAVAILABLE(WriteString, (void *c, float d, const uint8_t *s, qcx_byte_count_t n, qcx_entity_id_t e), (c, d, s, n, e))
QCX_VOID_UNAVAILABLE(WriteEntity, (void *c, float d, qcx_entity_id_t v, qcx_entity_id_t e), (c, d, v, e))
QCX_FLAG_UNAVAILABLE(StepDirection, (void *c, qcx_entity_id_t e, float y, float d), (c, e, y, d))
QCX_VOID_UNAVAILABLE(CenterPrint, (void *c, qcx_entity_id_t e, const uint8_t *s, qcx_byte_count_t n), (c, e, s, n))
QCX_VOID_UNAVAILABLE(AmbientSound, (void *c, const float o[3], const uint8_t *s, qcx_byte_count_t n, float v, float a), (c, o, s, n, v, a))
QCX_VOID_UNAVAILABLE(SetSpawnParms, (void *c, qcx_entity_id_t e, float p[16], qcx_byte_count_t n), (c, e, p, n))
QCX_VOID_UNAVAILABLE(LogFrag, (void *c, qcx_entity_id_t k, qcx_entity_id_t v), (c, k, v))
QCX_COUNT_UNAVAILABLE(InfoKey, (void *c, qcx_entity_id_t e, const uint8_t *k, qcx_byte_count_t n, uint8_t *o, qcx_byte_count_t z), (c, e, k, n, o, z))
QCX_VOID_UNAVAILABLE(Multicast, (void *c, const float o[3], float d), (c, o, d))

#undef QCX_VOID_UNAVAILABLE
#undef QCX_FLOAT_UNAVAILABLE
#undef QCX_ENTITY_UNAVAILABLE
#undef QCX_COUNT_UNAVAILABLE
#undef QCX_FLAG_UNAVAILABLE

void QCX_BindUnavailableServices(qcx_host_api_v1_t *host)
{
	if (host == NULL) return;
	if (host->sound == NULL) host->sound = QCX_Sound;
	if (host->traceline == NULL) host->traceline = QCX_TraceLine;
	if (host->checkclient == NULL) host->checkclient = QCX_CheckClient;
	if (host->stuffcmd == NULL) host->stuffcmd = QCX_StuffCmd;
	if (host->bprint == NULL) host->bprint = QCX_BPrint;
	if (host->sprint == NULL) host->sprint = QCX_SPrint;
	if (host->walkmove == NULL) host->walkmove = QCX_WalkMove;
	if (host->droptofloor == NULL) host->droptofloor = QCX_DropToFloor;
	if (host->checkbottom == NULL) host->checkbottom = QCX_CheckBottom;
	if (host->pointcontents == NULL) host->pointcontents = QCX_PointContents;
	if (host->aim == NULL) host->aim = QCX_Aim;
	if (host->write_byte == NULL) host->write_byte = QCX_WriteByte;
	if (host->write_char == NULL) host->write_char = QCX_WriteChar;
	if (host->write_short == NULL) host->write_short = QCX_WriteShort;
	if (host->write_long == NULL) host->write_long = QCX_WriteLong;
	if (host->write_coord == NULL) host->write_coord = QCX_WriteCoord;
	if (host->write_angle == NULL) host->write_angle = QCX_WriteAngle;
	if (host->write_string == NULL) host->write_string = QCX_WriteString;
	if (host->write_entity == NULL) host->write_entity = QCX_WriteEntity;
	if (host->step_direction == NULL) host->step_direction = QCX_StepDirection;
	if (host->centerprint == NULL) host->centerprint = QCX_CenterPrint;
	if (host->ambientsound == NULL) host->ambientsound = QCX_AmbientSound;
	if (host->setspawnparms == NULL) host->setspawnparms = QCX_SetSpawnParms;
	if (host->logfrag == NULL) host->logfrag = QCX_LogFrag;
	if (host->infokey == NULL) host->infokey = QCX_InfoKey;
	if (host->multicast == NULL) host->multicast = QCX_Multicast;
}
