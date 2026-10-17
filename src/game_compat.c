#include "qwsvdef.h"
#include "game_compat.h"
#ifdef QCX_ENABLED
#include "qcx/adapter.h"
#endif

qbool PR_GameIsNetQuake(void)
{
#ifdef QCX_ENABLED
	if (QCX_Active()) {
		return QCX_GameCompatibility() == QCX_GAME_PROFILE_NETQUAKE;
	}
#endif
#ifdef USE_PR2
	if (sv_vm != NULL) {
		return false;
	}
#endif
	return progs != NULL && pr_nqprogs;
}
