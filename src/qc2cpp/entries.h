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

#endif
