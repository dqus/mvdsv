#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

#include "qwsvdef.h"
#include "qcx/adapter.h"

static eval_t legacy_value;
static unsigned legacy_field_lookup_calls;
static unsigned legacy_profile_calls;
static unsigned legacy_print_edict_calls;
static unsigned legacy_print_edicts_calls;
static const char *diagnostics[3];
static unsigned diagnostic_count;

vm_t *sv_vm;

void PR2_Profile_f(void);
void ED2_PrintEdict_f(void);
void ED2_PrintEdicts(void);

char *PR1_GetString(int value)
{
	(void)value;
	return "";
}

void *VM_ExplicitArgPtr(vm_t *vm, intptr_t value)
{
	(void)vm;
	(void)value;
	return NULL;
}

qbool QCX_Active(void)
{
	return true;
}

eval_t *PR1_GetEdictFieldValue(edict_t *entity, char *field)
{
	(void)entity;
	(void)field;
	++legacy_field_lookup_calls;
	return &legacy_value;
}

void PR_Profile_f(void)
{
	++legacy_profile_calls;
}

void ED_PrintEdict_f(void)
{
	++legacy_print_edict_calls;
}

void ED_PrintEdicts(void)
{
	++legacy_print_edicts_calls;
}

void Con_Printf(char *format, ...)
{
	assert(diagnostic_count < sizeof(diagnostics) / sizeof(diagnostics[0]));
	diagnostics[diagnostic_count++] = format;
}

int main(void)
{
	edict_t entity;

	assert(PR2_GetEdictFieldValue(&entity, "mod_admin") == NULL);
	PR2_Profile_f();
	ED2_PrintEdict_f();
	ED2_PrintEdicts();

	assert(legacy_field_lookup_calls == 0U);
	assert(legacy_profile_calls == 0U);
	assert(legacy_print_edict_calls == 0U);
	assert(legacy_print_edicts_calls == 0U);
	assert(diagnostic_count == 3U);
	assert(strcmp(diagnostics[0], "profile is unavailable for QCX\n") == 0);
	assert(strcmp(diagnostics[1], "edict is unavailable for QCX\n") == 0);
	assert(strcmp(diagnostics[2], "edicts are unavailable for QCX\n") == 0);
	return 0;
}
