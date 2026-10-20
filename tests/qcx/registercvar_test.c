#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "qwsvdef.h"

/* Exercise the real static import, without exporting test-only production API. */
#include "../../src/qcx/services_world.c"

static jmp_buf validation_error;
static unsigned int callbacks;

void SV_Error(char *format, ...)
{
	(void)format;
	longjmp(validation_error, 1);
}

void Sys_Error(const char *format, ...)
{
	(void)format;
	abort();
}

qbool Cmd_Exists(const char *name)
{
	return !strcasecmp(name, "status");
}

static void on_change(cvar_t *var, char *value, qbool *cancel)
{
	(void)var; (void)value; (void)cancel;
	++callbacks;
}

static float register_value(const char *name, const char *value)
{
	return QCX_RegisterCvar(NULL, (const uint8_t *)name, strlen(name),
		(const uint8_t *)value, strlen(value));
}

static void reject(const uint8_t *name, qcx_byte_count_t name_size,
	const uint8_t *value, qcx_byte_count_t value_size)
{
	if (setjmp(validation_error) == 0) {
		QCX_RegisterCvar(NULL, name, name_size, value, value_size);
		abort();
	}
	assert(Cvar_Find("absent") == NULL);
}

int main(void)
{
	char name[] = "qcx_reg_value";
	char value[] = "7.25";
	assert(register_value(name, value) == 1.0f);
	cvar_t *const var = Cvar_Find(name);
	assert(var != NULL && var->flags == 0 && var->value == 7.25f);
	memset(name, 'x', strlen(name));
	memset(value, 'x', strlen(value));
	assert(!strcmp(var->name, "qcx_reg_value") && !strcmp(var->string, "7.25"));
	var->flags = CVAR_USER_CREATED;
	var->OnChange = on_change;
	assert(register_value("qcx_reg_value", "99") == 0.0f);
	assert(register_value("QCX_REG_VALUE", "55") == 0.0f);
	assert(!strcmp(var->string, "7.25") && var->value == 7.25f);
	assert(var->flags == CVAR_USER_CREATED && var->OnChange == on_change && callbacks == 0U);
	assert(register_value("empty", "") == 1.0f);
	assert(register_value("empty", "replacement") == 0.0f);
	assert(!strcmp(Cvar_Find("empty")->string, ""));
	assert(register_value("zero", "0") == 1.0f);
	assert(register_value("zero", "1") == 0.0f);
	assert(!strcmp(Cvar_Find("zero")->string, "0"));
	assert(register_value("", "value") == 0.0f && Cvar_Find("") == NULL);
	assert(register_value("STATUS", "value") == 0.0f && Cvar_Find("status") == NULL);
	static const uint8_t absent[] = "absent";
	static const uint8_t nul[] = {'a', 0, 'b'};
	uint8_t oversized_name[MAX_QPATH];
	uint8_t oversized_value[MAX_INFO_STRING];
	memset(oversized_name, 'a', sizeof(oversized_name));
	memset(oversized_value, 'a', sizeof(oversized_value));
	reject(nul, sizeof(nul), absent, 6U);
	reject(oversized_name, sizeof(oversized_name), absent, 6U);
	reject(absent, 6U, nul, sizeof(nul));
	reject(absent, 6U, oversized_value, sizeof(oversized_value));
	reject(NULL, 1U, absent, 6U);
	reject(absent, 6U, NULL, 1U);
	assert(callbacks == 0U);
	return 0;
}
