#ifndef MVDSV_QCX_WORLD_TEXT_H
#define MVDSV_QCX_WORLD_TEXT_H

/* Lightstyles are NUL-free text (including empty), not resource paths. This
 * payload limit is shared by builtin storage and the QCMS engine section. */
enum { QCX_MAX_LIGHTSTYLE_BYTES = 255U };

/* Shared by service calls and save restore. No per-update hunk allocation. */
char *QCX_StoreLightstyle(unsigned int style, const char *value);

#endif
