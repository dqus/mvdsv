/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

	
*/

#include "qwsvdef.h"
#include "nq_messages.h"
#if defined(QCX_TESTS)
#include "qcx/test_observer.h"
#endif

void NQ_Particle(const float origin[3], const float direction[3], float color, float count)
{
	int replacement = 0;
	int replacement_count = 0;
	/* Match the original PR1 float comparisons before SV_StartParticle's
	 * integer conversion. Other colours intentionally have no QW fallback. */
	if (count == 255) {
		replacement = TE_EXPLOSION;
	} else if (color == 73) {
		replacement = TE_BLOOD;
		replacement_count = 1;
	} else if (color == 225) {
		replacement = TE_LIGHTNINGBLOOD;
	}
	vec3_t org, dir;
	VectorCopy(origin, org);
	VectorCopy(direction, dir);
	SV_StartParticle(org, dir, color, count, replacement, replacement_count);
}

/* One server-owned incremental NQ stream, reset at map/physics boundaries.
 * Preserve the PR1 translator's reliable output and ignored destinations. */
enum { NQ_MSG_ONE = 1, NQ_MSG_INIT = 3 };

static byte nqp_buf_data[1024] /* must be large enough for svc_finale text */;
static sizebuf_t nqp_buf;
static qbool nqp_ignore_this_frame;
static int nqp_expect;

void NQ_ResetMessages(void)
{
	nqp_ignore_this_frame = false;
	nqp_expect = 0;
	SZ_Init (&nqp_buf, nqp_buf_data, sizeof(nqp_buf_data));
}

static void NQP_Flush (int count)
{
// FIXME, we make no distinction reliable or not
	assert (count <= nqp_buf.cursize);
	SZ_Write (&sv.reliable_datagram, nqp_buf_data, count);
	memmove (nqp_buf_data, nqp_buf_data + count, nqp_buf.cursize - count);
	nqp_buf.cursize -= count;
}

static void NQP_Skip (int count)
{
	assert (count <= nqp_buf.cursize);
	memmove (nqp_buf_data, nqp_buf_data + count, nqp_buf.cursize - count);
	nqp_buf.cursize -= count;
}

