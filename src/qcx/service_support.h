#ifndef MVDSV_QCX_SERVICE_SUPPORT_H
#define MVDSV_QCX_SERVICE_SUPPORT_H

#include "game/plugin_api.h"

#include <stddef.h>
#include <stdint.h>

struct edict_s;

/* Internal service-boundary validation; failures use the normal SV_Error path. */
struct edict_s *QCX_RequireServiceEdict(qcx_entity_id_t slot, const char *what);
/* Copies a validated byte span and appends NUL. Invalid input calls SV_Error;
 * callers continue only with a complete string, never a recoverable failure. */
void QCX_CopyText(const uint8_t *bytes, qcx_byte_count_t size, char *out,
	size_t capacity, const char *what);

#endif
