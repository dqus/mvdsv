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
void QC_Unpublish(void *context);
void QC_Fatal(void *context, const qc_program_diagnostic_v1_t *diagnostic);

#endif
