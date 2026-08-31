#include <assert.h>
#include <string.h>

#include "qwsvdef.h"
#include "qc2cpp/globals.h"
#include "qc2cpp/transport.h"

static const qc_game_api_v1_t *active_game;
static qbool qc_active;
static globalvars_t legacy_globals;
globalvars_t *pr_global_struct = &legacy_globals;
float *pr_globals = (float *)&legacy_globals;
server_t sv;

static union {
	long double alignment;
	uint8_t bytes[512];
} legacy_game_memory;

const qc_game_api_v1_t *QC_Game(void)
{
	return active_game;
}

qbool QC_Active(void)
{
	return qc_active;
}

static void assert_invalid_globals_fixture(const char *directory, const char *name)
{
	qc_transport_t *transport = NULL;
	qc_program_diagnostic_v1_t diagnostic = {0};
	assert(QC_TransportOpen(4, directory, name, NULL, &transport, &diagnostic)
		== QC_PLUGIN_OK);
	active_game = QC_TransportGame(transport);
	assert(active_game->init(active_game->context, 0, 1U) != 0U);
	qc_engine_field_exports_v1_t *fields = NULL;
	assert(active_game->memory_view(active_game->context,
		active_game->engine_fields(active_game->context), sizeof(*fields),
		_Alignof(qc_engine_field_exports_v1_t), (void **)&fields) == QC_PLUGIN_OK);
	assert(fields != NULL);
	if (strcmp(name, "bad_globals_abi") == 0) {
		assert(fields->abi_version != QC_ENGINE_FIELD_EXPORTS_ABI_VERSION_V1);
	}
	assert(!QC_ConfigureGlobals(1.0f, 2.0f, 3.0f));
	QC_ClearGlobals();
	active_game->shutdown(active_game->context);
	QC_TransportClose(transport);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	qc_transport_t *transport = NULL;
	qc_program_diagnostic_v1_t diagnostic = {0};
	assert(QC_TransportOpen(4, argv[1], "game", NULL, &transport, &diagnostic)
		== QC_PLUGIN_OK);
	active_game = QC_TransportGame(transport);
	assert(active_game->init(active_game->context, 0, 1U) != 0U);
	assert(QC_ConfigureGlobals(1.0f, 2.0f, 3.0f));
	legacy_globals.time = 101.0f;
	legacy_globals.self = 101;
	legacy_globals.other = 102;
	qc_active = true;
	*PR_Global_time() = 6.0f;
	assert(QC_Globals()->time == 6.0f);
	assert(legacy_globals.time == 101.0f);
	active_game->start_frame(active_game->context, 17.0f, 0.1f, 0U);
	assert(*PR_Global_time() == 17.0f);
	assert(legacy_globals.time == 101.0f);
	assert(QC_Globals()->parm1 == 1.0f);
	assert(QC_Globals()->parm2 == 2.0f);
	assert(QC_Globals()->parm3 == 3.0f);
	edict_t entity = {0};
	entity.e.entnum = 7;
	PR_SetGlobal_self(&entity);
	PR_SetGlobal_other(NULL);
	assert(QC_Globals()->self == 7U);
	assert(QC_Globals()->other == 0U);
	assert(legacy_globals.self == 101 && legacy_globals.other == 102);
	assert(QC_SetMapName("dm6"));
	uint8_t mapname[3] = {0};
	qc_byte_count_t required = 0U;
	assert(active_game->string_read(active_game->context, QC_SCOPE_GLOBAL, 0U,
		(const uint8_t *)"mapname", 7U, mapname, sizeof(mapname), &required)
		== QC_PLUGIN_OK);
	assert(required == sizeof(mapname));
	assert(mapname[0] == 'd' && mapname[1] == 'm' && mapname[2] == '6');
	QC_ClearGlobals();
	qc_active = false;
	assert(QC_Globals() == NULL);
	assert(*PR_Global_time() == 101.0f);
	memset(&sv, 0, sizeof(sv));
	sv.game_edicts = (entvars_t *)legacy_game_memory.bytes;
	edict_t legacy_entity = {0};
	legacy_entity.v = (entvars_t *)(legacy_game_memory.bytes + 128U);
	legacy_globals.self = 0;
	legacy_globals.other = 102;
	PR_SetGlobal_self(&legacy_entity);
	PR_SetGlobal_other(NULL);
	assert(legacy_globals.self == 128);
	assert(legacy_globals.other == 0);
	active_game->shutdown(active_game->context);
	QC_TransportClose(transport);
	assert_invalid_globals_fixture(argv[1], "bad_globals_abi");
	assert_invalid_globals_fixture(argv[1], "bad_globals_offset");
	assert_invalid_globals_fixture(argv[1], "bad_globals_size");
	return 0;
}
