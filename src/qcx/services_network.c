#include "qwsvdef.h"

#include "qcx/entities.h"
#include "qcx/services.h"

#include <stdint.h>

enum {
	QCX_MSG_BROADCAST = 0,
	QCX_MSG_ONE = 1,
	QCX_MSG_ALL = 2,
	QCX_MSG_INIT = 3,
	QCX_MSG_MULTICAST = 4,
};

enum {
	QCX_SPECPRINT_CENTERPRINT = 0x1,
	QCX_SPECPRINT_SPRINT = 0x2,
	QCX_SPECPRINT_STUFFCMD = 0x4,
};

static edict_t *QCX_RequireNetworkEdict(qcx_entity_id_t slot)
{
	edict_t *const entity = QCX_SlotToEdict(slot);
	if (entity == NULL || entity->v == NULL) {
		SV_Error("qc2cpp invalid network entity slot %u", slot);
	}
	return entity;
}

static client_t *QCX_RequireClient(qcx_entity_id_t slot)
{
	const int entnum = NUM_FOR_EDICT(QCX_RequireNetworkEdict(slot));
	if (entnum < 1 || entnum > MAX_CLIENTS) {
		SV_Error("qc2cpp network entity %d is not a client", entnum);
	}
	return &svs.clients[entnum - 1];
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

static int QCX_MessageDestination(float destination)
{
	const int value = (int)destination;
	if ((float)value != destination) {
		SV_Error("qc2cpp message destination must be integral");
	}
	return value;
}

static edict_t *QCX_MessageEntity(int destination, qcx_entity_id_t msg_entity)
{
	return destination == QCX_MSG_ONE ? QCX_RequireNetworkEdict(msg_entity) : NULL;
}

static void QCX_Sound(void *context, qcx_entity_id_t entity, float channel,
	const uint8_t *sample, qcx_byte_count_t sample_size, float volume,
	float attenuation)
{
	QCX_ObserveGameplayImport(context);
	char local[MAX_QPATH];
	if (QCX_CopyText(sample, sample_size, local, sizeof(local), "sound")) {
		SV_StartSound(QCX_RequireNetworkEdict(entity), (int)channel, local,
			(int)(volume * 255.0f), attenuation);
	}
}

static void QCX_StuffCmd(void *context, qcx_entity_id_t entity, const uint8_t *text,
	qcx_byte_count_t text_size)
{
	QCX_ObserveGameplayImport(context);
	char local[MAX_STUFFTEXT];
	if (!QCX_CopyText(text, text_size, local, sizeof(local), "stuffcmd")) {
		return;
	}
	(void)QCX_RequireClient(entity);
	PF2_stuffcmd(NUM_FOR_EDICT(QCX_RequireNetworkEdict(entity)), local, 0);
}

static void QCX_BPrint(void *context, float level, const uint8_t *text,
	qcx_byte_count_t text_size)
{
	QCX_ObserveGameplayImport(context);
	char local[MAX_INFO_STRING];
	if (QCX_CopyText(text, text_size, local, sizeof(local), "bprint")) {
		SV_BroadcastPrintf((int)level, "%s", local);
	}
}

static void QCX_SPrint(void *context, qcx_entity_id_t entity, float level,
	const uint8_t *text, qcx_byte_count_t text_size)
{
	QCX_ObserveGameplayImport(context);
	char local[MAX_INFO_STRING];
	if (QCX_CopyText(text, text_size, local, sizeof(local), "sprint")) {
		client_t *const client = QCX_RequireClient(entity);
		SV_ClientPrintf(client, (int)level, "%s", local);
		if ((int)sv_specprint.value & QCX_SPECPRINT_SPRINT) {
			const int entnum = NUM_FOR_EDICT(QCX_RequireNetworkEdict(entity));
			for (int index = 0; index < MAX_CLIENTS; ++index) {
				client_t *const spectator = &svs.clients[index];
				if (client->state && spectator->spectator
					&& spectator->spec_track == entnum
					&& (client->spec_print & QCX_SPECPRINT_SPRINT)) {
					SV_ClientPrintf(spectator, (int)level, "%s", local);
				}
			}
		}
	}
}

static void QCX_WriteByte(void *context, float destination, float value,
	qcx_entity_id_t msg_entity)
{
	QCX_ObserveGameplayImport(context);
	const int to = QCX_MessageDestination(destination);
	PF2_WriteByte(to, (int)value, QCX_MessageEntity(to, msg_entity));
}

static void QCX_WriteChar(void *context, float destination, float value,
	qcx_entity_id_t msg_entity)
{
	QCX_ObserveGameplayImport(context);
	const int to = QCX_MessageDestination(destination);
	PF2_WriteChar(to, (int)value, QCX_MessageEntity(to, msg_entity));
}

static void QCX_WriteShort(void *context, float destination, float value,
	qcx_entity_id_t msg_entity)
{
	QCX_ObserveGameplayImport(context);
	const int to = QCX_MessageDestination(destination);
	PF2_WriteShort(to, (int)value, QCX_MessageEntity(to, msg_entity));
}

static void QCX_WriteLong(void *context, float destination, float value,
	qcx_entity_id_t msg_entity)
{
	QCX_ObserveGameplayImport(context);
	const int to = QCX_MessageDestination(destination);
	PF2_WriteLong(to, (int)value, QCX_MessageEntity(to, msg_entity));
}

static void QCX_WriteCoord(void *context, float destination, float value,
	qcx_entity_id_t msg_entity)
{
	QCX_ObserveGameplayImport(context);
	const int to = QCX_MessageDestination(destination);
	PF2_WriteCoord(to, value, QCX_MessageEntity(to, msg_entity));
}

static void QCX_WriteAngle(void *context, float destination, float value,
	qcx_entity_id_t msg_entity)
{
	QCX_ObserveGameplayImport(context);
	const int to = QCX_MessageDestination(destination);
	PF2_WriteAngle(to, value, QCX_MessageEntity(to, msg_entity));
}

static void QCX_WriteString(void *context, float destination, const uint8_t *value,
	qcx_byte_count_t value_size, qcx_entity_id_t msg_entity)
{
	QCX_ObserveGameplayImport(context);
	char local[MAX_INFO_STRING];
	if (!QCX_CopyText(value, value_size, local, sizeof(local), "message")) {
		return;
	}
	const int to = QCX_MessageDestination(destination);
	PF2_WriteString(to, local, QCX_MessageEntity(to, msg_entity));
}

static void QCX_WriteEntity(void *context, float destination, qcx_entity_id_t value,
	qcx_entity_id_t msg_entity)
{
	QCX_ObserveGameplayImport(context);
	const int to = QCX_MessageDestination(destination);
	PF2_WriteEntity(to, NUM_FOR_EDICT(QCX_RequireNetworkEdict(value)),
		QCX_MessageEntity(to, msg_entity));
}

static void QCX_CenterPrint(void *context, qcx_entity_id_t entity,
	const uint8_t *text, qcx_byte_count_t text_size)
{
	QCX_ObserveGameplayImport(context);
	char local[MAX_INFO_STRING];
	if (!QCX_CopyText(text, text_size, local, sizeof(local), "centerprint")) {
		return;
	}
	(void)QCX_RequireClient(entity);
	PF2_centerprint(NUM_FOR_EDICT(QCX_RequireNetworkEdict(entity)), local);
}

static void QCX_AmbientSound(void *context, const float origin[3],
	const uint8_t *sample, qcx_byte_count_t sample_size, float volume,
	float attenuation)
{
	QCX_ObserveGameplayImport(context);
	char local[MAX_QPATH];
	if (origin == NULL || !QCX_CopyText(sample, sample_size, local, sizeof(local),
		"ambient sound")) {
		return;
	}
	int soundnum;
	for (soundnum = 0; sv.sound_precache[soundnum] != NULL; ++soundnum) {
		if (!strcmp(sv.sound_precache[soundnum], local)) {
			MSG_WriteByte(&sv.signon, svc_spawnstaticsound);
			MSG_WriteCoord(&sv.signon, origin[0]);
			MSG_WriteCoord(&sv.signon, origin[1]);
			MSG_WriteCoord(&sv.signon, origin[2]);
			MSG_WriteByte(&sv.signon, soundnum);
			MSG_WriteByte(&sv.signon, volume * 255.0f);
			MSG_WriteByte(&sv.signon, attenuation * 64.0f);
			return;
		}
	}
	SV_Error("qc2cpp ambient sound is not precached: %s", local);
}

static void QCX_SetSpawnParms(void *context, qcx_entity_id_t entity,
	float out_parms[16], qcx_byte_count_t out_parms_size)
{
	QCX_ObserveGameplayImport(context);
	if (out_parms == NULL || out_parms_size < sizeof(float) * 16U) {
		SV_Error("qc2cpp setspawnparms requires sixteen floats");
	}
	PF2_setspawnparms(NUM_FOR_EDICT(QCX_RequireNetworkEdict(entity)), out_parms);
}

static void QCX_LogFrag(void *context, qcx_entity_id_t killer, qcx_entity_id_t victim)
{
	QCX_ObserveGameplayImport(context);
	PF2_logfrag(NUM_FOR_EDICT(QCX_RequireNetworkEdict(killer)),
		NUM_FOR_EDICT(QCX_RequireNetworkEdict(victim)));
}

static qcx_byte_count_t QCX_InfoKey(void *context, qcx_entity_id_t entity,
	const uint8_t *key, qcx_byte_count_t key_size, uint8_t *out,
	qcx_byte_count_t out_capacity)
{
	QCX_ObserveGameplayImport(context);
	char local[MAX_KEY_STRING];
	if (!QCX_CopyText(key, key_size, local, sizeof(local), "infokey")) {
		return 0U;
	}
	const char *const value = PF2_infokey(
		NUM_FOR_EDICT(QCX_RequireNetworkEdict(entity)), local);
	const qcx_byte_count_t required = (qcx_byte_count_t)strlen(value);
	if (out != NULL && out_capacity != 0U) {
		const qcx_byte_count_t copied = required < out_capacity ? required : out_capacity;
		memcpy(out, value, copied);
	}
	return required;
}

static void QCX_Multicast(void *context, const float origin[3], float destination)
{
	QCX_ObserveGameplayImport(context);
	if (origin == NULL) {
		SV_Error("qc2cpp multicast requires an origin");
	}
	PF2_multicast(origin[0], origin[1], origin[2], (int)destination);
}

void QCX_BindNetworkServices(qcx_host_api_v1_t *host)
{
	if (host == NULL) return;
	host->sound = QCX_Sound;
	host->stuffcmd = QCX_StuffCmd;
	host->bprint = QCX_BPrint;
	host->sprint = QCX_SPrint;
	host->write_byte = QCX_WriteByte;
	host->write_char = QCX_WriteChar;
	host->write_short = QCX_WriteShort;
	host->write_long = QCX_WriteLong;
	host->write_coord = QCX_WriteCoord;
	host->write_angle = QCX_WriteAngle;
	host->write_string = QCX_WriteString;
	host->write_entity = QCX_WriteEntity;
	host->centerprint = QCX_CenterPrint;
	host->ambientsound = QCX_AmbientSound;
	host->setspawnparms = QCX_SetSpawnParms;
	host->logfrag = QCX_LogFrag;
	host->infokey = QCX_InfoKey;
	host->multicast = QCX_Multicast;
}
