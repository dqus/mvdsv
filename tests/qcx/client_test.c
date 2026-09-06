#include "game/plugin_api.h"
#include "qcx/services.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "qwsvdef.h"

server_t sv;
server_static_t svs;
ctxinfo_t _localinfo_;
cvar_t sv_specprint;
int msg_coordsize = 2;
int msg_anglesize = 1;

static entvars_t entity_state[2];
static edict_t entities[2];
static int direct_destination;
static char printed[MAX_INFO_STRING];
static int stuffcmd_calls;
static int centerprint_calls;
static int logfrag_calls;
static int multicast_calls;
static int write_byte_calls;
static int write_char_calls;
static int write_short_calls;
static int write_long_calls;
static int write_coord_calls;
static int write_angle_calls;
static int write_string_calls;
static int write_entity_calls;
static int setspawnparms_calls;
static int infokey_calls;

void SV_Error(char *error, ...) { (void)error; abort(); }
edict_t *QCX_SlotToEdict(qcx_entity_id_t slot) { return slot < 2U ? &entities[slot] : NULL; }
int NUM_FOR_EDICT(edict_t *entity) { return entity->e.entnum; }
void SV_StartSound(edict_t *entity, int channel, char *sample, int volume, float attenuation)
{ assert(entity == &entities[0]); assert(channel == 2); assert(!strcmp(sample, "misc/test.wav")); assert(volume == 127); assert(attenuation == 1.0f); }
void SV_BroadcastPrintf(int level, char *format, ...)
{ va_list args; assert(level == 2); va_start(args, format); vsnprintf(printed, sizeof(printed), format, args); va_end(args); }
void SV_ClientPrintf(client_t *client, int level, char *format, ...)
{ va_list args; assert(client == &svs.clients[0]); assert(level == 1); va_start(args, format); vsnprintf(printed, sizeof(printed), format, args); va_end(args); }
void ClientReliableCheckBlock(client_t *client, int size) { (void)client; (void)size; abort(); }
void ClientReliableWrite_Begin(client_t *client, int command, int size) { (void)client; (void)command; (void)size; abort(); }
void ClientReliableWrite_Byte(client_t *client, int value) { (void)client; (void)value; abort(); }
void ClientReliableWrite_Char(client_t *client, int value) { (void)client; (void)value; abort(); }
void ClientReliableWrite_Short(client_t *client, int value) { (void)client; (void)value; abort(); }
void ClientReliableWrite_Long(client_t *client, int value) { (void)client; (void)value; abort(); }
void ClientReliableWrite_Coord(client_t *client, float value) { (void)client; (void)value; abort(); }
void ClientReliableWrite_Angle(client_t *client, float value) { (void)client; (void)value; abort(); }
void ClientReliableWrite_String(client_t *client, char *value) { (void)client; (void)value; abort(); }
void MSG_WriteByte(sizebuf_t *buffer, int value) { (void)buffer; (void)value; abort(); }
void MSG_WriteChar(sizebuf_t *buffer, int value) { (void)buffer; (void)value; abort(); }
void MSG_WriteShort(sizebuf_t *buffer, int value) { (void)buffer; (void)value; abort(); }
void MSG_WriteLong(sizebuf_t *buffer, int value) { (void)buffer; (void)value; abort(); }
void MSG_WriteCoord(sizebuf_t *buffer, float value) { (void)buffer; (void)value; abort(); }
void MSG_WriteAngle(sizebuf_t *buffer, float value) { (void)buffer; (void)value; abort(); }
void MSG_WriteString(sizebuf_t *buffer, const char *value) { (void)buffer; (void)value; abort(); }
void SV_Multicast(vec3_t origin, int destination)
{
	(void)origin;
	(void)destination;
	abort();
}
qbool MVDWrite_Begin(byte type, int recipient, int size) { (void)type; (void)recipient; (void)size; return true; }
void MVD_MSG_WriteByte(const int value) { (void)value; }
void MVD_MSG_WriteShort(const int value) { (void)value; }
void MVD_MSG_WriteLong(const int value) { (void)value; }
void MVD_MSG_WriteCoord(const float value) { (void)value; }
void MVD_MSG_WriteAngle(const float value) { (void)value; }
void MVD_MSG_WriteString(const char *value) { (void)value; }
void PF2_stuffcmd(int entnum, char *text, int flags)
{
	assert(entnum == 1);
	assert(!strcmp(text, "cmd\n"));
	assert(flags == 0);
	++stuffcmd_calls;
}

