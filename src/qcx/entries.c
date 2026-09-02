#include "qwsvdef.h"

#include "qcx/adapter.h"
#include "qcx/entities.h"
#include "qcx/entries.h"

#if defined(QCX_TESTS)
#include "qcx/test_observer.h"
#endif

static qcx_entity_id_t QCX_EntrySlot(const edict_t *entity, const char *entry)
{
	const qcx_entity_id_t slot = QCX_EdictToSlot(entity);
	if (slot == QCX_INVALID_ENTITY_ID) {
		SV_Error("qc2cpp %s received an invalid edict", entry);
	}
	return slot;
}

void QCX_DispatchEdictTouch(edict_t *touched, edict_t *toucher, float time,
	float frametime)
{
#if defined(QCX_TESTS)
	QCX_TestObserverEdictTouch(touched, toucher);
#endif
	QCX_EdictTouch(QCX_EntrySlot(touched, "touch"), QCX_EntrySlot(toucher, "touch"),
		time, frametime);
}

void QCX_DispatchEdictThink(edict_t *thinking, float thinktime, float frametime)
{
#if defined(QCX_TESTS)
	QCX_TestObserverEdictThink(thinking);
#endif
	QCX_EdictThink(QCX_EntrySlot(thinking, "think"), thinktime, frametime);
}

void QCX_DispatchEdictBlocked(edict_t *pusher, edict_t *obstacle, float time,
	float frametime)
{
	QCX_EdictBlocked(QCX_EntrySlot(pusher, "blocked"),
		QCX_EntrySlot(obstacle, "blocked"), time, frametime);
}

void QCX_DispatchClientConnect(edict_t *client, unsigned int spectator)
{
	QCX_ClientConnect(QCX_EntrySlot(client, "client connect"), spectator);
}

void QCX_DispatchPutClientInServer(edict_t *client, unsigned int spectator)
{
	QCX_PutClientInServer(QCX_EntrySlot(client, "put client in server"), spectator);
}

void QCX_DispatchClientDisconnect(edict_t *client, unsigned int spectator)
{
	QCX_ClientDisconnect(QCX_EntrySlot(client, "client disconnect"), spectator);
}

unsigned int QCX_DispatchClientUserInfoChanged(edict_t *client, unsigned int after)
{
	return QCX_ClientUserInfoChanged(QCX_EntrySlot(client, "client userinfo changed"), after);
}

unsigned int QCX_DispatchClientCommand(edict_t *client)
{
	return QCX_ClientCommand(QCX_EntrySlot(client, "client command"));
}

void QCX_DispatchClientKill(edict_t *client)
{
	QCX_ClientKill(QCX_EntrySlot(client, "client kill"));
}

unsigned int QCX_DispatchClientSay(edict_t *client, unsigned int team,
	const unsigned char *text, unsigned int text_size)
{
	return QCX_ClientSay(QCX_EntrySlot(client, "client say"), team, text, text_size);
}

void QCX_DispatchClientPreThink(edict_t *client, float time, float frametime,
	unsigned int spectator)
{
	QCX_ClientPreThink(QCX_EntrySlot(client, "client prethink"), time, frametime,
		spectator);
}

void QCX_DispatchClientPostThink(edict_t *client, float time, unsigned int spectator)
{
	QCX_ClientPostThink(QCX_EntrySlot(client, "client postthink"), time, spectator);
}

void QCX_DispatchSetNewParms(float out_parms[16])
{
	QCX_SetNewParms(out_parms);
}

void QCX_DispatchSetChangeParms(edict_t *client, float out_parms[16])
{
	QCX_SetChangeParms(QCX_EntrySlot(client, "set change parms"), out_parms);
}
