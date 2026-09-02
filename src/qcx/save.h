#ifndef MVDSV_QC2CPP_SAVE_H
#define MVDSV_QC2CPP_SAVE_H

#include "qwsvdef.h"
#include "qcx/save_format.h"

#include <game/shared_abi.h>

qbool QCX_SaveGame(const char *name);
qbool QCX_LoadGame(const char *name);
qbool QCX_PrepareLoadGame(const char *name, char *map_name, uint32_t map_name_size);
qbool QCX_HasPreparedLoadGame(void);
qbool QCX_PrepareLoadResources(void);
qbool QCX_CommitPreparedLoadGame(void);
void QCX_DiscardPreparedLoadGame(void);
qcx_restore_status_t QCX_ValidateSaveGame(const qcx_save_image_t *image);
void QCX_ApplySaveGame(const qcx_save_image_t *image);
void QCX_SaveInvalidateConnectedSnapshot(void);

#endif
