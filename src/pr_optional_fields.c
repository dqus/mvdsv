/* Legacy optional-field discovery.  QCX resolves the same fixed matrix from
 * published descriptors in qcx/entities.c. */

#include "qwsvdef.h"

void PR1_ResolveOptionalFieldOffsets(void)
{
	fofs_items2 = ED_FindFieldOffset("items2");
	fofs_maxspeed = ED_FindFieldOffset("maxspeed");
	fofs_gravity = ED_FindFieldOffset("gravity");
	fofs_movement = ED_FindFieldOffset("movement");
	fofs_vw_index = ED_FindFieldOffset("vw_index");
	fofs_hideentity = ED_FindFieldOffset("hideentity");
	fofs_trackent = ED_FindFieldOffset("trackent");
	fofs_visibility = ED_FindFieldOffset("visclients");
	fofs_hide_players = ED_FindFieldOffset("hideplayers");
	fofs_teleported = ED_FindFieldOffset("teleported");
}