static void NQP_Process (void)
{
	int cmd;

	if (nqp_ignore_this_frame) {
		SZ_Clear (&nqp_buf);
		return;
	}

	while (1) {
		if (nqp_expect) {
			if (nqp_buf.cursize >= nqp_expect) {
				NQP_Flush (nqp_expect);
				nqp_expect = 0;
			}
			else
				break;
		}

		if (!nqp_buf.cursize)
			break;

		nqp_expect = 0;

		cmd = nqp_buf_data[0];
		if (cmd == svc_killedmonster || cmd == svc_foundsecret || cmd == svc_sellscreen)
			nqp_expect = 1;
		else if (cmd == svc_updatestat) {
			NQP_Skip (1);
			MSG_WriteByte (&sv.reliable_datagram, svc_updatestatlong);
			nqp_expect = 5;
		}
		else if (cmd == svc_print) {
			byte *p = (byte *)memchr (nqp_buf_data + 1, 0, nqp_buf.cursize - 1);
			if (!p)
				goto waitformore;
			MSG_WriteByte(&sv.reliable_datagram, svc_print);
			MSG_WriteByte(&sv.reliable_datagram, PRINT_HIGH);
			MSG_WriteString(&sv.reliable_datagram, (char *)(nqp_buf_data + 1));
			NQP_Skip(p - nqp_buf_data + 1);
		}
		else if (cmd == nq_svc_setview) {
			if (nqp_buf.cursize < 3)
				goto waitformore;
			NQP_Skip (3);		// TODO: make an extension for this
		}
		else if (cmd == svc_updatefrags)
			nqp_expect = 4;
		else if (cmd == nq_svc_updatecolors) {
			if (nqp_buf.cursize < 3)
				goto waitformore;
			MSG_WriteByte (&sv.reliable_datagram, svc_setinfo);
			MSG_WriteByte (&sv.reliable_datagram, nqp_buf_data[1]);
			MSG_WriteString (&sv.reliable_datagram, "topcolor");
			MSG_WriteString (&sv.reliable_datagram, va("%i", min(nqp_buf_data[2] & 15, 13)));
			MSG_WriteByte (&sv.reliable_datagram, svc_setinfo);
			MSG_WriteByte (&sv.reliable_datagram, nqp_buf_data[1]);
			MSG_WriteString (&sv.reliable_datagram, "bottomcolor");
			MSG_WriteString (&sv.reliable_datagram, va("%i", min((nqp_buf_data[2] >> 4)& 15, 13)));
			NQP_Skip (3);
		}
		else if (cmd == nq_svc_updatename) {
			int slot;
			byte *p;
			if (nqp_buf.cursize < 3)
				goto waitformore;
			slot = nqp_buf_data[1];
			p = (byte *)memchr (nqp_buf_data + 2, 0, nqp_buf.cursize - 2);
			if (!p)
				goto waitformore;
			MSG_WriteByte (&sv.reliable_datagram, svc_setinfo);
			MSG_WriteByte (&sv.reliable_datagram, slot);
			MSG_WriteString (&sv.reliable_datagram, "name");
			MSG_WriteString (&sv.reliable_datagram, (char *)nqp_buf_data + 2);
			MSG_WriteByte (&sv.reliable_datagram, svc_updateping);
			MSG_WriteByte (&sv.reliable_datagram, slot);
			MSG_WriteShort (&sv.reliable_datagram, 0);
			// We expect bots to set their name when they enter the game and never change it
			MSG_WriteByte (&sv.reliable_datagram, svc_updateentertime);
			MSG_WriteByte (&sv.reliable_datagram, slot);
			MSG_WriteFloat (&sv.reliable_datagram, 0);
			NQP_Skip ((p - nqp_buf_data) + 1);
		}
		else if (cmd == svc_cdtrack) {
			if (nqp_buf.cursize < 3)
				goto waitformore;
			NQP_Flush (2);
			NQP_Skip (1);
		}
		else if (cmd == svc_finale) {
			byte *p = (byte *)memchr (nqp_buf_data + 1, 0, nqp_buf.cursize - 1);
			if (!p)
				goto waitformore;
			nqp_expect = (p - nqp_buf_data) + 1;
		}
		else if (cmd == svc_intermission) {
#if defined(QCX_TESTS)
			QCX_TestObserverNqIntermission();
#endif
			int i;
			NQP_Flush (1);
			for (i = 0; i < 3; i++)
				MSG_WriteCoord (&sv.reliable_datagram, svs.clients[0].edict->v->origin[i]);
			for (i = 0; i < 3; i++)
				MSG_WriteAngle (&sv.reliable_datagram, svs.clients[0].edict->v->angles[i]);
		}
		else if (cmd == nq_svc_cutscene) {
			byte *p = (byte *)memchr (nqp_buf_data + 1, 0, nqp_buf.cursize - 1);
			if (!p)
				goto waitformore;
			MSG_WriteByte (&sv.reliable_datagram, svc_stufftext);
			MSG_WriteString (&sv.reliable_datagram, "//cutscene\n"); // ZQ extension
			NQP_Skip (p - nqp_buf_data + 1);
		}
		else if (nqp_buf_data[0] == svc_temp_entity) {
			if (nqp_buf.cursize < 2)
				break;

switch (nqp_buf_data[1]) {
  case TE_SPIKE:
  case TE_SUPERSPIKE:
  case TE_EXPLOSION:
  case TE_TAREXPLOSION:
  case TE_WIZSPIKE:
  case TE_KNIGHTSPIKE:
  case TE_LAVASPLASH:
  case TE_TELEPORT:
		nqp_expect = 8;
		break;
  case TE_GUNSHOT:
		if (nqp_buf.cursize < 8)
			goto waitformore;
		NQP_Flush (2);
		MSG_WriteByte (&sv.reliable_datagram, 1);
		NQP_Flush (6);
		break;

  case TE_LIGHTNING1:
  case TE_LIGHTNING2:
  case TE_LIGHTNING3:
		nqp_expect = 16;
	  break;
  case NQ_TE_BEAM:
  {		int entnum;
		if (nqp_buf.cursize < 16)
			goto waitformore;
		MSG_WriteByte (&sv.reliable_datagram, svc_temp_entity);
		MSG_WriteByte (&sv.reliable_datagram, TE_LIGHTNING1);
		entnum = nqp_buf_data[2] + nqp_buf_data[3]*256;
		if ((unsigned int)entnum > 1023)
			entnum = 0;
		MSG_WriteShort (&sv.reliable_datagram, (short)(entnum - 1288));
		NQP_Skip (4);
		nqp_expect = 12;
		break;
  }

  case NQ_TE_EXPLOSION2:
		if (nqp_buf.cursize < 10)
			goto waitformore;
		/* NQ type 12 is QW blood. Fall back to a plain explosion and
		 * consume both palette bytes before parsing the next message. */
		nqp_buf_data[1] = TE_EXPLOSION;
		NQP_Flush (8);
		NQP_Skip (2);
		break;
  default:
		Con_Printf ("WARNING: progs.dat sent an unsupported svc_temp_entity: %i\n", nqp_buf_data[1]);
	    goto ignore;
}

		}
		else {
			Con_Printf ("WARNING: progs.dat sent an unsupported svc: %i\n", cmd);
ignore:
			nqp_ignore_this_frame = true;
			break;
		}
	}
waitformore:;
}

