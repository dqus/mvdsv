#ifndef MVDSV_QC2CPP_ENTITIES_H
#define MVDSV_QC2CPP_ENTITIES_H

#include "qwsvdef.h"
#include "game/plugin_api.h"
#include "game/shared_entity_state.h"

#include <stdint.h>

struct edict_s;

#define QCX_INVALID_ENTITY_ID UINT32_MAX

int QCX_ConfigureEntities(qcx_guest_address_t publication_address);
int QCX_BindEntities(void);
void QCX_ClearEntities(void);
entvars_t *QCX_Entity(qcx_entity_id_t slot);
uint32_t QCX_EntityCapacity(void);
int QCX_ResolveEntityFields(void);
const char *QCX_EntityFieldError(void);
int QCX_EntityStringHeaderAddress(qcx_legacy_string_ref_t ref,
	qcx_guest_address_t *header);
qbool QCX_EntityHasModel(const struct edict_s *entity);
qcx_entity_id_t QCX_EdictToSlot(const struct edict_s *edict);
struct edict_s *QCX_SlotToEdict(qcx_entity_id_t slot);
void QCX_ClearEdict(struct edict_s *edict);
int QCX_SetEntityString(struct edict_s *edict, const char *field, const char *value);
const char *QCX_EntityStringFieldName(const struct edict_s *edict, const void *member);

#endif
