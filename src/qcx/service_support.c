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

void QCX_ValidateText(const uint8_t *bytes, qcx_byte_count_t size, const char *what)
{
	if ((bytes == NULL && size != 0U)
		|| (size != 0U && memchr(bytes, '\0', size) != NULL)) {
		SV_Error("qc2cpp invalid %s string", what);
	}
}

void QCX_CopyText(const uint8_t *bytes, qcx_byte_count_t size, char *out,
	size_t capacity, const char *what)
{
	if (size >= capacity) {
		SV_Error("qc2cpp invalid %s string", what);
	}
	QCX_ValidateText(bytes, size, what);
	if (size != 0U) {
		memcpy(out, bytes, size);
	}
	out[size] = '\0';
}
