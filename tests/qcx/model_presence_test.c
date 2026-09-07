#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

#include "qwsvdef.h"

server_t sv;

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
static qbool qcx_has_model;
static unsigned qcx_has_model_calls;

qbool QCX_Active(void)
{
	return true;
}

qbool QCX_EntityHasModel(const edict_t *entity)
{
	assert(entity == expected_entity);
	++qcx_has_model_calls;
	return qcx_has_model;
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
#else
	legacy_model = "";
	assert(!PR_EntityHasModel(&entity));
	legacy_model = "progs/player.mdl";
	assert(PR_EntityHasModel(&entity));
	assert(legacy_string_calls == 2U);
#endif
	return 0;
}
