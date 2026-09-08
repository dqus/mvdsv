#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#ifdef QCX_ENABLED
#include <stdarg.h>
#include <string.h>
#endif

#include "qwsvdef.h"

server_t sv;
void ED_Count(void);

void SV_Error(char *error, ...)
{
	(void)error;
	assert(!"unexpected SV_Error");
}

static const char *legacy_model;
static unsigned legacy_string_calls;

#ifdef USE_PR2
char *PR2_GetEntityString(string_t reference)
#else
char *PR1_GetString(int reference)
#endif
{
	assert(reference == 1);
	++legacy_string_calls;
	return (char *)legacy_model;
}

#ifdef QCX_ENABLED
static const edict_t *expected_entity;
static const edict_t *hidden_model_entity;
static const edict_t *visible_model_entity;
static qbool qcx_has_model;
static unsigned qcx_has_model_calls;
static int counted_models = -1;

qbool QCX_Active(void)
{
	return true;
}

qbool QCX_EntityHasModel(const edict_t *entity)
{
	++qcx_has_model_calls;
	if (entity == hidden_model_entity)
		return false;
	if (entity == visible_model_entity)
		return true;
	assert(entity == expected_entity);
	return qcx_has_model;
}

void Con_Printf(char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	if (strcmp(format, "view      :%3i\n") == 0)
		counted_models = va_arg(arguments, int);
	va_end(arguments);
}
#endif

int main(void)
{
	entvars_t state = {0};
	edict_t entity = {0};
	entity.v = &state;
	entity.v->model = 1;

#ifdef QCX_ENABLED
	expected_entity = &entity;
	qcx_has_model = false;
	assert(!PR_EntityHasModel(&entity));
	qcx_has_model = true;
	assert(PR_EntityHasModel(&entity));
	assert(qcx_has_model_calls == 2U);
	assert(legacy_string_calls == 0U);

	/* The raw QCX model token remains nonzero even for an empty model. */
	entvars_t count_states[2] = {{0}};
	memset(&sv, 0, sizeof(sv));
	sv.num_edicts = 2;
	sv.max_edicts = 2;
	for (int index = 0; index < sv.num_edicts; ++index) {
		sv.edicts[index].v = &count_states[index];
		sv.edicts[index].v->model = 1;
	}
	hidden_model_entity = &sv.edicts[0];
	visible_model_entity = &sv.edicts[1];
	ED_Count();
	assert(counted_models == 1);
	assert(qcx_has_model_calls == 4U);
#else
	legacy_model = "";
	assert(!PR_EntityHasModel(&entity));
	legacy_model = "progs/player.mdl";
	assert(PR_EntityHasModel(&entity));
	assert(legacy_string_calls == 2U);
#endif
	return 0;
}
