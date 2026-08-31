#include "qwsvdef.h"

#include "progs.h"
#include "qc2cpp/entities.h"
#include "qc2cpp/services.h"

#include <stdint.h>

static void QC_Unavailable(const char *name)
{
	SV_Error("qc2cpp host service %s is not implemented", name);
}

static edict_t *QC_RequireEdict(qc_entity_id_t slot)
{
	edict_t *const entity = QC_SlotToEdict(slot);
	if (entity == NULL || entity->v == NULL) {
		SV_Error("qc2cpp invalid entity slot %u", slot);
	}
	return entity;
}

static int QC_CopyText(const uint8_t *bytes, qc_byte_count_t size, char *out,
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

static char *QC_CopyPersistentText(const uint8_t *bytes, qc_byte_count_t size,
	const char *what)
{
	char local[MAX_QPATH];
	if (!QC_CopyText(bytes, size, local, sizeof(local), what)) {
		return NULL;
	}
	char *const result = Hunk_AllocName((int)size + 1, "qc2cpp");
	memcpy(result, local, (size_t)size + 1U);
	return result;
}

static void QC_SetOrigin(void *context, qc_entity_id_t slot, const float origin[3])
{
	(void)context;
	if (origin == NULL) {
		SV_Error("qc2cpp setorigin requires origin");
	}
	edict_t *const entity = QC_RequireEdict(slot);
	VectorCopy(origin, entity->v->origin);
	SV_AntilagReset(entity);
	SV_LinkEdict(entity, false);
}

static void QC_SetModel(void *context, qc_entity_id_t slot, const uint8_t *name,
	qc_byte_count_t name_size)
{
	(void)context;
	char local[MAX_QPATH];
	edict_t *const entity = QC_RequireEdict(slot);
	if (!QC_CopyText(name, name_size, local, sizeof(local), "model")
		|| !QC_SetEntityString(entity, "model", local)
		|| !SV_QC_SetModel(entity, local)) {
		SV_Error("qc2cpp setmodel failed for %s", local);
	}
}

static void QC_SetSize(void *context, qc_entity_id_t slot, const float mins[3],
	const float maxs[3])
{
	(void)context;
	if (mins == NULL || maxs == NULL) {
		SV_Error("qc2cpp setsize requires bounds");
	}
	edict_t *const entity = QC_RequireEdict(slot);
	VectorCopy(mins, entity->v->mins);
	VectorCopy(maxs, entity->v->maxs);
	VectorSubtract(maxs, mins, entity->v->size);
	SV_LinkEdict(entity, false);
}

static qc_entity_id_t QC_Spawn(void *context)
{
	(void)context;
	const qc_entity_id_t slot = QC_EdictToSlot(ED_Alloc());
	if (slot == QC_INVALID_ENTITY_ID) {
		SV_Error("qc2cpp spawn returned invalid entity");
	}
	return slot;
}

static void QC_Remove(void *context, qc_entity_id_t slot)
{
	(void)context;
	ED_Free(QC_RequireEdict(slot));
}

static uint32_t QC_MapMetadata(void *context, qc_entity_id_t slot,
	const uint8_t *key, qc_byte_count_t key_size, const uint8_t *value,
	qc_byte_count_t value_size)
{
	(void)context;
	(void)QC_RequireEdict(slot);
	if ((key == NULL && key_size != 0U) || (value == NULL && value_size != 0U)
		|| (key_size != 0U && memchr(key, '\0', key_size) != NULL)
		|| (value_size != 0U && memchr(value, '\0', value_size) != NULL)) {
		SV_Error("qc2cpp invalid map metadata");
	}
	char local_key[MAX_QPATH];
	char local_value[MAX_INFO_STRING];
	if (!QC_CopyText(key, key_size, local_key, sizeof(local_key), "map key")
		|| !QC_CopyText(value, value_size, local_value, sizeof(local_value), "map value")) {
		return QC_MAP_METADATA_ERROR;
	}
	edict_t *const entity = QC_RequireEdict(slot);
	if (!strcmp(local_key, "alpha")) {
		entity->xv.alpha = bound(0.0f, atof(local_value), 1.0f);
		return QC_MAP_METADATA_HANDLED;
	}
	if (!strcmp(local_key, "colormod")) {
		float colour[3];
		if (sscanf(local_value, "%f %f %f", &colour[0], &colour[1], &colour[2]) == 3
			&& colour[0] > 0.0f && colour[1] > 0.0f && colour[2] > 0.0f) {
			VectorCopy(colour, entity->xv.colourmod);
		}
		return QC_MAP_METADATA_HANDLED;
	}
	/* QC owns its schema; M handles only its reserved engine metadata. */
	return QC_MAP_METADATA_NOT_HANDLED;
}

static uint32_t QC_MapAdmit(void *context, qc_entity_id_t slot, float spawnflags)
{
	(void)context;
	(void)QC_RequireEdict(slot);
	const int flags = (int)spawnflags;
	if ((int)deathmatch.value != 0) {
		return (flags & SPAWNFLAG_NOT_DEATHMATCH) != 0 ? QC_MAP_REJECT : QC_MAP_ACCEPT;
	}
	if ((current_skill == 0 && (flags & SPAWNFLAG_NOT_EASY) != 0)
		|| (current_skill == 1 && (flags & SPAWNFLAG_NOT_MEDIUM) != 0)
		|| (current_skill >= 2 && (flags & SPAWNFLAG_NOT_HARD) != 0)) {
		return QC_MAP_REJECT;
	}
	return QC_MAP_ACCEPT;
}

static float QC_MapTime(void *context)
{
	(void)context;
	return (float)sv.time;
}

static void QC_MapPostSpawn(void *context, qc_entity_id_t slot)
{
	(void)context;
	(void)QC_RequireEdict(slot);
	SV_FlushSignon();
}

static qc_byte_count_t QC_Precache(void *context, const uint8_t *name,
	qc_byte_count_t name_size, uint8_t *out, qc_byte_count_t out_capacity,
	int (*operation)(const char *), const char *what)
{
	(void)context;
	char *const persistent = QC_CopyPersistentText(name, name_size, what);
	if (persistent == NULL || !operation(persistent)) {
		SV_Error("qc2cpp %s precache failed", what);
	}
	if (out != NULL && out_capacity >= name_size) {
		memcpy(out, name, name_size);
	}
	return name_size;
}

static qc_byte_count_t QC_PrecacheModel(void *context, const uint8_t *name,
	qc_byte_count_t name_size, uint8_t *out, qc_byte_count_t out_capacity)
{
	return QC_Precache(context, name, name_size, out, out_capacity,
		SV_QC_PrecacheModel, "model");
}

static qc_byte_count_t QC_PrecacheSound(void *context, const uint8_t *name,
	qc_byte_count_t name_size, uint8_t *out, qc_byte_count_t out_capacity)
{
	return QC_Precache(context, name, name_size, out, out_capacity,
		SV_QC_PrecacheSound, "sound");
}

static void QC_LightStyle(void *context, float style, const uint8_t *value,
	qc_byte_count_t value_size)
{
	(void)context;
	if (style < 0.0f || style >= (float)MAX_LIGHTSTYLES) {
		SV_Error("qc2cpp lightstyle index out of range");
	}
	char *const persistent = QC_CopyPersistentText(value, value_size, "lightstyle");
	if (persistent == NULL) {
		return;
	}
	SV_QC_LightStyle((int)style, persistent);
}

static float QC_Cvar(void *context, const uint8_t *name, qc_byte_count_t name_size)
{
	(void)context;
	char local[MAX_QPATH];
	if (!QC_CopyText(name, name_size, local, sizeof(local), "cvar")) {
		return 0.0f;
	}
	return Cvar_Value(local);
}

static void QC_CvarSet(void *context, const uint8_t *name, qc_byte_count_t name_size,
	const uint8_t *value, qc_byte_count_t value_size)
{
	(void)context;
	char local_name[MAX_QPATH];
	char local_value[MAX_INFO_STRING];
	if (!QC_CopyText(name, name_size, local_name, sizeof(local_name), "cvar")
		|| !QC_CopyText(value, value_size, local_value, sizeof(local_value), "cvar value")) {
		return;
	}
	cvar_t *const var = Cvar_Find(local_name);
	if (var != NULL) {
		Cvar_Set(var, local_value);
	}
}

static void QC_LocalCmd(void *context, const uint8_t *text, qc_byte_count_t text_size)
{
	(void)context;
	char local[MAXCMDBUF];
	if (QC_CopyText(text, text_size, local, sizeof(local), "localcmd")) {
		Cbuf_AddText(local);
	}
}

static void QC_DPrint(void *context, const uint8_t *text, qc_byte_count_t text_size)
{
	(void)context;
	char local[MAX_INFO_STRING];
	if (QC_CopyText(text, text_size, local, sizeof(local), "dprint")) {
		Con_Printf("%s", local);
	}
}

static void QC_MakeStatic(void *context, qc_entity_id_t slot)
{
	(void)context;
	edict_t *const entity = QC_RequireEdict(slot);
	char model[MAX_QPATH];
	uint32_t required = 0U;
	if (QC_CopyEntityString(entity, "model", model, sizeof(model), &required)
		!= QC_PLUGIN_OK) {
		SV_Error("qc2cpp makestatic requires model");
	}
	SV_QC_MakeStatic(entity, model);
}

static void QC_ChangeLevel(void *context, const uint8_t *map, qc_byte_count_t map_size)
{
	(void)context;
	char local[MAX_QPATH];
	if (QC_CopyText(map, map_size, local, sizeof(local), "map")) {
		SV_QC_ChangeLevel(local);
	}
}

void QC_BindWorldServices(qc_host_api_v1_t *host)
{
	if (host == NULL) return;
	host->setorigin = QC_SetOrigin;
	host->setmodel = QC_SetModel;
	host->setsize = QC_SetSize;
	host->spawn = QC_Spawn;
	host->remove = QC_Remove;
	host->map_metadata = QC_MapMetadata;
	host->map_admit = QC_MapAdmit;
	host->map_time = QC_MapTime;
	host->map_post_spawn = QC_MapPostSpawn;
	host->precache_sound = QC_PrecacheSound;
	host->precache_model = QC_PrecacheModel;
	host->dprint = QC_DPrint;
	host->lightstyle = QC_LightStyle;
	host->cvar = QC_Cvar;
	host->localcmd = QC_LocalCmd;
	host->makestatic = QC_MakeStatic;
	host->changelevel = QC_ChangeLevel;
	host->cvar_set = QC_CvarSet;
	host->precache_model2 = QC_PrecacheModel;
	host->precache_sound2 = QC_PrecacheSound;
}

#define QC_VOID_UNAVAILABLE(name, parameters, arguments) \
	static void QC_##name parameters { QC_Unavailable(#name); }
#define QC_FLOAT_UNAVAILABLE(name, parameters, arguments) \
	static float QC_##name parameters { QC_Unavailable(#name); return 0.0f; }
#define QC_ENTITY_UNAVAILABLE(name, parameters, arguments) \
	static qc_entity_id_t QC_##name parameters { QC_Unavailable(#name); return QC_INVALID_ENTITY_ID; }
#define QC_COUNT_UNAVAILABLE(name, parameters, arguments) \
	static qc_byte_count_t QC_##name parameters { QC_Unavailable(#name); return 0U; }
#define QC_FLAG_UNAVAILABLE(name, parameters, arguments) \
	static uint32_t QC_##name parameters { QC_Unavailable(#name); return 0U; }

QC_VOID_UNAVAILABLE(Sound, (void *c, qc_entity_id_t e, float ch, const uint8_t *s, qc_byte_count_t n, float v, float a), (c, e, ch, s, n, v, a))
QC_ENTITY_UNAVAILABLE(CheckClient, (void *c, qc_entity_id_t e), (c, e))
QC_VOID_UNAVAILABLE(TraceLine, (void *c, const float a[3], const float b[3], float n, qc_entity_id_t p, qc_trace_result_v1_t *r, qc_byte_count_t z), (c, a, b, n, p, r, z))
QC_VOID_UNAVAILABLE(StuffCmd, (void *c, qc_entity_id_t e, const uint8_t *s, qc_byte_count_t n), (c, e, s, n))
QC_VOID_UNAVAILABLE(BPrint, (void *c, float l, const uint8_t *s, qc_byte_count_t n), (c, l, s, n))
QC_VOID_UNAVAILABLE(SPrint, (void *c, qc_entity_id_t e, float l, const uint8_t *s, qc_byte_count_t n), (c, e, l, s, n))
QC_FLOAT_UNAVAILABLE(WalkMove, (void *c, qc_entity_id_t e, float y, float d), (c, e, y, d))
QC_FLOAT_UNAVAILABLE(DropToFloor, (void *c, qc_entity_id_t e), (c, e))
QC_FLOAT_UNAVAILABLE(CheckBottom, (void *c, qc_entity_id_t e), (c, e))
QC_FLOAT_UNAVAILABLE(PointContents, (void *c, const float p[3]), (c, p))
QC_VOID_UNAVAILABLE(Aim, (void *c, qc_entity_id_t e, float s, const float f[3], float o[3], qc_byte_count_t n), (c, e, s, f, o, n))
QC_VOID_UNAVAILABLE(WriteByte, (void *c, float d, float v, qc_entity_id_t e), (c, d, v, e))
QC_VOID_UNAVAILABLE(WriteChar, (void *c, float d, float v, qc_entity_id_t e), (c, d, v, e))
QC_VOID_UNAVAILABLE(WriteShort, (void *c, float d, float v, qc_entity_id_t e), (c, d, v, e))
QC_VOID_UNAVAILABLE(WriteLong, (void *c, float d, float v, qc_entity_id_t e), (c, d, v, e))
QC_VOID_UNAVAILABLE(WriteCoord, (void *c, float d, float v, qc_entity_id_t e), (c, d, v, e))
QC_VOID_UNAVAILABLE(WriteAngle, (void *c, float d, float v, qc_entity_id_t e), (c, d, v, e))
QC_VOID_UNAVAILABLE(WriteString, (void *c, float d, const uint8_t *s, qc_byte_count_t n, qc_entity_id_t e), (c, d, s, n, e))
QC_VOID_UNAVAILABLE(WriteEntity, (void *c, float d, qc_entity_id_t v, qc_entity_id_t e), (c, d, v, e))
QC_FLAG_UNAVAILABLE(StepDirection, (void *c, qc_entity_id_t e, float y, float d), (c, e, y, d))
QC_VOID_UNAVAILABLE(CenterPrint, (void *c, qc_entity_id_t e, const uint8_t *s, qc_byte_count_t n), (c, e, s, n))
QC_VOID_UNAVAILABLE(AmbientSound, (void *c, const float o[3], const uint8_t *s, qc_byte_count_t n, float v, float a), (c, o, s, n, v, a))
QC_VOID_UNAVAILABLE(SetSpawnParms, (void *c, qc_entity_id_t e, float p[16], qc_byte_count_t n), (c, e, p, n))
QC_VOID_UNAVAILABLE(LogFrag, (void *c, qc_entity_id_t k, qc_entity_id_t v), (c, k, v))
QC_COUNT_UNAVAILABLE(InfoKey, (void *c, qc_entity_id_t e, const uint8_t *k, qc_byte_count_t n, uint8_t *o, qc_byte_count_t z), (c, e, k, n, o, z))
QC_VOID_UNAVAILABLE(Multicast, (void *c, const float o[3], float d), (c, o, d))

#undef QC_VOID_UNAVAILABLE
#undef QC_FLOAT_UNAVAILABLE
#undef QC_ENTITY_UNAVAILABLE
#undef QC_COUNT_UNAVAILABLE
#undef QC_FLAG_UNAVAILABLE

void QC_BindUnavailableServices(qc_host_api_v1_t *host)
{
	if (host == NULL) return;
	if (host->sound == NULL) host->sound = QC_Sound;
	if (host->traceline == NULL) host->traceline = QC_TraceLine;
	if (host->checkclient == NULL) host->checkclient = QC_CheckClient;
	if (host->stuffcmd == NULL) host->stuffcmd = QC_StuffCmd;
	if (host->bprint == NULL) host->bprint = QC_BPrint;
	if (host->sprint == NULL) host->sprint = QC_SPrint;
	if (host->walkmove == NULL) host->walkmove = QC_WalkMove;
	if (host->droptofloor == NULL) host->droptofloor = QC_DropToFloor;
	if (host->checkbottom == NULL) host->checkbottom = QC_CheckBottom;
	if (host->pointcontents == NULL) host->pointcontents = QC_PointContents;
	if (host->aim == NULL) host->aim = QC_Aim;
	if (host->write_byte == NULL) host->write_byte = QC_WriteByte;
	if (host->write_char == NULL) host->write_char = QC_WriteChar;
	if (host->write_short == NULL) host->write_short = QC_WriteShort;
	if (host->write_long == NULL) host->write_long = QC_WriteLong;
	if (host->write_coord == NULL) host->write_coord = QC_WriteCoord;
	if (host->write_angle == NULL) host->write_angle = QC_WriteAngle;
	if (host->write_string == NULL) host->write_string = QC_WriteString;
	if (host->write_entity == NULL) host->write_entity = QC_WriteEntity;
	if (host->step_direction == NULL) host->step_direction = QC_StepDirection;
	if (host->centerprint == NULL) host->centerprint = QC_CenterPrint;
	if (host->ambientsound == NULL) host->ambientsound = QC_AmbientSound;
	if (host->setspawnparms == NULL) host->setspawnparms = QC_SetSpawnParms;
	if (host->logfrag == NULL) host->logfrag = QC_LogFrag;
	if (host->infokey == NULL) host->infokey = QC_InfoKey;
	if (host->multicast == NULL) host->multicast = QC_Multicast;
}
