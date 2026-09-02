#ifndef MVDSV_QC2CPP_ADAPTER_H
#define MVDSV_QC2CPP_ADAPTER_H

#include "game/plugin_api.h"

#define QCX_PROGTYPE_NATIVE 4
#define QCX_PROGTYPE_WASM 5

qbool QCX_Active(void);
const qcx_game_api_v1_t *QCX_Game(void);
void QCX_LoadProgs(void);
void QCX_InitProg(void);
void QCX_Shutdown(void);
void QCX_UnloadProgs(void);
void QCX_LoadEntities(const char *data);
void QCX_StartFrame(float time, float frametime, qbool is_bot_frame);
void QCX_EdictTouch(qcx_entity_id_t touched, qcx_entity_id_t toucher, float time,
	float frametime);
void QCX_EdictThink(qcx_entity_id_t self, float time, float frametime);
void QCX_EdictBlocked(qcx_entity_id_t pusher, qcx_entity_id_t obstacle, float time,
	float frametime);
void QCX_ClientConnect(qcx_entity_id_t self, uint32_t spectator);
void QCX_PutClientInServer(qcx_entity_id_t self, uint32_t spectator);
void QCX_ClientDisconnect(qcx_entity_id_t self, uint32_t spectator);
uint32_t QCX_ClientUserInfoChanged(qcx_entity_id_t self, uint32_t after);
uint32_t QCX_ClientCommand(qcx_entity_id_t self);
void QCX_ClientKill(qcx_entity_id_t self);
uint32_t QCX_ClientSay(qcx_entity_id_t self, uint32_t team, const uint8_t *text,
	qcx_byte_count_t size);
void QCX_ClientPreThink(qcx_entity_id_t self, float time, float frametime,
	uint32_t spectator);
void QCX_ClientPostThink(qcx_entity_id_t self, float time, uint32_t spectator);
void QCX_SetNewParms(float out_parms[16]);
void QCX_SetChangeParms(qcx_entity_id_t self, float out_parms[16]);
qcx_restore_status_t QCX_SetSaveSelection(const uint8_t *bitmap, qcx_byte_count_t size);
qcx_byte_count_t QCX_SaveGuest(uint8_t *out, qcx_byte_count_t capacity);
qcx_restore_status_t QCX_ValidateGuestRestore(const uint8_t *data, qcx_byte_count_t size,
	const uint8_t *selection, qcx_byte_count_t selection_size);
qcx_restore_status_t QCX_RestoreGuest(const uint8_t *data, qcx_byte_count_t size);
void QCX_Unpublish(void *context);
void QCX_Fatal(void *context, const qcx_program_diagnostic_v1_t *diagnostic);

#endif