void PF2_centerprint(int entnum, char *text)
{
	assert(entnum == 1);
	assert(!strcmp(text, "center"));
	++centerprint_calls;
}

void PF2_logfrag(int killer_entnum, int victim_entnum)
{
	assert(killer_entnum == 1);
	assert(victim_entnum == 7);
	++logfrag_calls;
}

void PF2_multicast(float x, float y, float z, int destination)
{
	assert(x == 1.0f && y == 2.0f && z == 3.0f);
	direct_destination = destination;
	++multicast_calls;
}

void PF2_WriteByte(int to, int value, edict_t *msg_entity)
{
	assert(to == 1 && value == 37 && msg_entity == &entities[0]);
	++write_byte_calls;
}

void PF2_WriteChar(int to, int value, edict_t *msg_entity)
{
	assert(to == 0 && value == 38 && msg_entity == NULL);
	++write_char_calls;
}

void PF2_WriteShort(int to, int value, edict_t *msg_entity)
{
	assert(to == 2 && value == 39 && msg_entity == NULL);
	++write_short_calls;
}

void PF2_WriteLong(int to, int value, edict_t *msg_entity)
{
	assert(to == 3 && value == 40 && msg_entity == NULL);
	++write_long_calls;
}

void PF2_WriteCoord(int to, float value, edict_t *msg_entity)
{
	assert(to == 4 && value == 41.0f && msg_entity == NULL);
	++write_coord_calls;
}

void PF2_WriteAngle(int to, float value, edict_t *msg_entity)
{
	assert(to == 0 && value == 42.0f && msg_entity == NULL);
	++write_angle_calls;
}

void PF2_WriteString(int to, char *value, edict_t *msg_entity)
{
	assert(to == 0 && !strcmp(value, "message") && msg_entity == NULL);
	++write_string_calls;
}

void PF2_WriteEntity(int to, int value_entnum, edict_t *msg_entity)
{
	assert(to == 0 && value_entnum == 7 && msg_entity == NULL);
	++write_entity_calls;
}

void PF2_setspawnparms(int entnum, float *out_parms)
{
	assert(entnum == 1);
	out_parms[0] = 4.0f;
	++setspawnparms_calls;
}

const char *PF2_infokey(int entnum, const char *key)
{
	assert(entnum == 1);
	++infokey_calls;
	return !strcmp(key, "name") || !strcmp(key, "netname") ? "player" : "";
}
void SZ_Print(sizebuf_t *buffer, const char *text) { (void)buffer; strlcpy(printed, text, sizeof(printed)); }
void SV_Write_Log(int type, int level, char *text) { (void)type; (void)level; strlcpy(printed, text, sizeof(printed)); }
char *va(const char *format, ...) { static char value[MAX_INFO_STRING]; va_list args; va_start(args, format); vsnprintf(value, sizeof(value), format, args); va_end(args); return value; }
char *Info_Get(ctxinfo_t *context, const char *key) { (void)context; return !strcmp(key, "name") ? "player" : ""; }
char *Info_ValueForKey(char *info, const char *key) { (void)info; (void)key; return ""; }
char *NET_BaseAdrToString(netadr_t address) { (void)address; return "127.0.0.1"; }
int SV_CalcPing(client_t *client) { (void)client; return 42; }
char *VersionStringFull(void) { return "test"; }
void SV_TimeOfDay(date_t *date, char *format) { (void)format; strlcpy(date->str, "date", sizeof(date->str)); }

