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

static sizebuf_t *QCX_WriteDestination(float destination)
{
	const int value = (int)destination;
	if ((float)value != destination) {
		SV_Error("qc2cpp message destination must be integral");
	}
	switch (value) {
	case QCX_MSG_BROADCAST:
		return &sv.datagram;
	case QCX_MSG_ALL:
		return &sv.reliable_datagram;
	case QCX_MSG_INIT:
		if (sv.state != ss_loading) {
			SV_Error("qc2cpp MSG_INIT is only valid while loading");
		}
		return &sv.signon;
	case QCX_MSG_MULTICAST:
		return &sv.multicast;
	default:
		SV_Error("qc2cpp invalid message destination %d", value);
		return NULL;
	}
}

static int QCX_IsMessageOne(float destination)
{
	const int value = (int)destination;
	if ((float)value != destination) {
		SV_Error("qc2cpp message destination must be integral");
	}
	return value == QCX_MSG_ONE;
}

static int QCX_CoordMessageSize(void)
{
#ifdef FTE_PEXT_FLOATCOORDS
	return msg_coordsize;
#else
	return 2;
#endif
}

static int QCX_AngleMessageSize(void)
{
#ifdef FTE_PEXT_FLOATCOORDS
	return msg_anglesize;
#else
	return 1;
#endif
}

static void QCX_Sound(void *context, qcx_entity_id_t entity, float channel,
	const uint8_t *sample, qcx_byte_count_t sample_size, float volume,
	float attenuation)
{
	(void)context;
	char local[MAX_QPATH];
	if (QCX_CopyText(sample, sample_size, local, sizeof(local), "sound")) {
		SV_StartSound(QCX_RequireNetworkEdict(entity), (int)channel, local,
			(int)(volume * 255.0f), attenuation);
	}
}

static void QCX_StuffCmd(void *context, qcx_entity_id_t entity, const uint8_t *text,
	qcx_byte_count_t text_size)
{
	(void)context;
	char local[MAX_STUFFTEXT];
	client_t *const client = QCX_RequireClient(entity);
	if (!QCX_CopyText(text, text_size, local, sizeof(local), "stuffcmd")) {
		return;
	}
	if (!strncmp(local, "disconnect\n", MAX_STUFFTEXT)) {
		client->drop = true;
		return;
	}
	if (strlen(client->stufftext_buf) + strlen(local) >= MAX_STUFFTEXT) {
		SV_Error("qc2cpp stufftext buffer overflow");
	}
	strlcat(client->stufftext_buf, local, MAX_STUFFTEXT);
	if (strchr(client->stufftext_buf, '\n') != NULL) {
		ClientReliableWrite_Begin(client, svc_stufftext,
			2 + (int)strlen(client->stufftext_buf));
		ClientReliableWrite_String(client, client->stufftext_buf);
		if (sv.mvdrecording && MVDWrite_Begin(dem_single, client - svs.clients,
			2 + (int)strlen(client->stufftext_buf))) {
			MVD_MSG_WriteByte(svc_stufftext);
			MVD_MSG_WriteString(client->stufftext_buf);
		}
		if ((int)sv_specprint.value & QCX_SPECPRINT_STUFFCMD) {
			const int entnum = NUM_FOR_EDICT(QCX_RequireNetworkEdict(entity));
			for (int index = 0; index < MAX_CLIENTS; ++index) {
				client_t *const spectator = &svs.clients[index];
				if (client->state && spectator->spectator
					&& spectator->spec_track == entnum
					&& (client->spec_print & QCX_SPECPRINT_STUFFCMD)) {
					ClientReliableWrite_Begin(spectator, svc_stufftext,
						2 + (int)strlen(client->stufftext_buf));
					ClientReliableWrite_String(spectator, client->stufftext_buf);
				}
			}
		}
		client->stufftext_buf[0] = '\0';
	}
}

static void QCX_BPrint(void *context, float level, const uint8_t *text,
	qcx_byte_count_t text_size)
{
	(void)context;
	char local[MAX_INFO_STRING];
	if (QCX_CopyText(text, text_size, local, sizeof(local), "bprint")) {
		SV_BroadcastPrintf((int)level, "%s", local);
	}
}

