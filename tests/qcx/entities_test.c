#include <assert.h>
#include <string.h>

#include "qwsvdef.h"
#include "qcx/entities.h"
#include "qcx/strings.h"
#include "qcx/transport.h"

_Static_assert(_Generic((entvars_t *)0,
	qcx_shared_entity_state_v1_t *: 0,
	default: 1),
	"QCX must not replace MVDSV's entvars_t definition");
_Static_assert(_Generic(QCX_Entity(0U),
	entvars_t *: 1,
	default: 0),
	"QCX entity access must use MVDSV's entvars_t");

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

qcx_transport_kind_t QCX_GameTransportKind(void)
{
	return QCX_TRANSPORT_NATIVE;
}

static void assert_invalid_entities_fixture(const char *directory, const char *name)
{
	qcx_transport_t *transport = NULL;
	qcx_program_diagnostic_v1_t diagnostic = {0};
	assert(QCX_TransportOpen(QCX_TRANSPORT_NATIVE, directory, name, NULL, &transport, &diagnostic)
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
	assert(QCX_TransportOpen(QCX_TRANSPORT_NATIVE, argv[1], "game", NULL, &transport, &diagnostic)
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
	char copied_model[32] = {0};
	qcx_string_view_t view;
	assert(QCX_ReadLegacyString(sv.edicts[7].v->model, &view));
	assert(view.size == 13U);
	assert(QCX_BorrowStringView(&view) != NULL);
	memcpy(copied_model, QCX_BorrowStringView(&view), view.size);
	copied_model[view.size] = '\0';
	assert(strcmp(copied_model, "progs/dog.mdl") == 0);
	assert(QCX_SetEntityString(&sv.edicts[7], "netname", "Ranger"));
	char copied_netname[16] = {0};
	assert(QCX_ReadLegacyString(sv.edicts[7].v->netname, &view));
	assert(view.size == 6U && QCX_BorrowStringView(&view) != NULL);
	memcpy(copied_netname, QCX_BorrowStringView(&view), view.size);
	copied_netname[view.size] = '\0';
	assert(strcmp(copied_model, "progs/dog.mdl") == 0);
	assert(strcmp(copied_netname, "Ranger") == 0);
	assert(QCX_SetEntityString(&sv.edicts[7], "model", ""));
	assert(QCX_ReadLegacyString(sv.edicts[7].v->model, &view));
	assert(view.size == 0U && strcmp(QCX_BorrowStringView(&view), "") == 0);
	assert(strcmp(copied_model, "progs/dog.mdl") == 0);
	QCX_ClearEntities();
	assert(QCX_Entity(7U) == NULL);
	active_game->shutdown(active_game->context);
	QCX_TransportClose(transport);
	assert_invalid_entities_fixture(argv[1], "bad_entities_capacity");
	assert_invalid_entities_fixture(argv[1], "bad_entities_stride");
	assert_invalid_entities_fixture(argv[1], "bad_entities_overflow");
	return 0;
}
