#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "qwsvdef.h"
#include "qcx/entities.h"

server_static_t svs;
cvar_t sv_specprint;

static entvars_t player_state;
static edict_t player;
static unsigned int deliveries[MAX_CLIENTS];
static jmp_buf validation_error;
static qbool expect_error;

void SV_Error(char *error, ...)
{
	(void)error;
	assert(expect_error);
	longjmp(validation_error, 1);
}

edict_t *QCX_SlotToEdict(qcx_entity_id_t slot)
{
	return slot == 1U ? &player : NULL;
}

int NUM_FOR_EDICT(edict_t *entity)
{
	assert(entity == &player);
	return entity->e.entnum;
}

void Con_Printf(char *format, ...)
{
	(void)format;
	abort();
}

void SV_ClientPrintf(client_t *client, int level, char *format, ...)
{
	char text[64];
	va_list args;
	assert(level == PRINT_HIGH);
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	assert(!strcmp(text, "hello%world"));
	++deliveries[client - svs.clients];
}

void SV_ClientPrintf2(client_t *client, int level, char *format, ...)
{
	(void)client;
	(void)level;
	(void)format;
	/* QCX sprint must retain normal, demo-recorded print routing. */
	abort();
}

/* Exercise the real QCX boundary and link the real PF2_sprint. Including the
 * boundary lets dead stripping discard unrelated host imports and PR2 APIs. */
#include "../../src/qcx/services_network.c"

static void check_delivery(unsigned int player_count, unsigned int spectator_count)
{
	memset(deliveries, 0, sizeof(deliveries));
	QCX_SPrint(NULL, 1U, PRINT_HIGH, (const uint8_t *)"hello%world", 11U);
	assert(deliveries[0] == player_count);
	assert(deliveries[1] == spectator_count);
	for (int index = 2; index < MAX_CLIENTS; ++index) {
		assert(deliveries[index] == 0U);
	}
}

static void test_spectator_routing(void)
{
	client_t *const target = &svs.clients[0];
	client_t *const spectator = &svs.clients[1];
	target->state = cs_connected;
	target->spec_print = 0;
	spectator->state = cs_spawned;
	spectator->spectator = true;
	spectator->spec_track = 1;
	spectator->spec_print = SPECPRINT_SPRINT;
	sv_specprint.value = SPECPRINT_SPRINT;
	check_delivery(1U, 1U);

	target->spec_print = SPECPRINT_SPRINT;
	spectator->spec_print = 0;
	check_delivery(1U, 0U);
	spectator->spec_print = SPECPRINT_SPRINT;
	spectator->state = cs_free;
	check_delivery(1U, 0U);
	spectator->state = cs_spawned;
	spectator->spec_track = 2;
	check_delivery(1U, 0U);
	spectator->spec_track = 1;
	spectator->spectator = false;
	check_delivery(1U, 0U);
	spectator->spectator = true;
	sv_specprint.value = 0;
	check_delivery(1U, 0U);
	sv_specprint.value = SPECPRINT_SPRINT;
	target->state = cs_spawned;
	check_delivery(1U, 1U);
	for (int state = cs_free; state < cs_connected; ++state) {
		target->state = state;
		check_delivery(0U, 0U);
	}
}

static void test_invalid_arguments(void)
{
	expect_error = true;
	if (setjmp(validation_error) == 0) {
		QCX_SPrint(NULL, 1U, PRINT_HIGH, (const uint8_t *)"a\0b", 3U);
		assert(!"embedded NUL accepted");
	}
	if (setjmp(validation_error) == 0) {
		QCX_SPrint(NULL, 2U, PRINT_HIGH, (const uint8_t *)"hello%world", 11U);
		assert(!"invalid slot accepted");
	}
	player.e.entnum = MAX_CLIENTS + 1;
	if (setjmp(validation_error) == 0) {
		QCX_SPrint(NULL, 1U, PRINT_HIGH, (const uint8_t *)"hello%world", 11U);
		assert(!"non-client entity accepted");
	}
	expect_error = false;
}

int main(void)
{
	player.v = &player_state;
	player.e.entnum = 1;
	test_spectator_routing();
	test_invalid_arguments();
	return 0;
}
