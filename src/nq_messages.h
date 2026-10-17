#ifndef MVDSV_NQ_MESSAGES_H
#define MVDSV_NQ_MESSAGES_H

/* Typed NQ writes only; the caller selects NQ versus direct QW routing. */
void NQ_ResetMessages(void);
void NQ_WriteByte(int destination, int value);
void NQ_WriteChar(int destination, int value);
void NQ_WriteShort(int destination, int value);
void NQ_WriteLong(int destination, int value);
void NQ_WriteCoord(int destination, float value);
void NQ_WriteAngle(int destination, float value);
void NQ_WriteString(int destination, const char *value);
void NQ_WriteEntity(int destination, int value);

#endif
