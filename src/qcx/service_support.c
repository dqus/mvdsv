#include "qwsvdef.h"

#include "qcx/entities.h"
#include "qcx/service_support.h"

edict_t *QCX_RequireServiceEdict(qcx_entity_id_t slot, const char *what)
{
	edict_t *const entity = QCX_SlotToEdict(slot);
	if (entity == NULL || entity->v == NULL) {
		SV_Error("qc2cpp invalid %s slot %u", what, slot);
	}
	return entity;
}

int QCX_CopyText(const uint8_t *bytes, qcx_byte_count_t size, char *out,
	size_t capacity, const char *what)
{
	if ((bytes == NULL && size != 0U) || size >= capacity
		|| (size != 0U && memchr(bytes, '\0', size) != NULL)) {
		SV_Error("qc2cpp invalid %s string", what);
		return 0;
	}
	if (size != 0U) {
		memcpy(out, bytes, size);
	}
	out[size] = '\0';
	return 1;
}
