#ifndef MVDSV_QC2CPP_ENTRIES_H
#define MVDSV_QC2CPP_ENTRIES_H

struct edict_s;

/* Engine-facing re-entry boundaries.  The transport below them uses slots. */
void QCX_DispatchEdictTouch(struct edict_s *touched, struct edict_s *toucher,
	float time, float frametime);
void QCX_DispatchEdictThink(struct edict_s *thinking, float thinktime,
	float frametime);
void QCX_DispatchEdictBlocked(struct edict_s *pusher, struct edict_s *obstacle,
	float time, float frametime);
void QCX_DispatchClientConnect(struct edict_s *client, unsigned int spectator);
void QCX_DispatchPutClientInServer(struct edict_s *client, unsigned int spectator);
void QCX_DispatchClientDisconnect(struct edict_s *client, unsigned int spectator);
unsigned int QCX_DispatchClientUserInfoChanged(struct edict_s *client,
	unsigned int after);
unsigned int QCX_DispatchClientCommand(struct edict_s *client);
void QCX_DispatchClientKill(struct edict_s *client);
unsigned int QCX_DispatchClientSay(struct edict_s *client, unsigned int team,
	const unsigned char *text, unsigned int text_size);
void QCX_DispatchClientPreThink(struct edict_s *client, float time, float frametime,
	unsigned int spectator);
void QCX_DispatchClientPostThink(struct edict_s *client, float time,
	unsigned int spectator);
void QCX_DispatchSetNewParms(float out_parms[16]);
void QCX_DispatchSetChangeParms(struct edict_s *client, float out_parms[16]);

#endif