static void QCX_SPrint(void *context, qcx_entity_id_t entity, float level,
	const uint8_t *text, qcx_byte_count_t text_size)
{
	(void)context;
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

#define QCX_DEFINE_WRITE(name, writer, reliable_writer, mvd_writer, bytes) \
static void QCX_Write##name(void *context, float destination, float value, \
	qcx_entity_id_t msg_entity) \
{ \
	(void)context; \
	if (QCX_IsMessageOne(destination)) { \
		client_t *const client = QCX_RequireClient(msg_entity); \
		ClientReliableCheckBlock(client, bytes); \
		reliable_writer(client, value); \
		if (sv.mvdrecording && MVDWrite_Begin(dem_single, client - svs.clients, bytes)) { \
			mvd_writer(value); \
		} \
	} else { \
		writer(QCX_WriteDestination(destination), value); \
	} \
}

QCX_DEFINE_WRITE(Byte, MSG_WriteByte, ClientReliableWrite_Byte, MVD_MSG_WriteByte, 1)
QCX_DEFINE_WRITE(Char, MSG_WriteChar, ClientReliableWrite_Char, MVD_MSG_WriteByte, 1)
QCX_DEFINE_WRITE(Short, MSG_WriteShort, ClientReliableWrite_Short, MVD_MSG_WriteShort, 2)
QCX_DEFINE_WRITE(Long, MSG_WriteLong, ClientReliableWrite_Long, MVD_MSG_WriteLong, 4)
QCX_DEFINE_WRITE(Coord, MSG_WriteCoord, ClientReliableWrite_Coord, MVD_MSG_WriteCoord, QCX_CoordMessageSize())
QCX_DEFINE_WRITE(Angle, MSG_WriteAngle, ClientReliableWrite_Angle, MVD_MSG_WriteAngle, QCX_AngleMessageSize())

static void QCX_WriteString(void *context, float destination, const uint8_t *value,
	qcx_byte_count_t value_size, qcx_entity_id_t msg_entity)
{
	(void)context;
	char local[MAX_INFO_STRING];
	if (!QCX_CopyText(value, value_size, local, sizeof(local), "message")) {
		return;
	}
	if (QCX_IsMessageOne(destination)) {
		client_t *const client = QCX_RequireClient(msg_entity);
		ClientReliableCheckBlock(client, 1 + (int)value_size);
		ClientReliableWrite_String(client, local);
		if (sv.mvdrecording && MVDWrite_Begin(dem_single, client - svs.clients,
			1 + (int)value_size)) {
			MVD_MSG_WriteString(local);
		}
	} else {
		MSG_WriteString(QCX_WriteDestination(destination), local);
	}
}

static void QCX_WriteEntity(void *context, float destination, qcx_entity_id_t value,
	qcx_entity_id_t msg_entity)
{
	(void)context;
	const int entnum = NUM_FOR_EDICT(QCX_RequireNetworkEdict(value));
	if (QCX_IsMessageOne(destination)) {
		client_t *const client = QCX_RequireClient(msg_entity);
		ClientReliableCheckBlock(client, 2);
		ClientReliableWrite_Short(client, entnum);
		if (sv.mvdrecording && MVDWrite_Begin(dem_single, client - svs.clients, 2)) {
			MVD_MSG_WriteShort(entnum);
		}
	} else {
		MSG_WriteShort(QCX_WriteDestination(destination), entnum);
	}
}

static void QCX_CenterPrint(void *context, qcx_entity_id_t entity,
	const uint8_t *text, qcx_byte_count_t text_size)
{
	(void)context;
	char local[MAX_INFO_STRING];
	if (!QCX_CopyText(text, text_size, local, sizeof(local), "centerprint")) {
		return;
	}
	client_t *const client = QCX_RequireClient(entity);
	ClientReliableWrite_Begin(client, svc_centerprint, 2 + (int)text_size);
	ClientReliableWrite_String(client, local);
	if (sv.mvdrecording && MVDWrite_Begin(dem_single, client - svs.clients,
		2 + (int)text_size)) {
		MVD_MSG_WriteByte(svc_centerprint);
		MVD_MSG_WriteString(local);
	}
	if ((int)sv_specprint.value & QCX_SPECPRINT_CENTERPRINT) {
		const int entnum = NUM_FOR_EDICT(QCX_RequireNetworkEdict(entity));
		for (int index = 0; index < MAX_CLIENTS; ++index) {
			client_t *const spectator = &svs.clients[index];
			if (client->state && spectator->spectator
				&& spectator->spec_track == entnum
				&& (client->spec_print & QCX_SPECPRINT_CENTERPRINT)) {
				ClientReliableWrite_Begin(spectator, svc_centerprint,
					2 + (int)text_size);
				ClientReliableWrite_String(spectator, local);
			}
		}
	}
}

