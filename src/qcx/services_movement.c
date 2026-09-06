#include "qwsvdef.h"

#include "qcx/entities.h"
#include "qcx/globals.h"
#include "qcx/services.h"

static edict_t *QCX_RequireMovementEdict(qcx_entity_id_t slot)
{
	edict_t *const entity = QCX_SlotToEdict(slot);
	if (entity == NULL || entity->v == NULL) {
		SV_Error("qc2cpp invalid movement entity slot %u", slot);
	}
	return entity;
}

static void QCX_TraceLine(void *context, const float start[3], const float end[3],
	float nomonsters, qcx_entity_id_t passent, qcx_trace_result_v1_t *out_result,
	qcx_byte_count_t out_result_size)
{
	QCX_ObserveGameplayImport(context);
	if (start == NULL || end == NULL || out_result == NULL
		|| out_result_size < sizeof(*out_result)) {
		SV_Error("qc2cpp traceline requires complete vectors and result");
	}
	edict_t *const passedict = QCX_RequireMovementEdict(passent);
	int type = (int)nomonsters;
	if (sv_antilag.value == 2.0f) {
		type |= MOVE_LAGGED;
	}
	const trace_t trace = SV_Trace((float *)start, vec3_origin, vec3_origin,
		(float *)end, type, passedict);
	out_result->allsolid = trace.allsolid;
	out_result->startsolid = trace.startsolid;
	out_result->fraction = trace.fraction;
	VectorCopy(trace.endpos, out_result->endpos);
	VectorCopy(trace.plane.normal, out_result->plane_normal);
	out_result->plane_dist = trace.plane.dist;
	out_result->entity = trace.e.ent == NULL ? 0U : QCX_EdictToSlot(trace.e.ent);
	if (out_result->entity == QCX_INVALID_ENTITY_ID) {
		SV_Error("qc2cpp traceline returned an invalid entity");
	}
	out_result->inopen = trace.inopen;
	out_result->inwater = trace.inwater;
	out_result->reserved0 = 0U;
}

static qcx_entity_id_t QCX_CheckClient(void *context, qcx_entity_id_t self)
{
	QCX_ObserveGameplayImport(context);
	edict_t *const result = PF2_checkclient(QCX_RequireMovementEdict(self));
	const qcx_entity_id_t slot = QCX_EdictToSlot(result);
	if (slot == QCX_INVALID_ENTITY_ID) {
		SV_Error("qc2cpp checkclient returned an invalid entity");
	}
	return slot;
}

static float QCX_WalkMove(void *context, qcx_entity_id_t self, float yaw,
	float distance)
{
	QCX_ObserveGameplayImport(context);
	(void)self;
	qcx_shared_global_state_v1_t *const globals = QCX_Globals();
	if (globals == NULL) {
		SV_Error("qc2cpp walkmove has no shared globals");
	}
	const qcx_entity_id_t caller_self = globals->self;
	const float result = (float)PF2_walkmove(
		QCX_RequireMovementEdict(caller_self), yaw, distance);
	globals->self = caller_self;
	return result;
}

static float QCX_DropToFloor(void *context, qcx_entity_id_t self)
{
	QCX_ObserveGameplayImport(context);
	return (float)PF2_droptofloor(QCX_RequireMovementEdict(self));
}

static float QCX_CheckBottom(void *context, qcx_entity_id_t entity)
{
	QCX_ObserveGameplayImport(context);
	return SV_CheckBottom(QCX_RequireMovementEdict(entity)) ? 1.0f : 0.0f;
}

static float QCX_PointContents(void *context, const float point[3])
{
	QCX_ObserveGameplayImport(context);
	if (point == NULL) {
		SV_Error("qc2cpp pointcontents requires a point");
	}
	return (float)PF2_pointcontents(point[0], point[1], point[2]);
}

static void QCX_Aim(void *context, qcx_entity_id_t entity, float speed,
	const float forward[3], float out_value[3], qcx_byte_count_t out_value_size)
{
	QCX_ObserveGameplayImport(context);
	(void)entity;
	(void)speed;
	if (forward == NULL || out_value == NULL || out_value_size < sizeof(float) * 3U) {
		SV_Error("qc2cpp aim requires complete vectors");
	}
	VectorCopy(forward, out_value);
}

static uint32_t QCX_StepDirection(void *context, qcx_entity_id_t self, float yaw,
	float distance)
{
	QCX_ObserveGameplayImport(context);
	return SV_StepDirection(QCX_RequireMovementEdict(self), yaw, distance) ? 1U : 0U;
}

void QCX_BindMovementServices(qcx_host_api_v1_t *host)
{
	if (host == NULL) {
		return;
	}
	host->traceline = QCX_TraceLine;
	host->checkclient = QCX_CheckClient;
	host->walkmove = QCX_WalkMove;
	host->droptofloor = QCX_DropToFloor;
	host->checkbottom = QCX_CheckBottom;
	host->pointcontents = QCX_PointContents;
	host->aim = QCX_Aim;
	host->step_direction = QCX_StepDirection;
}
