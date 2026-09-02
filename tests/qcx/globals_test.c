#include <assert.h>
#include <string.h>

#include "qwsvdef.h"
#include "qcx/globals.h"
#include "qcx/transport.h"

static const qcx_game_api_v1_t *active_game;
static qbool qcx_active;
static globalvars_t legacy_globals;
globalvars_t *pr_global_struct = &legacy_globals;
float *pr_globals = (float *)&legacy_globals;
server_t sv;

static union {
	long double alignment;
	uint8_t bytes[512];
} legacy_game_memory;

const qcx_game_api_v1_t *QCX_Game(void)
{
	return active_game;
}

qbool QCX_Active(void)
{
	return qcx_active;
}

qcx_entity_id_t QCX_EdictToSlot(const edict_t *entity)
{
	(void)entity;
	return QCX_INVALID_ENTITY_ID;
}

static void assert_invalid_globals_fixture(const char *directory, const char *name)
{
	qcx_transport_t *transport = NULL;
	qcx_program_diagnostic_v1_t diagnostic = {0};
	assert(QCX_TransportOpen(4, directory, name, NULL, &transport, &diagnostic)
		== QCX_PLUGIN_OK);
	active_game = QCX_TransportGame(transport);
	assert(active_game->init(active_game->context, 0, 1U) != 0U);
	qcx_engine_field_exports_v1_t *fields = NULL;
	assert(active_game->memory_view(active_game->context,
		active_game->engine_fields(active_game->context), sizeof(*fields),
		_Alignof(qcx_engine_field_exports_v1_t), (void **)&fields) == QCX_PLUGIN_OK);
	assert(fields != NULL);
	if (strcmp(name, "bad_globals_abi") == 0) {
		assert(fields->abi_version != QCX_ENGINE_FIELD_EXPORTS_ABI_VERSION_V1);
	}
	assert(!QCX_ConfigureGlobals(1.0f, 2.0f, 3.0f));
	QCX_ClearGlobals();
	active_game->shutdown(active_game->context);
	QCX_TransportClose(transport);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	qcx_transport_t *transport = NULL;
	qcx_program_diagnostic_v1_t diagnostic = {0};
	assert(QCX_TransportOpen(4, argv[1], "game", NULL, &transport, &diagnostic)
		== QCX_PLUGIN_OK);
	active_game = QCX_TransportGame(transport);
	assert(active_game->init(active_game->context, 0, 1U) != 0U);
	assert(QCX_ConfigureGlobals(1.0f, 2.0f, 3.0f));
	assert(pr_global_struct == (globalvars_t *)QCX_Globals());
	assert(pr_globals == (float *)QCX_Globals());
	legacy_globals.time = 101.0f;
	legacy_globals.self = 101;
	legacy_globals.other = 102;
	qcx_active = true;
	PR_GLOBAL(time) = 6.0f;
	assert(QCX_Globals()->time == 6.0f);
	assert(legacy_globals.time == 101.0f);
	active_game->start_frame(active_game->context, 17.0f, 0.1f, 0U);
	assert(PR_GLOBAL(time) == 17.0f);
	assert(legacy_globals.time == 101.0f);
	assert(QCX_Globals()->parm1 == 1.0f);
	assert(QCX_Globals()->parm2 == 2.0f);
	assert(QCX_Globals()->parm3 == 3.0f);
	edict_t entity = {0};
	entity.e.entnum = 7;
	PR_GLOBAL(self) = entity.e.entnum;
	PR_GLOBAL(other) = 0;
	assert(QCX_Globals()->self == 7U);
	assert(QCX_Globals()->other == 0U);
	assert(legacy_globals.self == 101 && legacy_globals.other == 102);
	assert(QCX_SetMapName("dm6"));
	uint8_t mapname[3] = {0};
	qcx_byte_count_t required = 0U;
	assert(active_game->string_read(active_game->context, QCX_SCOPE_GLOBAL, 0U,
		(const uint8_t *)"mapname", 7U, mapname, sizeof(mapname), &required)
		== QCX_PLUGIN_OK);
	assert(required == sizeof(mapname));
	assert(mapname[0] == 'd' && mapname[1] == 'm' && mapname[2] == '6');
	QCX_ClearGlobals();
	qcx_active = false;
	assert(QCX_Globals() == NULL);
	assert(pr_global_struct == &legacy_globals);
	assert(pr_globals == (float *)&legacy_globals);
	assert(PR_GLOBAL(time) == 101.0f);
	memset(&sv, 0, sizeof(sv));
	sv.game_edicts = (entvars_t *)legacy_game_memory.bytes;
	edict_t legacy_entity = {0};
	legacy_entity.v = (entvars_t *)(legacy_game_memory.bytes + 128U);
	legacy_globals.self = 0;
	legacy_globals.other = 102;
	PR_GLOBAL(self) = PR_EntityReference(&legacy_entity);
	PR_GLOBAL(other) = PR_EntityReference(NULL);
	assert(legacy_globals.self == 128);
	assert(legacy_globals.other == 0);
	active_game->shutdown(active_game->context);
	QCX_TransportClose(transport);
	assert_invalid_globals_fixture(argv[1], "bad_globals_abi");
	assert_invalid_globals_fixture(argv[1], "bad_globals_offset");
	assert_invalid_globals_fixture(argv[1], "bad_globals_size");
	return 0;
}