static void QCX_AmbientSound(void *context, const float origin[3],
	const uint8_t *sample, qcx_byte_count_t sample_size, float volume,
	float attenuation)
{
	(void)context;
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
	(void)context;
	if (out_parms == NULL || out_parms_size < sizeof(float) * 16U) {
		SV_Error("qc2cpp setspawnparms requires sixteen floats");
	}
	memcpy(out_parms, QCX_RequireClient(entity)->spawn_parms, sizeof(float) * 16U);
}

static void QCX_LogFrag(void *context, qcx_entity_id_t killer, qcx_entity_id_t victim)
{
	(void)context;
	SV_QC_LogFrag(QCX_RequireNetworkEdict(killer), QCX_RequireNetworkEdict(victim));
}

static qcx_byte_count_t QCX_InfoKey(void *context, qcx_entity_id_t entity,
	const uint8_t *key, qcx_byte_count_t key_size, uint8_t *out,
	qcx_byte_count_t out_capacity)
{
	(void)context;
	char local[MAX_KEY_STRING];
	if (!QCX_CopyText(key, key_size, local, sizeof(local), "infokey")) {
		return 0U;
	}
	const int entnum = NUM_FOR_EDICT(QCX_RequireNetworkEdict(entity));
	const char *value = "";
	char local_value[256];
	if (entnum == 0) {
		const char *lookup = local;
		if (lookup[0] == '\\') {
			++lookup;
			value = (!strcmp(lookup, "date_str") || !strcmp(lookup, "ip")
				|| !strncmp(lookup, "realip", 7) || !strncmp(lookup, "download", 9)
				|| !strcmp(lookup, "ping") || !strcmp(lookup, "*userid")
				|| !strncmp(lookup, "login", 6) || !strcmp(lookup, "*VIP")
				|| !strcmp(lookup, "*state") || !strcmp(lookup, "netname")
				|| !strcmp(lookup, "mapname") || !strcmp(lookup, "modelname")
				|| !strcmp(lookup, "version") || !strcmp(lookup, "servername"))
				? "yes" : "no";
		} else if (!strcmp(lookup, "date_str")) {
			date_t date;
			SV_TimeOfDay(&date, "%a %b %d, %H:%M:%S %Y");
			value = date.str;
		} else if (!strcmp(lookup, "mapname")) value = sv.mapname;
		else if (!strcmp(lookup, "modelname")) value = sv.modelname;
		else if (!strcmp(lookup, "version")) value = VersionStringFull();
		else if (!strcmp(lookup, "servername")) value = SERVER_NAME;
		else value = Info_ValueForKey(svs.info, lookup);
		if (value == NULL || *value == '\0') value = Info_Get(&_localinfo_, lookup);
	} else if (entnum >= 1 && entnum <= MAX_CLIENTS) {
		client_t *const client = &svs.clients[entnum - 1];
		if (!strcmp(local, "ip")) {
			value = NET_BaseAdrToString(client->netchan.remote_address);
		} else if (!strncmp(local, "realip", 7)) {
			value = NET_BaseAdrToString(client->realip);
		} else if (!strncmp(local, "download", 9)) {
			snprintf(local_value, sizeof(local_value), "%d",
				client->file_percent ? client->file_percent : -1);
			value = local_value;
		} else if (!strcmp(local, "ping")) {
			snprintf(local_value, sizeof(local_value), "%d", SV_CalcPing(client));
			value = local_value;
		} else if (!strcmp(local, "*userid")) {
			snprintf(local_value, sizeof(local_value), "%d", client->userid);
			value = local_value;
		} else if (!strncmp(local, "login", 6)) {
			value = client->login;
		} else if (!strcmp(local, "*VIP")) {
			snprintf(local_value, sizeof(local_value), "%d", client->vip);
			value = local_value;
		} else if (!strcmp(local, "netname")) {
			value = client->name;
		} else if (!strcmp(local, "*state")) {
			switch (client->state) {
			case cs_free: value = "free"; break;
			case cs_zombie: value = "zombie"; break;
			case cs_preconnected: value = "preconnected"; break;
			case cs_connected: value = "connected"; break;
			case cs_spawned: value = "spawned"; break;
			default: value = "unknown"; break;
			}
		} else {
			value = Info_Get(&client->_userinfo_ctx_, local);
		}
	}
	const qcx_byte_count_t required = (qcx_byte_count_t)strlen(value);
	if (out != NULL && out_capacity != 0U) {
		const qcx_byte_count_t copied = required < out_capacity ? required : out_capacity;
		memcpy(out, value, copied);
	}
	return required;
}

static void QCX_Multicast(void *context, const float origin[3], float destination)
{
	(void)context;
	if (origin == NULL) {
		SV_Error("qc2cpp multicast requires an origin");
	}
	SV_Multicast((float *)origin, (int)destination);
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