int main(void)
{
	for (int index = 0; index < 2; ++index) entities[index].v = &entity_state[index];
	entities[0].e.entnum = 1;
	entities[1].e.entnum = 7;
	strlcpy(svs.clients[0].name, "player", sizeof(svs.clients[0].name));
	svs.clients[0].spawn_parms[0] = 4.0f;
	qcx_host_api_v1_t host = {
		.abi_version = QCX_PLUGIN_ABI_VERSION_V1,
		.struct_size = sizeof(host),
	};

	QCX_BindNetworkServices(&host);
	assert(host.sound != NULL);
	assert(host.stuffcmd != NULL);
	assert(host.bprint != NULL);
	assert(host.sprint != NULL);
	assert(host.write_byte != NULL);
	assert(host.write_string != NULL);
	assert(host.centerprint != NULL);
	assert(host.ambientsound != NULL);
	assert(host.setspawnparms != NULL);
	assert(host.logfrag != NULL);
	assert(host.infokey != NULL);
	assert(host.multicast != NULL);
	host.sound(host.context, 0U, 2.0f, (const uint8_t *)"misc/test.wav", 13U, 0.5f, 1.0f);
	host.bprint(host.context, 2.0f, (const uint8_t *)"hello", 5U);
	assert(!strcmp(printed, "hello"));
	host.sprint(host.context, 0U, 1.0f, (const uint8_t *)"client", 6U);
	assert(!strcmp(printed, "client"));
	host.stuffcmd(host.context, 0U, (const uint8_t *)"cmd\n", 4U);
	assert(stuffcmd_calls == 1);
	host.centerprint(host.context, 0U, (const uint8_t *)"center", 6U);
	assert(centerprint_calls == 1);
	host.logfrag(host.context, 0U, 1U);
	assert(logfrag_calls == 1);
	host.write_byte(host.context, 1.0f, 37.0f, 0U);
	host.write_char(host.context, 0.0f, 38.0f, 2U);
	host.write_short(host.context, 2.0f, 39.0f, 2U);
	host.write_long(host.context, 3.0f, 40.0f, 2U);
	host.write_coord(host.context, 4.0f, 41.0f, 2U);
	host.write_angle(host.context, 0.0f, 42.0f, 2U);
	host.write_string(host.context, 0.0f, (const uint8_t *)"message", 7U, 2U);
	host.write_entity(host.context, 0.0f, 1U, 2U);
	assert(write_byte_calls == 1 && write_char_calls == 1 && write_short_calls == 1
		&& write_long_calls == 1 && write_coord_calls == 1 && write_angle_calls == 1
		&& write_string_calls == 1 && write_entity_calls == 1);
	uint8_t info[16] = {0};
	assert(host.infokey(host.context, 0U, (const uint8_t *)"name", 4U, info, sizeof(info)) == 6U);
	assert(!memcmp(info, "player", 6U));
	memset(info, 0, sizeof(info));
	assert(host.infokey(host.context, 0U, (const uint8_t *)"netname", 7U, info, sizeof(info)) == 6U);
	assert(!memcmp(info, "player", 6U));
	uint8_t truncated_info[4] = {0U, 0U, 0U, 0xffU};
	assert(host.infokey(host.context, 0U, (const uint8_t *)"name", 4U, truncated_info, 3U) == 6U);
	assert(!memcmp(truncated_info, "pla", 3U) && truncated_info[3] == 0xffU);
	assert(infokey_calls == 3);
	float parms[16] = {0};
	host.setspawnparms(host.context, 0U, parms, sizeof(parms));
	assert(parms[0] == 4.0f && setspawnparms_calls == 1);
	const float origin[3] = {1.0f, 2.0f, 3.0f};
	host.multicast(host.context, origin, 2.0f);
	assert(direct_destination == 2 && multicast_calls == 1);
	return 0;
}