void NQ_WriteByte(int destination, int value)
{
	if (destination == NQ_MSG_ONE || destination == NQ_MSG_INIT) {
		return;
	}
	MSG_WriteByte(&nqp_buf, value);
	NQP_Process();
}

/* Keep PR1's NQ WriteChar encoder, including its low-byte wire form. */
void NQ_WriteChar(int destination, int value)
{
	if (destination == NQ_MSG_ONE || destination == NQ_MSG_INIT) {
		return;
	}
	MSG_WriteByte(&nqp_buf, value);
	NQP_Process();
}

void NQ_WriteShort(int destination, int value)
{
	if (destination == NQ_MSG_ONE || destination == NQ_MSG_INIT) {
		return;
	}
	MSG_WriteShort(&nqp_buf, value);
	NQP_Process();
}

void NQ_WriteLong(int destination, int value)
{
	if (destination == NQ_MSG_ONE || destination == NQ_MSG_INIT) {
		return;
	}
	MSG_WriteLong(&nqp_buf, value);
	NQP_Process();
}

void NQ_WriteCoord(int destination, float value)
{
	if (destination == NQ_MSG_ONE || destination == NQ_MSG_INIT) {
		return;
	}
	MSG_WriteCoord(&nqp_buf, value);
	NQP_Process();
}

void NQ_WriteAngle(int destination, float value)
{
	if (destination == NQ_MSG_ONE || destination == NQ_MSG_INIT) {
		return;
	}
	MSG_WriteAngle(&nqp_buf, value);
	NQP_Process();
}

void NQ_WriteString(int destination, const char *value)
{
	if (destination == NQ_MSG_ONE || destination == NQ_MSG_INIT) {
		return;
	}
	MSG_WriteString(&nqp_buf, value);
	NQP_Process();
}

void NQ_WriteEntity(int destination, int value)
{
	if (destination == NQ_MSG_ONE || destination == NQ_MSG_INIT) {
		return;
	}
	MSG_WriteShort(&nqp_buf, value);
	NQP_Process();
}

