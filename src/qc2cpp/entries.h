#ifndef MVDSV_QC2CPP_ENTRIES_H
#define MVDSV_QC2CPP_ENTRIES_H

struct edict_s;

/* Engine-facing re-entry boundaries.  The transport below them uses slots. */
void QC_DispatchEdictTouch(struct edict_s *touched, struct edict_s *toucher,
	float time, float frametime);
void QC_DispatchEdictThink(struct edict_s *thinking, float thinktime,
	float frametime);
void QC_DispatchEdictBlocked(struct edict_s *pusher, struct edict_s *obstacle,
	float time, float frametime);
void QC_DispatchClientConnect(struct edict_s *client, unsigned int spectator);
void QC_DispatchPutClientInServer(struct edict_s *client, unsigned int spectator);
void QC_DispatchClientDisconnect(struct edict_s *client, unsigned int spectator);
unsigned int QC_DispatchClientUserInfoChanged(struct edict_s *client,
	unsigned int after);
unsigned int QC_DispatchClientCommand(struct edict_s *client);
void QC_DispatchClientKill(struct edict_s *client);
unsigned int QC_DispatchClientSay(struct edict_s *client, unsigned int team,
	const unsigned char *text, unsigned int text_size);
void QC_DispatchClientPreThink(struct edict_s *client, float time, float frametime,
	unsigned int spectator);
void QC_DispatchClientPostThink(struct edict_s *client, float time,
	unsigned int spectator);
void QC_DispatchSetNewParms(float out_parms[16]);
void QC_DispatchSetChangeParms(struct edict_s *client, float out_parms[16]);

#endif
