#ifndef MVDSV_QC2CPP_SAVE_H
#define MVDSV_QC2CPP_SAVE_H

#include "qwsvdef.h"
#include "qc2cpp/save_format.h"

#include <game/shared_abi.h>

qbool QC_SaveGame(const char *name);
qbool QC_LoadGame(const char *name);
qbool QC_PrepareLoadGame(const char *name, char *map_name, uint32_t map_name_size);
qbool QC_HasPreparedLoadGame(void);
qbool QC_PrepareLoadResources(void);
qbool QC_CommitPreparedLoadGame(void);
void QC_DiscardPreparedLoadGame(void);
qc_restore_status_t QC_ValidateSaveGame(const qc_save_image_t *image);
void QC_ApplySaveGame(const qc_save_image_t *image);
void QC_SaveInvalidateConnectedSnapshot(void);

#endif
