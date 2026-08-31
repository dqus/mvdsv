#include <assert.h>
#include <string.h>

#include "qwsvdef.h"
#include "qc2cpp/entities.h"
#include "qc2cpp/transport.h"

static const qc_game_api_v1_t *active_game;
static qbool qc_active;
server_t sv;

const qc_game_api_v1_t *QC_Game(void)
{
	return active_game;
}

qbool QC_Active(void)
{
	return qc_active;
}

static void assert_invalid_entities_fixture(const char *directory, const char *name)
{
	qc_transport_t *transport = NULL;
	qc_program_diagnostic_v1_t diagnostic = {0};
	assert(QC_TransportOpen(4, directory, name, NULL, &transport, &diagnostic)
		== QC_PLUGIN_OK);
	active_game = QC_TransportGame(transport);
	assert(!QC_ConfigureEntities(active_game->init(active_game->context, 0, 1U)));
	QC_ClearEntities();
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
	const qc_guest_address_t publication = active_game->init(active_game->context, 0, 1U);
	assert(publication != 0U);
	assert(QC_ConfigureEntities(publication));
	qc_active = true;
	sv.max_edicts = 11;
	assert(QC_BindEntities());
	assert((void *)sv.edicts[7].v == (void *)QC_Entity(7U));
	assert(QC_EdictToSlot(&sv.edicts[0]) == 0U);
	assert(QC_EdictToSlot(&sv.edicts[7]) == 7U);
	assert(QC_SlotToEdict(7U) == &sv.edicts[7]);
	assert(QC_Entity(10U) != NULL);
	assert(QC_Entity(11U) == NULL);
	QC_Entity(7U)->enemy = 3;
	assert(QC_Entity(7U)->enemy == 3);
	assert(QC_SlotToEdict((qc_entity_id_t)QC_Entity(7U)->enemy) == &sv.edicts[3]);
	QC_Entity(7U)->health = 99.0f;
	QC_ClearEdict(&sv.edicts[7]);
	assert(QC_Entity(7U)->health == 0.0f);
	assert(QC_SetEntityString(&sv.edicts[7], "model", "progs/dog.mdl"));
	uint8_t model[32] = {0};
	qc_byte_count_t model_required = 0U;
	assert(active_game->string_read(active_game->context, QC_SCOPE_ENTITY, 7U,
		(const uint8_t *)"model", 5U, model, sizeof(model), &model_required)
		== QC_PLUGIN_OK);
	assert(model_required == 13U && memcmp(model, "progs/dog.mdl", 13U) == 0);
	char copied_model[32] = {0};
	uint32_t copied_model_required = 0U;
	assert(QC_CopyEntityString(&sv.edicts[7], "model", copied_model,
		sizeof(copied_model), &copied_model_required) == QC_PLUGIN_OK);
	assert(copied_model_required == 14U && strcmp(copied_model, "progs/dog.mdl") == 0);
	assert(QC_SetEntityString(&sv.edicts[7], "netname", "Ranger"));
	char copied_netname[16] = {0};
	assert(QC_CopyEntityString(&sv.edicts[7], "netname", copied_netname,
		sizeof(copied_netname), NULL) == QC_PLUGIN_OK);
	assert(strcmp(copied_model, "progs/dog.mdl") == 0);
	assert(strcmp(copied_netname, "Ranger") == 0);
	assert(QC_SetEntityString(&sv.edicts[7], "model", ""));
	assert(QC_CopyEntityString(&sv.edicts[7], "model", copied_model,
		sizeof(copied_model), &copied_model_required) == QC_PLUGIN_OK);
	assert(copied_model_required == 1U && copied_model[0] == '\0');
	char legacy[7] = {0};
	uint32_t required = 0U;
	assert(QC_CopyLegacyString(9, legacy, sizeof(legacy), &required) == QC_PLUGIN_OK);
	assert(required == sizeof(legacy) && strcmp(legacy, "legacy") == 0);
	char too_small[6] = {0};
	assert(QC_CopyLegacyString(9, too_small, sizeof(too_small), &required)
		== QC_PLUGIN_BUFFER_TOO_SMALL);
	assert(required == sizeof(legacy));
	QC_ClearEntities();
	assert(QC_Entity(7U) == NULL);
	active_game->shutdown(active_game->context);
	QC_TransportClose(transport);
	assert_invalid_entities_fixture(argv[1], "bad_entities_capacity");
	assert_invalid_entities_fixture(argv[1], "bad_entities_stride");
	assert_invalid_entities_fixture(argv[1], "bad_entities_overflow");
	return 0;
}
