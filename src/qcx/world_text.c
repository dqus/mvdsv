#include "qwsvdef.h"
#include "qcx/world_text.h"

static char qcx_lightstyles[MAX_LIGHTSTYLES][QCX_MAX_LIGHTSTYLE_BYTES + 1U];

char *QCX_StoreLightstyle(unsigned int style, const char *value)
{
	if (style >= MAX_LIGHTSTYLES || value == NULL
		|| strlen(value) > QCX_MAX_LIGHTSTYLE_BYTES) {
		SV_Error("qc2cpp invalid lightstyle storage");
	}
	strlcpy(qcx_lightstyles[style], value, sizeof(qcx_lightstyles[style]));
	return qcx_lightstyles[style];
}
