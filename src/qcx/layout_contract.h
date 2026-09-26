#ifndef MVDSV_QCX_LAYOUT_CONTRACT_H
#define MVDSV_QCX_LAYOUT_CONTRACT_H

#include "qwsvdef.h"
#include "game/shared_entity_state.h"
#include "game/shared_global_state.h"

#include <stddef.h>

/* QCX publishes these words; MVDSV accesses them through its own progdefs.h types. */
_Static_assert(PROGHEADER_CRC == QCX_SHARED_ENTITY_STATE_PROGHEADER_CRC,
	"QCX entity CRC differs from MVDSV");
_Static_assert(sizeof(entvars_t) == sizeof(qcx_shared_entity_state_v1_t),
	"QCX entity size differs from MVDSV");
_Static_assert(_Alignof(entvars_t) == _Alignof(qcx_shared_entity_state_v1_t),
	"QCX entity alignment differs from MVDSV");

#define QCX_ASSERT_ENTITY_FIELD(field) \
	_Static_assert(offsetof(entvars_t, field) == offsetof(qcx_shared_entity_state_v1_t, field) \
		&& sizeof(((entvars_t *)0)->field) == sizeof(((qcx_shared_entity_state_v1_t *)0)->field), \
		"QCX entity field differs from MVDSV: " #field)

QCX_ASSERT_ENTITY_FIELD(modelindex);
QCX_ASSERT_ENTITY_FIELD(absmin);
QCX_ASSERT_ENTITY_FIELD(absmax);
QCX_ASSERT_ENTITY_FIELD(ltime);
QCX_ASSERT_ENTITY_FIELD(lastruntime);
QCX_ASSERT_ENTITY_FIELD(movetype);
QCX_ASSERT_ENTITY_FIELD(solid);
QCX_ASSERT_ENTITY_FIELD(origin);
QCX_ASSERT_ENTITY_FIELD(oldorigin);
QCX_ASSERT_ENTITY_FIELD(velocity);
QCX_ASSERT_ENTITY_FIELD(angles);
QCX_ASSERT_ENTITY_FIELD(avelocity);
QCX_ASSERT_ENTITY_FIELD(classname);
QCX_ASSERT_ENTITY_FIELD(model);
QCX_ASSERT_ENTITY_FIELD(frame);
QCX_ASSERT_ENTITY_FIELD(skin);
QCX_ASSERT_ENTITY_FIELD(effects);
QCX_ASSERT_ENTITY_FIELD(mins);
QCX_ASSERT_ENTITY_FIELD(maxs);
QCX_ASSERT_ENTITY_FIELD(size);
QCX_ASSERT_ENTITY_FIELD(touch);
QCX_ASSERT_ENTITY_FIELD(use);
QCX_ASSERT_ENTITY_FIELD(think);
QCX_ASSERT_ENTITY_FIELD(blocked);
QCX_ASSERT_ENTITY_FIELD(nextthink);
QCX_ASSERT_ENTITY_FIELD(groundentity);
QCX_ASSERT_ENTITY_FIELD(health);
QCX_ASSERT_ENTITY_FIELD(frags);
QCX_ASSERT_ENTITY_FIELD(weapon);
QCX_ASSERT_ENTITY_FIELD(weaponmodel);
QCX_ASSERT_ENTITY_FIELD(weaponframe);
QCX_ASSERT_ENTITY_FIELD(currentammo);
QCX_ASSERT_ENTITY_FIELD(ammo_shells);
QCX_ASSERT_ENTITY_FIELD(ammo_nails);
QCX_ASSERT_ENTITY_FIELD(ammo_rockets);
QCX_ASSERT_ENTITY_FIELD(ammo_cells);
QCX_ASSERT_ENTITY_FIELD(items);
QCX_ASSERT_ENTITY_FIELD(takedamage);
QCX_ASSERT_ENTITY_FIELD(chain);
QCX_ASSERT_ENTITY_FIELD(deadflag);
QCX_ASSERT_ENTITY_FIELD(view_ofs);
QCX_ASSERT_ENTITY_FIELD(button0);
QCX_ASSERT_ENTITY_FIELD(button1);
QCX_ASSERT_ENTITY_FIELD(button2);
QCX_ASSERT_ENTITY_FIELD(impulse);
QCX_ASSERT_ENTITY_FIELD(fixangle);
QCX_ASSERT_ENTITY_FIELD(v_angle);
QCX_ASSERT_ENTITY_FIELD(netname);
QCX_ASSERT_ENTITY_FIELD(enemy);
QCX_ASSERT_ENTITY_FIELD(flags);
QCX_ASSERT_ENTITY_FIELD(colormap);
QCX_ASSERT_ENTITY_FIELD(team);
QCX_ASSERT_ENTITY_FIELD(max_health);
QCX_ASSERT_ENTITY_FIELD(teleport_time);
QCX_ASSERT_ENTITY_FIELD(armortype);
QCX_ASSERT_ENTITY_FIELD(armorvalue);
QCX_ASSERT_ENTITY_FIELD(waterlevel);
QCX_ASSERT_ENTITY_FIELD(watertype);
QCX_ASSERT_ENTITY_FIELD(ideal_yaw);
QCX_ASSERT_ENTITY_FIELD(yaw_speed);
QCX_ASSERT_ENTITY_FIELD(aiment);
QCX_ASSERT_ENTITY_FIELD(goalentity);
QCX_ASSERT_ENTITY_FIELD(spawnflags);
QCX_ASSERT_ENTITY_FIELD(target);
QCX_ASSERT_ENTITY_FIELD(targetname);
QCX_ASSERT_ENTITY_FIELD(dmg_take);
QCX_ASSERT_ENTITY_FIELD(dmg_save);
QCX_ASSERT_ENTITY_FIELD(dmg_inflictor);
QCX_ASSERT_ENTITY_FIELD(owner);
QCX_ASSERT_ENTITY_FIELD(movedir);
QCX_ASSERT_ENTITY_FIELD(message);
QCX_ASSERT_ENTITY_FIELD(sounds);
QCX_ASSERT_ENTITY_FIELD(noise);
QCX_ASSERT_ENTITY_FIELD(noise1);
QCX_ASSERT_ENTITY_FIELD(noise2);
QCX_ASSERT_ENTITY_FIELD(noise3);

#undef QCX_ASSERT_ENTITY_FIELD

_Static_assert(sizeof(globalvars_t) == sizeof(qcx_shared_global_state_v1_t),
	"QCX globals size differs from MVDSV");
_Static_assert(_Alignof(globalvars_t) == _Alignof(qcx_shared_global_state_v1_t),
	"QCX globals alignment differs from MVDSV");

#define QCX_ASSERT_GLOBAL_FIELD(field, shared_field) \
	_Static_assert(offsetof(globalvars_t, field) == offsetof(qcx_shared_global_state_v1_t, shared_field) \
		&& sizeof(((globalvars_t *)0)->field) == sizeof(((qcx_shared_global_state_v1_t *)0)->shared_field), \
		"QCX global field differs from MVDSV: " #field)

QCX_ASSERT_GLOBAL_FIELD(pad, reserved_call_frame);
QCX_ASSERT_GLOBAL_FIELD(self, self);
QCX_ASSERT_GLOBAL_FIELD(other, other);
QCX_ASSERT_GLOBAL_FIELD(world, world);
QCX_ASSERT_GLOBAL_FIELD(time, time);
QCX_ASSERT_GLOBAL_FIELD(frametime, frametime);
QCX_ASSERT_GLOBAL_FIELD(newmis, newmis);
QCX_ASSERT_GLOBAL_FIELD(force_retouch, force_retouch);
QCX_ASSERT_GLOBAL_FIELD(mapname, reserved_mapname);
QCX_ASSERT_GLOBAL_FIELD(serverflags, serverflags);
QCX_ASSERT_GLOBAL_FIELD(total_secrets, total_secrets);
QCX_ASSERT_GLOBAL_FIELD(total_monsters, total_monsters);
QCX_ASSERT_GLOBAL_FIELD(found_secrets, found_secrets);
QCX_ASSERT_GLOBAL_FIELD(killed_monsters, killed_monsters);
QCX_ASSERT_GLOBAL_FIELD(parm1, parm1);
QCX_ASSERT_GLOBAL_FIELD(parm2, parm2);
QCX_ASSERT_GLOBAL_FIELD(parm3, parm3);
QCX_ASSERT_GLOBAL_FIELD(parm4, parm4);
QCX_ASSERT_GLOBAL_FIELD(parm5, parm5);
QCX_ASSERT_GLOBAL_FIELD(parm6, parm6);
QCX_ASSERT_GLOBAL_FIELD(parm7, parm7);
QCX_ASSERT_GLOBAL_FIELD(parm8, parm8);
QCX_ASSERT_GLOBAL_FIELD(parm9, parm9);
QCX_ASSERT_GLOBAL_FIELD(parm10, parm10);
QCX_ASSERT_GLOBAL_FIELD(parm11, parm11);
QCX_ASSERT_GLOBAL_FIELD(parm12, parm12);
QCX_ASSERT_GLOBAL_FIELD(parm13, parm13);
QCX_ASSERT_GLOBAL_FIELD(parm14, parm14);
QCX_ASSERT_GLOBAL_FIELD(parm15, parm15);
QCX_ASSERT_GLOBAL_FIELD(parm16, parm16);
QCX_ASSERT_GLOBAL_FIELD(v_forward, v_forward);
QCX_ASSERT_GLOBAL_FIELD(v_up, v_up);
QCX_ASSERT_GLOBAL_FIELD(v_right, v_right);
QCX_ASSERT_GLOBAL_FIELD(trace_allsolid, trace_allsolid);
QCX_ASSERT_GLOBAL_FIELD(trace_startsolid, trace_startsolid);
QCX_ASSERT_GLOBAL_FIELD(trace_fraction, trace_fraction);
QCX_ASSERT_GLOBAL_FIELD(trace_endpos, trace_endpos);
QCX_ASSERT_GLOBAL_FIELD(trace_plane_normal, trace_plane_normal);
QCX_ASSERT_GLOBAL_FIELD(trace_plane_dist, trace_plane_dist);
QCX_ASSERT_GLOBAL_FIELD(trace_ent, trace_ent);
QCX_ASSERT_GLOBAL_FIELD(trace_inopen, trace_inopen);
QCX_ASSERT_GLOBAL_FIELD(trace_inwater, trace_inwater);
QCX_ASSERT_GLOBAL_FIELD(msg_entity, msg_entity);

#undef QCX_ASSERT_GLOBAL_FIELD

#define QCX_ASSERT_FUNCTION_FIELD(field, index) \
	_Static_assert(offsetof(globalvars_t, field) \
			== offsetof(qcx_shared_global_state_v1_t, reserved_function_slots) \
				+ (index) * sizeof(((qcx_shared_global_state_v1_t *)0)->reserved_function_slots[0]) \
		&& sizeof(((globalvars_t *)0)->field) \
			== sizeof(((qcx_shared_global_state_v1_t *)0)->reserved_function_slots[0]), \
		"QCX global function slot differs from MVDSV: " #field)

QCX_ASSERT_FUNCTION_FIELD(main, 0);
QCX_ASSERT_FUNCTION_FIELD(StartFrame, 1);
QCX_ASSERT_FUNCTION_FIELD(PlayerPreThink, 2);
QCX_ASSERT_FUNCTION_FIELD(PlayerPostThink, 3);
QCX_ASSERT_FUNCTION_FIELD(ClientKill, 4);
QCX_ASSERT_FUNCTION_FIELD(ClientConnect, 5);
QCX_ASSERT_FUNCTION_FIELD(PutClientInServer, 6);
QCX_ASSERT_FUNCTION_FIELD(ClientDisconnect, 7);
QCX_ASSERT_FUNCTION_FIELD(SetNewParms, 8);
QCX_ASSERT_FUNCTION_FIELD(SetChangeParms, 9);

#undef QCX_ASSERT_FUNCTION_FIELD

#endif
