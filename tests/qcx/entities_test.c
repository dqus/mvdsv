#include <assert.h>
#include <string.h>

#include "qwsvdef.h"
#include "qcx/entities.h"
#include "qcx/transport.h"

static const qcx_game_api_v1_t *active_game;
static qbool qcx_active;
server_t sv;
int fofs_items2, fofs_maxspeed, fofs_gravity, fofs_movement, fofs_vw_index;
int fofs_hideentity, fofs_trackent, fofs_visibility, fofs_hide_players, fofs_teleported;

const qcx_game_api_v1_t *QCX_Game(void)
{
	return active_game;
}

qbool QCX_Active(void)
{
	return qcx_active;
}

static void assert_invalid_entities_fixture(const char *directory, const char *name)
{
	qcx_transport_t *transport = NULL;
	qcx_program_diagnostic_v1_t diagnostic = {0};
	assert(QCX_TransportOpen(4, directory, name, NULL, &transport, &diagnostic)
		== QCX_PLUGIN_OK);
	active_game = QCX_TransportGame(transport);
	assert(!QCX_ConfigureEntities(active_game->init(active_game->context, 0, 1U)));
	QCX_ClearEntities();
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
	const qcx_guest_address_t publication = active_game->init(active_game->context, 0, 1U);
	assert(publication != 0U);
	assert(QCX_ConfigureEntities(publication));
	qcx_active = true;
	sv.max_edicts = 11;
	sv.edicts[7].e.entnum = 77;
	sv.edicts[7].e.area.ed = &sv.edicts[2];
	assert(QCX_BindEntities());
	assert((void *)sv.edicts[7].v == (void *)QCX_Entity(7U));
	assert(sv.edicts[7].e.entnum == 77);
	assert(sv.edicts[7].e.area.ed == &sv.edicts[2]);
	assert(QCX_EdictToSlot(&sv.edicts[0]) == 0U);
	assert(QCX_EdictToSlot(&sv.edicts[7]) == 7U);
	assert(QCX_SlotToEdict(7U) == &sv.edicts[7]);
	assert(QCX_Entity(10U) != NULL);
	assert(QCX_Entity(11U) == NULL);
	QCX_Entity(7U)->enemy = 3;
	assert(QCX_Entity(7U)->enemy == 3);
	assert(QCX_SlotToEdict((qcx_entity_id_t)QCX_Entity(7U)->enemy) == &sv.edicts[3]);
	QCX_Entity(7U)->health = 99.0f;
	QCX_ClearEdict(&sv.edicts[7]);
	assert(QCX_Entity(7U)->health == 0.0f);
	assert(QCX_SetEntityString(&sv.edicts[7], "model", "progs/dog.mdl"));
	uint8_t model[32] = {0};
	qcx_byte_count_t model_required = 0U;
	assert(active_game->string_read(active_game->context, QCX_SCOPE_ENTITY, 7U,
		(const uint8_t *)"model", 5U, model, sizeof(model), &model_required)
		== QCX_PLUGIN_OK);
	assert(model_required == 13U && memcmp(model, "progs/dog.mdl", 13U) == 0);
	char copied_model[32] = {0};
	uint32_t copied_model_required = 0U;
	assert(QCX_CopyEntityString(&sv.edicts[7], "model", copied_model,
		sizeof(copied_model), &copied_model_required) == QCX_PLUGIN_OK);
	assert(copied_model_required == 14U && strcmp(copied_model, "progs/dog.mdl") == 0);
	assert(QCX_SetEntityString(&sv.edicts[7], "netname", "Ranger"));
	char copied_netname[16] = {0};
	assert(QCX_CopyEntityString(&sv.edicts[7], "netname", copied_netname,
		sizeof(copied_netname), NULL) == QCX_PLUGIN_OK);
	assert(strcmp(copied_model, "progs/dog.mdl") == 0);
	assert(strcmp(copied_netname, "Ranger") == 0);
	assert(QCX_SetEntityString(&sv.edicts[7], "model", ""));
	assert(QCX_CopyEntityString(&sv.edicts[7], "model", copied_model,
		sizeof(copied_model), &copied_model_required) == QCX_PLUGIN_OK);
	assert(copied_model_required == 1U && copied_model[0] == '\0');
	char legacy[7] = {0};
	uint32_t required = 0U;
	assert(QCX_CopyLegacyString(9, legacy, sizeof(legacy), &required) == QCX_PLUGIN_OK);
	assert(required == sizeof(legacy) && strcmp(legacy, "legacy") == 0);
	char too_small[6] = {0};
	assert(QCX_CopyLegacyString(9, too_small, sizeof(too_small), &required)
		== QCX_PLUGIN_BUFFER_TOO_SMALL);
	assert(required == sizeof(legacy));
	QCX_ClearEntities();
	assert(QCX_Entity(7U) == NULL);
	active_game->shutdown(active_game->context);
	QCX_TransportClose(transport);
	assert_invalid_entities_fixture(argv[1], "bad_entities_capacity");
	assert_invalid_entities_fixture(argv[1], "bad_entities_stride");
	assert_invalid_entities_fixture(argv[1], "bad_entities_overflow");
	return 0;
}
