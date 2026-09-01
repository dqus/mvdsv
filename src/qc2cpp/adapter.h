#ifndef MVDSV_QC2CPP_ADAPTER_H
#define MVDSV_QC2CPP_ADAPTER_H

#include "game/plugin_api.h"

#define QC_PROGTYPE_NATIVE 4
#define QC_PROGTYPE_WASM 5

qbool QC_Active(void);
const qc_game_api_v1_t *QC_Game(void);
void QC_LoadProgs(void);
void QC_InitProg(void);
void QC_Shutdown(void);
void QC_UnloadProgs(void);
void QC_LoadEntities(const char *data);
void QC_StartFrame(float time, float frametime, qbool is_bot_frame);
void QC_EdictTouch(qc_entity_id_t touched, qc_entity_id_t toucher, float time,
	float frametime);
void QC_EdictThink(qc_entity_id_t self, float time, float frametime);
void QC_EdictBlocked(qc_entity_id_t pusher, qc_entity_id_t obstacle, float time,
	float frametime);
void QC_ClientConnect(qc_entity_id_t self, uint32_t spectator);
void QC_PutClientInServer(qc_entity_id_t self, uint32_t spectator);
void QC_ClientDisconnect(qc_entity_id_t self, uint32_t spectator);
uint32_t QC_ClientUserInfoChanged(qc_entity_id_t self, uint32_t after);
uint32_t QC_ClientCommand(qc_entity_id_t self);
void QC_ClientKill(qc_entity_id_t self);
uint32_t QC_ClientSay(qc_entity_id_t self, uint32_t team, const uint8_t *text,
	qc_byte_count_t size);
void QC_ClientPreThink(qc_entity_id_t self, float time, float frametime,
	uint32_t spectator);
void QC_ClientPostThink(qc_entity_id_t self, float time, uint32_t spectator);
void QC_SetNewParms(float out_parms[16]);
void QC_SetChangeParms(qc_entity_id_t self, float out_parms[16]);
void QC_Unpublish(void *context);
void QC_Fatal(void *context, const qc_program_diagnostic_v1_t *diagnostic);

#endif
