#include "qwsvdef.h"

#include "qc2cpp/adapter.h"
#include "qc2cpp/entities.h"
#include "qc2cpp/entries.h"

static qc_entity_id_t QC_EntrySlot(const edict_t *entity, const char *entry)
{
	const qc_entity_id_t slot = QC_EdictToSlot(entity);
	if (slot == QC_INVALID_ENTITY_ID) {
		SV_Error("qc2cpp %s received an invalid edict", entry);
	}
	return slot;
}

void QC_DispatchEdictTouch(edict_t *touched, edict_t *toucher, float time,
	float frametime)
{
	QC_EdictTouch(QC_EntrySlot(touched, "touch"), QC_EntrySlot(toucher, "touch"),
		time, frametime);
}

void QC_DispatchEdictThink(edict_t *thinking, float thinktime, float frametime)
{
	QC_EdictThink(QC_EntrySlot(thinking, "think"), thinktime, frametime);
}

void QC_DispatchEdictBlocked(edict_t *pusher, edict_t *obstacle, float time,
	float frametime)
{
	QC_EdictBlocked(QC_EntrySlot(pusher, "blocked"),
		QC_EntrySlot(obstacle, "blocked"), time, frametime);
}

void QC_DispatchClientConnect(edict_t *client, unsigned int spectator)
{
	QC_ClientConnect(QC_EntrySlot(client, "client connect"), spectator);
}

void QC_DispatchPutClientInServer(edict_t *client, unsigned int spectator)
{
	QC_PutClientInServer(QC_EntrySlot(client, "put client in server"), spectator);
}

void QC_DispatchClientDisconnect(edict_t *client, unsigned int spectator)
{
	QC_ClientDisconnect(QC_EntrySlot(client, "client disconnect"), spectator);
}

unsigned int QC_DispatchClientUserInfoChanged(edict_t *client, unsigned int after)
{
	return QC_ClientUserInfoChanged(QC_EntrySlot(client, "client userinfo changed"), after);
}

unsigned int QC_DispatchClientCommand(edict_t *client)
{
	return QC_ClientCommand(QC_EntrySlot(client, "client command"));
}

void QC_DispatchClientKill(edict_t *client)
{
	QC_ClientKill(QC_EntrySlot(client, "client kill"));
}

unsigned int QC_DispatchClientSay(edict_t *client, unsigned int team,
	const unsigned char *text, unsigned int text_size)
{
	return QC_ClientSay(QC_EntrySlot(client, "client say"), team, text, text_size);
}

void QC_DispatchClientPreThink(edict_t *client, float time, float frametime,
	unsigned int spectator)
{
	QC_ClientPreThink(QC_EntrySlot(client, "client prethink"), time, frametime,
		spectator);
}

void QC_DispatchClientPostThink(edict_t *client, float time, unsigned int spectator)
{
	QC_ClientPostThink(QC_EntrySlot(client, "client postthink"), time, spectator);
}

void QC_DispatchSetNewParms(float out_parms[16])
{
	QC_SetNewParms(out_parms);
}

void QC_DispatchSetChangeParms(edict_t *client, float out_parms[16])
{
	QC_SetChangeParms(QC_EntrySlot(client, "set change parms"), out_parms);
}
