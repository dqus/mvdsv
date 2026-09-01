#include "qwsvdef.h"

#include "qc2cpp/entities.h"
#include "qc2cpp/globals.h"
#include "qc2cpp/services.h"

static edict_t *QC_RequireMovementEdict(qc_entity_id_t slot)
{
	edict_t *const entity = QC_SlotToEdict(slot);
	if (entity == NULL || entity->v == NULL) {
		SV_Error("qc2cpp invalid movement entity slot %u", slot);
	}
	return entity;
}

static void QC_TraceLine(void *context, const float start[3], const float end[3],
	float nomonsters, qc_entity_id_t passent, qc_trace_result_v1_t *out_result,
	qc_byte_count_t out_result_size)
{
	(void)context;
	if (start == NULL || end == NULL || out_result == NULL
		|| out_result_size < sizeof(*out_result)) {
		SV_Error("qc2cpp traceline requires complete vectors and result");
	}
	edict_t *const passedict = QC_RequireMovementEdict(passent);
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
	out_result->entity = trace.e.ent == NULL ? 0U : QC_EdictToSlot(trace.e.ent);
	if (out_result->entity == QC_INVALID_ENTITY_ID) {
		SV_Error("qc2cpp traceline returned an invalid entity");
	}
	out_result->inopen = trace.inopen;
	out_result->inwater = trace.inwater;
	out_result->reserved0 = 0U;
}

static qc_entity_id_t QC_CheckClient(void *context, qc_entity_id_t self)
{
	(void)context;
	edict_t *const result = SV_QC_CheckClient(QC_RequireMovementEdict(self));
	const qc_entity_id_t slot = QC_EdictToSlot(result);
	if (slot == QC_INVALID_ENTITY_ID) {
		SV_Error("qc2cpp checkclient returned an invalid entity");
	}
	return slot;
}

static float QC_WalkMove(void *context, qc_entity_id_t self, float yaw,
	float distance)
{
	(void)context;
	(void)self;
	qc_shared_global_state_v1_t *const globals = QC_Globals();
	if (globals == NULL) {
		SV_Error("qc2cpp walkmove has no shared globals");
	}
	const qc_entity_id_t caller_self = globals->self;
	const float result = SV_QC_WalkMove(QC_RequireMovementEdict(caller_self), yaw,
		distance);
	globals->self = caller_self;
	return result;
}

static float QC_DropToFloor(void *context, qc_entity_id_t self)
{
	(void)context;
	return SV_QC_DropToFloor(QC_RequireMovementEdict(self));
}

static float QC_CheckBottom(void *context, qc_entity_id_t entity)
{
	(void)context;
	return SV_CheckBottom(QC_RequireMovementEdict(entity)) ? 1.0f : 0.0f;
}

static float QC_PointContents(void *context, const float point[3])
{
	(void)context;
	if (point == NULL) {
		SV_Error("qc2cpp pointcontents requires a point");
	}
	return (float)SV_PointContents((float *)point);
}

static void QC_Aim(void *context, qc_entity_id_t entity, float speed,
	const float forward[3], float out_value[3], qc_byte_count_t out_value_size)
{
	(void)context;
	(void)entity;
	(void)speed;
	if (forward == NULL || out_value == NULL || out_value_size < sizeof(float) * 3U) {
		SV_Error("qc2cpp aim requires complete vectors");
	}
	VectorCopy(forward, out_value);
}

static uint32_t QC_StepDirection(void *context, qc_entity_id_t self, float yaw,
	float distance)
{
	(void)context;
	return SV_StepDirection(QC_RequireMovementEdict(self), yaw, distance) ? 1U : 0U;
}

void QC_BindMovementServices(qc_host_api_v1_t *host)
{
	if (host == NULL) {
		return;
	}
	host->traceline = QC_TraceLine;
	host->checkclient = QC_CheckClient;
	host->walkmove = QC_WalkMove;
	host->droptofloor = QC_DropToFloor;
	host->checkbottom = QC_CheckBottom;
	host->pointcontents = QC_PointContents;
	host->aim = QC_Aim;
	host->step_direction = QC_StepDirection;
}
