#ifndef MVDSV_NQ_COMPAT_H
#define MVDSV_NQ_COMPAT_H

void NQ_ApplyModelBounds(edict_t *entity, const char *model);
qbool NQ_CvarOverride(const char *name, float game_deathmatch, float *out_value);
qbool NQ_QueueLocalCommand(const char *text);

#endif
