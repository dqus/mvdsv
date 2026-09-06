#include "qwsvdef.h"

#include "progs.h"
#include "qcx/entities.h"
#include "qcx/services.h"

#include <stdint.h>

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
	QCX_ObserveGameplayImport(context);
	if (origin == NULL) {
		SV_Error("qc2cpp setorigin requires origin");
	}
	edict_t *const entity = QCX_RequireEdict(slot);
	PF2_setorigin(entity, origin[0], origin[1], origin[2]);
}

static void QCX_SetModel(void *context, qcx_entity_id_t slot, const uint8_t *name,
	qcx_byte_count_t name_size)
{
	QCX_ObserveGameplayImport(context);
	char local[MAX_QPATH];
	edict_t *const entity = QCX_RequireEdict(slot);
	if (!QCX_CopyText(name, name_size, local, sizeof(local), "model")) {
		SV_Error("qc2cpp setmodel failed for %s", local);
	}
	PF2_setmodel(entity, local);
}

static void QCX_SetSize(void *context, qcx_entity_id_t slot, const float mins[3],
	const float maxs[3])
{
	QCX_ObserveGameplayImport(context);
	if (mins == NULL || maxs == NULL) {
		SV_Error("qc2cpp setsize requires bounds");
	}
	edict_t *const entity = QCX_RequireEdict(slot);
	PF2_setsize(entity,
		mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]);
}

static qcx_entity_id_t QCX_Spawn(void *context)
{
	QCX_ObserveGameplayImport(context);
	const qcx_entity_id_t slot = QCX_EdictToSlot(ED_Alloc());
	if (slot == QCX_INVALID_ENTITY_ID) {
		SV_Error("qc2cpp spawn returned invalid entity");
	}
	return slot;
}

static void QCX_Remove(void *context, qcx_entity_id_t slot)
{
	QCX_ObserveGameplayImport(context);
	ED_Free(QCX_RequireEdict(slot));
}

static uint32_t QCX_MapMetadata(void *context, qcx_entity_id_t slot,
	const uint8_t *key, qcx_byte_count_t key_size, const uint8_t *value,
	qcx_byte_count_t value_size)
{
	QCX_ObserveGameplayImport(context);
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
	QCX_ObserveGameplayImport(context);
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
	QCX_ObserveGameplayImport(context);
	return (float)sv.time;
}

static void QCX_MapPostSpawn(void *context, qcx_entity_id_t slot)
{
	QCX_ObserveGameplayImport(context);
	(void)QCX_RequireEdict(slot);
	SV_FlushSignon();
}

static qcx_byte_count_t QCX_Precache(void *context, const uint8_t *name,
	qcx_byte_count_t name_size, uint8_t *out, qcx_byte_count_t out_capacity,
	void (*operation)(char *), const char *what)
{
	QCX_ObserveGameplayImport(context);
	char *const persistent = QCX_CopyPersistentText(name, name_size, what);
	if (persistent == NULL) {
		SV_Error("qc2cpp %s precache failed", what);
	}
	operation(persistent);
	if (out != NULL && out_capacity >= name_size) {
		memcpy(out, name, name_size);
	}
	return name_size;
}

static qcx_byte_count_t QCX_PrecacheModel(void *context, const uint8_t *name,
	qcx_byte_count_t name_size, uint8_t *out, qcx_byte_count_t out_capacity)
{
	return QCX_Precache(context, name, name_size, out, out_capacity,
		PF2_precache_model, "model");
}

static qcx_byte_count_t QCX_PrecacheSound(void *context, const uint8_t *name,
	qcx_byte_count_t name_size, uint8_t *out, qcx_byte_count_t out_capacity)
{
	return QCX_Precache(context, name, name_size, out, out_capacity,
		PF2_precache_sound, "sound");
}

static void QCX_LightStyle(void *context, float style, const uint8_t *value,
	qcx_byte_count_t value_size)
{
	QCX_ObserveGameplayImport(context);
	if (style < 0.0f || style >= (float)MAX_LIGHTSTYLES) {
		SV_Error("qc2cpp lightstyle index out of range");
	}
	char *const persistent = QCX_CopyPersistentText(value, value_size, "lightstyle");
	if (persistent == NULL) {
		return;
	}
	PF2_lightstyle((int)style, persistent);
}

static float QCX_Cvar(void *context, const uint8_t *name, qcx_byte_count_t name_size)
{
	QCX_ObserveGameplayImport(context);
	char local[MAX_QPATH];
	if (!QCX_CopyText(name, name_size, local, sizeof(local), "cvar")) {
		return 0.0f;
	}
	return Cvar_Value(local);
}

static void QCX_CvarSet(void *context, const uint8_t *name, qcx_byte_count_t name_size,
	const uint8_t *value, qcx_byte_count_t value_size)
{
	QCX_ObserveGameplayImport(context);
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
	QCX_ObserveGameplayImport(context);
	char local[MAXCMDBUF];
	if (QCX_CopyText(text, text_size, local, sizeof(local), "localcmd")) {
		Cbuf_AddText(local);
	}
}

static void QCX_DPrint(void *context, const uint8_t *text, qcx_byte_count_t text_size)
{
	QCX_ObserveGameplayImport(context);
	char local[MAX_INFO_STRING];
	if (QCX_CopyText(text, text_size, local, sizeof(local), "dprint")) {
		Con_Printf("%s", local);
	}
}

static void QCX_MakeStatic(void *context, qcx_entity_id_t slot)
{
	QCX_ObserveGameplayImport(context);
	edict_t *const entity = QCX_RequireEdict(slot);
	PF2_makestatic(entity);
}

static void QCX_ChangeLevel(void *context, const uint8_t *map, qcx_byte_count_t map_size)
{
	QCX_ObserveGameplayImport(context);
	char local[MAX_QPATH];
	if (QCX_CopyText(map, map_size, local, sizeof(local), "map")) {
		static int last_spawncount;
		if (svs.spawncount == last_spawncount) {
			return;
		}
		last_spawncount = svs.spawncount;
		Cbuf_AddText(va("map %s\n", local));
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
