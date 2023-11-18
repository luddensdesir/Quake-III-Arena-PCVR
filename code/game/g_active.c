/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//

#include "g_local.h"


/*
===============
G_DamageFeedback

Called just before a snapshot is sent to the given player.
Totals up all damage and generates both the player_state_t
damage values to that client for pain blends and kicks, and
global pain sound events for all clients.
===============
*/
void P_DamageFeedback( gentity_t *player ) {
	gclient_t	*client;
	float	count;
	vec3_t	angles;

	client = player->client;
	if ( client->ps.pm_type == PM_DEAD ) {
		return;
	}

	// total points of damage shot at the player this frame
	count = client->damage_blood + client->damage_armor;
	if ( count == 0 ) {
		return;		// didn't take any damage
	}

	if ( count > 255 ) {
		count = 255;
	}

	// send the information to the client

	// world damage (falling, slime, etc) uses a special code
	// to make the blend blob centered instead of positional
	if ( client->damage_fromWorld ) {
		client->ps.damagePitch = 255;
		client->ps.damageYaw = 255;

		client->damage_fromWorld = qfalse;
	} else {
		vectoangles( client->damage_from, angles );
		client->ps.damagePitch = angles[PITCH]/360.0 * 256;
		client->ps.damageYaw = angles[YAW]/360.0 * 256;
	}

	// play an apropriate pain sound
	if ( (level.time > player->pain_debounce_time) && !(player->flags & FL_GODMODE) ) {
		player->pain_debounce_time = level.time + 700;
		G_AddEvent( player, EV_PAIN, player->health );
		client->ps.damageEvent++;
	}


	client->ps.damageCount = count;

	//
	// clear totals
	//
	client->damage_blood = 0;
	client->damage_armor = 0;
	client->damage_knockback = 0;
}



/*
=============
P_WorldEffects

Check for lava / slime contents and drowning
=============
*/
void P_WorldEffects( gentity_t *ent ) {
	qboolean	envirosuit;
	int			waterlevel;

	if ( ent->client->noclip ) {
		ent->client->airOutTime = level.time + 12000;	// don't need air
		return;
	}

	waterlevel = ent->waterlevel;

	envirosuit = ent->client->ps.powerups[PW_BATTLESUIT] > level.time;

	//
	// check for drowning
	//
	if ( waterlevel == 3 ) {
		// envirosuit give air
		if ( envirosuit ) {
			ent->client->airOutTime = level.time + 10000;
		}

		// if out of air, start drowning
		if ( ent->client->airOutTime < level.time) {
			// drown!
			ent->client->airOutTime += 1000;
			if ( ent->health > 0 ) {
				// take more damage the longer underwater
				ent->damage += 2;
				if (ent->damage > 15)
					ent->damage = 15;

				// play a gurp sound instead of a normal pain sound
				if (ent->health <= ent->damage) {
					G_Sound(ent, CHAN_VOICE, G_SoundIndex("*drown.wav"));
				} else if (rand()&1) {
					G_Sound(ent, CHAN_VOICE, G_SoundIndex("sound/player/gurp1.wav"));
				} else {
					G_Sound(ent, CHAN_VOICE, G_SoundIndex("sound/player/gurp2.wav"));
				}

				// don't play a normal pain sound
				ent->pain_debounce_time = level.time + 200;

				G_Damage (ent, NULL, NULL, NULL, NULL, 
					ent->damage, DAMAGE_NO_ARMOR, MOD_WATER);
			}
		}
	} else {
		ent->client->airOutTime = level.time + 12000;
		ent->damage = 2;
	}

	//
	// check for sizzle damage (move to pmove?)
	//
	if (waterlevel && 
		(ent->watertype&(CONTENTS_LAVA|CONTENTS_SLIME)) ) {
		if (ent->health > 0
			&& ent->pain_debounce_time <= level.time	) {

			if ( envirosuit ) {
				G_AddEvent( ent, EV_POWERUP_BATTLESUIT, 0 );
			} else {
				if (ent->watertype & CONTENTS_LAVA) {
					G_Damage (ent, NULL, NULL, NULL, NULL, 
						30*waterlevel, 0, MOD_LAVA);
				}

				if (ent->watertype & CONTENTS_SLIME) {
					G_Damage (ent, NULL, NULL, NULL, NULL, 
						10*waterlevel, 0, MOD_SLIME);
				}
			}
		}
	}
}



/*
===============
G_SetClientSound
===============
*/
void G_SetClientSound( gentity_t *ent ) {
#ifdef MISSIONPACK
	if( ent->s.eFlags & EF_TICKING ) {
		ent->client->ps.loopSound = G_SoundIndex( "sound/weapons/proxmine/wstbtick.wav");
	}
	else
#endif
	if (ent->waterlevel && (ent->watertype&(CONTENTS_LAVA|CONTENTS_SLIME)) ) {
		ent->client->ps.loopSound = level.snd_fry;
	} else {
		ent->client->ps.loopSound = 0;
	}
}



//==============================================================

/*
==============
ClientImpacts
==============
*/
void ClientImpacts( gentity_t *ent, pmove_t *pm ) {
	int		i, j;
	trace_t	trace;
	gentity_t	*other;

	memset( &trace, 0, sizeof( trace ) );
	for (i=0 ; i<pm->numtouch ; i++) {
		for (j=0 ; j<i ; j++) {
			if (pm->touchents[j] == pm->touchents[i] ) {
				break;
			}
		}
		if (j != i) {
			continue;	// duplicated
		}
		other = &g_entities[ pm->touchents[i] ];

		if ( ( ent->r.svFlags & SVF_BOT ) && ( ent->touch ) ) {
			ent->touch( ent, other, &trace );
		}

		if ( !other->touch ) {
			continue;
		}

		other->touch( other, ent, &trace );
	}

}

/*
============
G_TouchTriggers

Find all trigger entities that ent's current position touches.
Spectators will only interact with teleporters.
============
*/
void	G_TouchTriggers( gentity_t *ent ) {
	int			i, num;
	int			touch[MAX_GENTITIES];
	gentity_t	*hit;
	trace_t		trace;
	vec3_t		mins, maxs;
	static vec3_t	range = { 40, 40, 52 };

	if ( !ent->client ) {
		return;
	}

	// dead clients don't activate triggers!
	if ( ent->client->ps.stats[STAT_HEALTH] <= 0 ) {
		return;
	}

	VectorSubtract( ent->client->ps.origin, range, mins );
	VectorAdd( ent->client->ps.origin, range, maxs );

	num = trap_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );

	// can't use ent->absmin, because that has a one unit pad
	VectorAdd( ent->client->ps.origin, ent->r.mins, mins );
	VectorAdd( ent->client->ps.origin, ent->r.maxs, maxs );

	for ( i=0 ; i<num ; i++ ) {
		hit = &g_entities[touch[i]];

		if ( !hit->touch && !ent->touch ) {
			continue;
		}
		if ( !( hit->r.contents & CONTENTS_TRIGGER ) ) {
			continue;
		}

		// ignore most entities if a spectator
		if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			if ( hit->s.eType != ET_TELEPORT_TRIGGER &&
				// this is ugly but adding a new ET_? type will
				// most likely cause network incompatibilities
				hit->touch != Touch_DoorTrigger) {
				continue;
			}
		}

		// use seperate code for determining if an item is picked up
		// so you don't have to actually contact its bounding box
		if ( hit->s.eType == ET_ITEM ) {
			if ( !BG_PlayerTouchesItem( &ent->client->ps, &hit->s, level.time ) ) {
				continue;
			}
		} else {
			if ( !trap_EntityContact( mins, maxs, hit ) ) {
				continue;
			}
		}

		memset( &trace, 0, sizeof(trace) );

		if ( hit->touch ) {
			hit->touch (hit, ent, &trace);
		}

		if ( ( ent->r.svFlags & SVF_BOT ) && ( ent->touch ) ) {
			ent->touch( ent, hit, &trace );
		}
	}

	// if we didn't touch a jump pad this pmove frame
	if ( ent->client->ps.jumppad_frame != ent->client->ps.pmove_framecount ) {
		ent->client->ps.jumppad_frame = 0;
		ent->client->ps.jumppad_ent = 0;
	}
}

/*
=================
SpectatorThink
=================
*/
void SpectatorThink( gentity_t *ent, usercmd_t *ucmd ) {
	pmove_t	pm;
	gclient_t	*client;

	client = ent->client;

	if ( client->sess.spectatorState != SPECTATOR_FOLLOW ) {
		client->ps.pm_type = PM_SPECTATOR;
		client->ps.speed = 400;	// faster than normal

		// set up for pmove
		memset (&pm, 0, sizeof(pm));
		pm.ps = &client->ps;
		pm.cmd = *ucmd;
		pm.tracemask = MASK_PLAYERSOLID & ~CONTENTS_BODY;	// spectators can fly through bodies
		pm.trace = trap_Trace;
		pm.pointcontents = trap_PointContents;

		// perform a pmove
		Pmove (&pm);
		// save results of pmove
		VectorCopy( client->ps.origin, ent->s.origin );

		G_TouchTriggers( ent );
		trap_UnlinkEntity( ent );
	}

	client->oldbuttons = client->buttons;
	client->buttons = ucmd->buttons;

	// attack button cycles through spectators
	if ( ( client->buttons & BUTTON_ATTACK ) && ! ( client->oldbuttons & BUTTON_ATTACK ) ) {
		Cmd_FollowCycle_f( ent, 1 );
	}
}



/*
=================
ClientInactivityTimer

Returns qfalse if the client is dropped
=================
*/
qboolean ClientInactivityTimer( gclient_t *client ) {
	if ( ! g_inactivity.integer ) {
		// give everyone some time, so if the operator sets g_inactivity during
		// gameplay, everyone isn't kicked
		client->inactivityTime = level.time + 60 * 1000;
		client->inactivityWarning = qfalse;
	} else if ( client->pers.cmd.forwardmove || 
		client->pers.cmd.rightmove || 
		client->pers.cmd.upmove ||
		(client->pers.cmd.buttons & BUTTON_ATTACK) ) {
		client->inactivityTime = level.time + g_inactivity.integer * 1000;
		client->inactivityWarning = qfalse;
	} else if ( !client->pers.localClient ) {
		if ( level.time > client->inactivityTime ) {
			trap_DropClient( client - level.clients, "Dropped due to inactivity" );
			return qfalse;
		}
		if ( level.time > client->inactivityTime - 10000 && !client->inactivityWarning ) {
			client->inactivityWarning = qtrue;
			trap_SendServerCommand( client - level.clients, "cp \"Ten seconds until inactivity drop!\n\"" );
		}
	}
	return qtrue;
}

/*
==================
ClientTimerActions

Actions that happen once a second
==================
*/
void ClientTimerActions( gentity_t *ent, int msec ) {
	gclient_t	*client;
#ifdef MISSIONPACK
	int			maxHealth;
#endif

	client = ent->client;
	client->timeResidual += msec;

	while ( client->timeResidual >= 1000 ) {
		client->timeResidual -= 1000;

		// regenerate
#ifdef MISSIONPACK
		if( bg_itemlist[client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_GUARD ) {
			maxHealth = client->ps.stats[STAT_MAX_HEALTH] / 2;
		}
		else if ( client->ps.powerups[PW_REGEN] ) {
			maxHealth = client->ps.stats[STAT_MAX_HEALTH];
		}
		else {
			maxHealth = 0;
		}
		if( maxHealth ) {
			if ( ent->health < maxHealth ) {
				ent->health += 15;
				if ( ent->health > maxHealth * 1.1 ) {
					ent->health = maxHealth * 1.1;
				}
				G_AddEvent( ent, EV_POWERUP_REGEN, 0 );
			} else if ( ent->health < maxHealth * 2) {
				ent->health += 5;
				if ( ent->health > maxHealth * 2 ) {
					ent->health = maxHealth * 2;
				}
				G_AddEvent( ent, EV_POWERUP_REGEN, 0 );
			}
#else
		if ( client->ps.powerups[PW_REGEN] ) {
			if ( ent->health < client->ps.stats[STAT_MAX_HEALTH]) {
				ent->health += 15;
				if ( ent->health > client->ps.stats[STAT_MAX_HEALTH] * 1.1 ) {
					ent->health = client->ps.stats[STAT_MAX_HEALTH] * 1.1;
				}
				G_AddEvent( ent, EV_POWERUP_REGEN, 0 );
			} else if ( ent->health < client->ps.stats[STAT_MAX_HEALTH] * 2) {
				ent->health += 5;
				if ( ent->health > client->ps.stats[STAT_MAX_HEALTH] * 2 ) {
					ent->health = client->ps.stats[STAT_MAX_HEALTH] * 2;
				}
				G_AddEvent( ent, EV_POWERUP_REGEN, 0 );
			}
#endif
		} else {
			// count down health when over max
			if ( ent->health > client->ps.stats[STAT_MAX_HEALTH] ) {
				ent->health--;
			}
		}

		// count down armor when over max
		if ( client->ps.stats[STAT_ARMOR] > client->ps.stats[STAT_MAX_HEALTH] ) {
			client->ps.stats[STAT_ARMOR]--;
		}
	}
#ifdef MISSIONPACK
	if( bg_itemlist[client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_AMMOREGEN ) {
		int w, max, inc, t, i;
    int weapList[]={WP_MACHINEGUN,WP_SHOTGUN,WP_GRENADE_LAUNCHER,WP_ROCKET_LAUNCHER,WP_LIGHTNING,WP_RAILGUN,WP_PLASMAGUN,WP_BFG,WP_NAILGUN,WP_PROX_LAUNCHER,WP_CHAINGUN};
    int weapCount = sizeof(weapList) / sizeof(int);
		//
    for (i = 0; i < weapCount; i++) {
		  w = weapList[i];

		  switch(w) {
			  case WP_MACHINEGUN: max = 50; inc = 4; t = 1000; break;
			  case WP_SHOTGUN: max = 10; inc = 1; t = 1500; break;
			  case WP_GRENADE_LAUNCHER: max = 10; inc = 1; t = 2000; break;
			  case WP_ROCKET_LAUNCHER: max = 10; inc = 1; t = 1750; break;
			  case WP_LIGHTNING: max = 50; inc = 5; t = 1500; break;
			  case WP_RAILGUN: max = 10; inc = 1; t = 1750; break;
			  case WP_PLASMAGUN: max = 50; inc = 5; t = 1500; break;
			  case WP_BFG: max = 10; inc = 1; t = 4000; break;
			  case WP_NAILGUN: max = 10; inc = 1; t = 1250; break;
			  case WP_PROX_LAUNCHER: max = 5; inc = 1; t = 2000; break;
			  case WP_CHAINGUN: max = 100; inc = 5; t = 1000; break;
			  default: max = 0; inc = 0; t = 1000; break;
		  }
		  client->ammoTimes[w] += msec;
		  if ( client->ps.ammo[w] >= max ) {
			  client->ammoTimes[w] = 0;
		  }
		  if ( client->ammoTimes[w] >= t ) {
			  while ( client->ammoTimes[w] >= t )
				  client->ammoTimes[w] -= t;
			  client->ps.ammo[w] += inc;
			  if ( client->ps.ammo[w] > max ) {
				  client->ps.ammo[w] = max;
			  }
		  }
    }
	}
#endif
}

/*
====================
ClientIntermissionThink
====================
*/
void ClientIntermissionThink( gclient_t *client ) {
	client->ps.eFlags &= ~EF_TALK;
	client->ps.eFlags &= ~EF_FIRING;

	// the level will exit when everyone wants to or after timeouts

	// swap and latch button actions
	client->oldbuttons = client->buttons;
	client->buttons = client->pers.cmd.buttons;
	if ( client->buttons & ( BUTTON_ATTACK | BUTTON_USE_HOLDABLE ) & ( client->oldbuttons ^ client->buttons ) ) {
		// this used to be an ^1 but once a player says ready, it should stick
		client->readyToExit = 1;
	}
}


/*
================
ClientEvents

Events will be passed on to the clients for presentation,
but any server game effects are handled here
================
*/
void ClientEvents( gentity_t *ent, int oldEventSequence ) {
	int		i, j;
	int		event;
	gclient_t *client;
	int		damage;
	vec3_t	dir;
	vec3_t	origin, angles;
//	qboolean	fired;
	gitem_t *item;
	gentity_t *drop;

	client = ent->client;

	if ( oldEventSequence < client->ps.eventSequence - MAX_PS_EVENTS ) {
		oldEventSequence = client->ps.eventSequence - MAX_PS_EVENTS;

/*
==============
JUHOX: MoveRopeElement

derived from PM_SlideMove() [bg_slidemove.c]
returns qfalse if element is in solid
==============
*/
#if GRAPPLE_ROPE
#define	MAX_CLIP_PLANES	5
#include "bg_local.h"
static qboolean MoveRopeElement(const vec3_t start, const vec3_t idealpos, vec3_t realpos, qboolean* touch) {
	vec3_t		velocity;
	static vec3_t ropeMins = {-ROPE_ELEMENT_SIZE, -ROPE_ELEMENT_SIZE, -ROPE_ELEMENT_SIZE};
	static vec3_t ropeMaxs = {ROPE_ELEMENT_SIZE, ROPE_ELEMENT_SIZE, ROPE_ELEMENT_SIZE};

	int			bumpcount, numbumps;
	vec3_t		dir;
	float		d;
	int			numplanes;
	vec3_t		planes[MAX_CLIP_PLANES];
	vec3_t		clipVelocity;
	int			i, j, k;
	trace_t	trace;
	vec3_t		end;
	float		time_left;
	float		into;



	VectorSubtract(idealpos, start, velocity);
	VectorCopy(start, realpos);
	*touch = qfalse;
	
	numbumps = MAX_CLIP_PLANES - 1;

	time_left = 1.0;	// seconds

	numplanes = 0;

	// never turn against original velocity
	if (VectorNormalize2(velocity, planes[numplanes]) < 1) return qtrue;
	numplanes++;

	for (bumpcount=0; bumpcount < numbumps; bumpcount++) {

		// calculate position we are trying to move to
		VectorMA(realpos, time_left, velocity, end);

		// see if we can make it there
		trap_Trace(&trace, realpos, ropeMins, ropeMaxs, end, -1, CONTENTS_SOLID);

		if (trace.allsolid) {
			if (time_left >= 1.0) return qfalse;
			SnapVectorTowards(realpos, start);
			return qtrue;
		}

		if (trace.fraction > 0) {
			// actually covered some distance
			VectorCopy(trace.endpos, realpos);
		}

		//if (trace.fraction >= 1) return qtrue;
		// check if we can get back!
		if (trace.fraction >= 1) {
			trace_t trace2;

			trap_Trace(&trace2, end, ropeMins, ropeMaxs, realpos, -1, CONTENTS_SOLID);
			if (trace2.fraction >= 1) return qtrue;
			if (trace.allsolid) {
				if (time_left >= 1.0) return qfalse;
				SnapVectorTowards(realpos, start);
				return qtrue;
			}
		}

		*touch = qtrue;

		time_left -= time_left * trace.fraction;

		if (numplanes >= MAX_CLIP_PLANES) {
			// this shouldn't really happen
			return qtrue;
		}

		//
		// if this is the same plane we hit before, nudge velocity
		// out along it, which fixes some epsilon issues with
		// non-axial planes
		//
		for (i = 0; i < numplanes; i++) {
			if (DotProduct(trace.plane.normal, planes[i]) > 0.99) {
				VectorAdd(trace.plane.normal, velocity, velocity);
				break;
			}
		}
		if (i < numplanes) {
			continue;
		}
		VectorCopy(trace.plane.normal, planes[numplanes]);
		numplanes++;

		//
		// modify velocity so it parallels all of the clip planes
		//

		// find a plane that it enters
		for (i = 0; i < numplanes; i++) {
			into = DotProduct(velocity, planes[i]);
			if (into >= 0.1) {
				continue;		// move doesn't interact with the plane
			}

			// see how hard we are hitting things
			/*
			if ( -into > pml.impactSpeed ) {
				pml.impactSpeed = -into;
			}
			*/

			// slide along the plane
			PM_ClipVelocity(velocity, planes[i], clipVelocity, OVERCLIP);

			// see if there is a second plane that the new move enters
			for (j = 0; j < numplanes; j++) {
				if (j == i) {
					continue;
				}
				if (DotProduct(clipVelocity, planes[j]) >= 0.1) {
					continue;		// move doesn't interact with the plane
				}

				// try clipping the move to the plane
				PM_ClipVelocity(clipVelocity, planes[j], clipVelocity, OVERCLIP);

				// see if it goes back into the first clip plane
				if (DotProduct(clipVelocity, planes[i]) >= 0) {
					continue;
				}

				// slide the original velocity along the crease
				CrossProduct(planes[i], planes[j], dir);
				VectorNormalize(dir);
				d = DotProduct(dir, velocity);
				VectorScale(dir, d, clipVelocity);

				// see if there is a third plane the the new move enters
				for (k = 0; k < numplanes; k++) {
					if (k == i || k == j) {
						continue;
					}
					if (DotProduct(clipVelocity, planes[k]) >= 0.1) {
						continue;		// move doesn't interact with the plane
					}

					// stop dead at a tripple plane interaction
					return qtrue;
				}
			}

			// if we have fixed all interactions, try another move
			VectorCopy(clipVelocity, velocity);
			break;
		}
	}

	return qtrue;
}
#endif

/*
==============
JUHOX: ThinkRopeElement

returns qfalse if element is in solid
==============
*/
#if GRAPPLE_ROPE
static ropeElement_t tempRope[MAX_ROPE_ELEMENTS];
static qboolean ThinkRopeElement(gclient_t* client, int ropeElement, int phase, float dt) {
	const ropeElement_t* srcRope;
	const ropeElement_t* srcRE;
	ropeElement_t* dstRE;
	vec3_t startPos;
	vec3_t predPos;
	vec3_t succPos;
	vec3_t anchorPos;
	vec3_t velocity;
	float dist;
	float f;
	vec3_t dir;
	vec3_t idealpos;
	vec3_t realpos;
	float errSqr;

	switch (phase) {
	case 0:
		srcRope = client->ropeElements;
		dstRE = &tempRope[ropeElement];
		break;
	case 1:
		srcRope = tempRope;
		dstRE = &client->ropeElements[ropeElement];
		break;
	default:
		return qfalse;
	}
	srcRE = &srcRope[ropeElement];

	VectorCopy(client->ropeElements[ropeElement].pos, startPos);

	if (ropeElement > 0) {
		VectorCopy(srcRope[ropeElement-1].pos, predPos);
		VectorCopy(client->ropeElements[ropeElement-1].pos, anchorPos);
	}
	else {
		VectorCopy(client->hook->r.currentOrigin, predPos);
		VectorCopy(predPos, anchorPos);
	}

	if (ropeElement < client->numRopeElements-1) {
		VectorCopy(srcRope[ropeElement+1].pos, succPos);
	}
	else {
		VectorCopy(client->ps.origin, succPos);
	}

	VectorCopy(srcRE->velocity, velocity);
	
	velocity[2] -= 0.5 * g_gravity.value * dt;
	if (!srcRE->touch) {
		velocity[0] += 0.05 * dt * crandom();
		velocity[1] += 0.05 * dt * crandom();
		velocity[2] += 0.05 * dt * crandom();
	}

	VectorSubtract(succPos, srcRE->pos, dir);
	dist = VectorLength(dir);
	if (dist > 1.5 * ROPE_ELEMENT_SIZE) {
		f = 4.0;
	}
	else if (dist > ROPE_ELEMENT_SIZE) {
		f = 2.0;
	}
	else {
		f = 0.1;
	}
	VectorMA(velocity, f, dir, velocity);

	VectorSubtract(predPos, srcRE->pos, dir);
	dist = VectorLength(dir);
	if (dist > 1.5 * ROPE_ELEMENT_SIZE) {
		f = 4.0;
	}
	else if (dist > ROPE_ELEMENT_SIZE) {
		f = 2.0;
	}
	else {
		f = 0.1;
	}
	VectorMA(velocity, f, dir, velocity);
	
	VectorScale(velocity, 0.9, velocity);

	VectorCopy(velocity, dstRE->velocity);

	VectorMA(srcRE->pos, dt, velocity, idealpos);

	{
		vec3_t v;
		vec3_t w;
		float d;

		VectorSubtract(succPos, predPos, v);
		VectorSubtract(idealpos, predPos, w);
		f = VectorNormalize(v);
		d = DotProduct(v, w);
		/*
		if (d < 0) {
			VectorMA(idealpos, -d, v, idealpos);
		}
		else if (d > f) {
			VectorMA(idealpos, f - d, v, idealpos);
		}
		*/
		if (d < 0) {
			VectorCopy(predPos, idealpos);
		}
		else if (d > f) {
			VectorCopy(succPos, idealpos);
		}
	}

	if (phase == 1) {
		VectorSubtract(idealpos, anchorPos, dir);
		dist = VectorLength(dir);
		if (dist > 1.5 * ROPE_ELEMENT_SIZE) {
			VectorMA(anchorPos, 1.5 * ROPE_ELEMENT_SIZE / dist, dir, idealpos);
		}
	}

	/*
	if (!MoveRopeElement(startPos, idealpos, realpos, &dstRE->touch)) {
		return qfalse;
	}
	*/
	switch (phase) {
	case 0:
		VectorCopy(idealpos, dstRE->pos);
		return qtrue;
	case 1:
		if (!MoveRopeElement(startPos, idealpos, realpos, &dstRE->touch)) {
			return qfalse;
		}
		break;
	}

	/*
	if (re->touch) {
		VectorScale(re->velocity, 0.7, re->velocity);
	}
	*/

	errSqr = DistanceSquared(idealpos, realpos);
	if (errSqr > 0.1) {
		vec3_t realpos2;
		qboolean touch;

		startPos[2] += ROPE_ELEMENT_SIZE;
		if (MoveRopeElement(startPos, idealpos, realpos2, &touch)) {
			if (DistanceSquared(idealpos, realpos2) < errSqr) {
				dstRE->touch = touch;
				VectorCopy(realpos2, realpos);
			}
		}
	}

	VectorCopy(realpos, dstRE->pos);
	return qtrue;
}
#endif

/*
==============
JUHOX: IsRopeSegmentTaut
==============
*/
/*
#if GRAPPLE_ROPE
static qboolean IsRopeSegmentTaut(const vec3_t start, const vec3_t end, int numSections) {
	return Distance(start, end) / numSections > ROPE_ELEMENT_SIZE;
}
#endif
*/

/*
==============
JUHOX: IsRopeTaut
==============
*/
/*
#if GRAPPLE_ROPE
static qboolean IsRopeTaut(gentity_t* ent) {
	gclient_t* client;
	int i;
	vec3_t start;
	int n;

	client = ent->client;
	if (client->hook->s.eType != ET_GRAPPLE) return qfalse;

	VectorCopy(client->hook->r.currentOrigin, start);
	n = 0;
	for (i = 0; i < client->numRopeElements; i++) {
		ropeElement_t* re;

		re = &client->ropeElements[i];
		n++;
		if (!re->touch) continue;

		if (!IsRopeSegmentTaut(start, re->pos, n)) return qfalse;

		VectorCopy(re->pos, start);
		n = 0;
	}
	n++;
	return IsRopeSegmentTaut(start, client->ps.origin, n);
}
#endif
*/
#if GRAPPLE_ROPE
static qboolean IsRopeTaut(gentity_t* ent, qboolean wasTaut) {
	gclient_t* client;
	int i;
	int n;
	vec3_t dir;
	float dirLengthSqr;
	float dirLength;
	float treshold;

	client = ent->client;
	if (client->hook->s.eType != ET_GRAPPLE) return qfalse;
	
	/*
	i = 0;
	n = client->numRopeElements;
	while (n > 0) {
		int j;
		int m;
		trace_t trace;

		m = n >> 1;
		j = i + m;

		trap_Trace(&trace, client->ps.origin, NULL, NULL, client->ropeElements[j].pos, -1, CONTENTS_SOLID);

		if (trace.fraction < 1.0) {
			i = j + 1;
			n -= m + 1;
		}
		else {
			n = m;
		}
	}
	*/
	for (i = client->numRopeElements-1; i >= 0; i--) {
		trace_t trace;

		if (client->ropeElements[i].touch) break;

		trap_Trace(&trace, client->ps.origin, NULL, NULL, client->ropeElements[i].pos, -1, CONTENTS_SOLID);

		if (trace.fraction < 1.0) break;
	}
	i++;

	if (i >= client->numRopeElements) return qtrue;
	/*
	return IsRopeSegmentTaut(client->ropeElements[i].pos, client->ps.origin, client->numRopeElements - i);
	*/
	VectorSubtract(client->ropeElements[i].pos, client->ps.origin, dir);
	dirLengthSqr = VectorLengthSquared(dir);
	dirLength = sqrt(dirLengthSqr);
	treshold = (wasTaut? 0.2 : 0.1) * dirLength;
	n = i;
	for (++i; i < client->numRopeElements; i++) {
		float k;
		vec3_t pos;
		vec3_t dir2;
		vec3_t plummet;
		
		VectorCopy(client->ropeElements[i].pos, pos);
		VectorSubtract(pos, client->ps.origin, dir2);
		k = DotProduct(dir, dir2) / dirLengthSqr;
		if (k < 0 || k > 1) return qfalse;
		VectorMA(client->ps.origin, k, dir, plummet);
		if (Distance(plummet, pos) > treshold) return qfalse;
	}
	return qtrue;
}
#endif

/*
==============
JUHOX: NextTouchedRopeElement
==============
*/
#if GRAPPLE_ROPE
static int NextTouchedRopeElement(gclient_t* client, int index, vec3_t pos) {
	if (index < 0) {
		VectorCopy(client->ps.origin, pos);
		return -1;
	}

	while (index < client->numRopeElements) {
		if (client->ropeElements[index].touch) break;
		index++;
	}

	if (index >= client->numRopeElements) {
		VectorCopy(client->ps.origin, pos);
		index = -1;
	}
	else {
		VectorCopy(client->ropeElements[index].pos, pos);
	}
	return index;
}
#endif

/*
==============
JUHOX: TautRopePos

called with index=-1 to init
==============
*/
#if GRAPPLE_ROPE
static void TautRopePos(gclient_t* client, int index, vec3_t pos) {
	static float distCovered;
	static float totalDist;
	static vec3_t startPos;
	static vec3_t dir;
	static int destIndex;

	if (index < 0) {
		vec3_t dest;

		distCovered = 0;
		VectorCopy(client->hook->r.currentOrigin, startPos);
		destIndex = NextTouchedRopeElement(client, 0, dest);
		VectorSubtract(dest, startPos, dir);
		totalDist = VectorNormalize(dir);
		return;
	}

	distCovered += 1.5 * ROPE_ELEMENT_SIZE;

	CheckDist:
	if (distCovered > totalDist) {
		distCovered -= totalDist;
		if (destIndex < 0) {
			VectorCopy(client->ps.origin, startPos);
			VectorClear(dir);
			totalDist = 1000000.0;
		}
		else {
			vec3_t dest;

			VectorCopy(client->ropeElements[destIndex].pos, startPos);
			destIndex = NextTouchedRopeElement(client, destIndex+1, dest);
			VectorSubtract(dest, startPos, dir);
			totalDist = VectorNormalize(dir);
			goto CheckDist;
		}
	}
	VectorMA(startPos, distCovered, dir, pos);
}
#endif

/*
==============
JUHOX: CreateGrappleRope
==============
*/
#if GRAPPLE_ROPE
static void CreateGrappleRope(gentity_t* ent) {
	gclient_t* client;
	int i;

	client = ent->client;

	for (i = 0; i < client->numRopeElements; i++) {
		gentity_t* ropeEntity;
		vec3_t pos;

		ropeEntity = client->ropeEntities[i / 8];
		if (!ropeEntity) {
			ropeEntity = G_Spawn();
			if (!ropeEntity) break;
			client->ropeEntities[i / 8] = ropeEntity;

			ropeEntity->s.eType = ET_GRAPPLE_ROPE;
			ropeEntity->classname = "grapple rope element";
			ropeEntity->r.svFlags = SVF_USE_CURRENT_ORIGIN;
		}
		ropeEntity->s.time = 0;

		VectorCopy(client->ropeElements[i].pos, pos);

		switch (i & 7) {
		case 0:
			G_SetOrigin(ropeEntity, pos);
			trap_LinkEntity(ropeEntity);
			break;
		case 1:
			VectorCopy(pos, ropeEntity->s.pos.trDelta);
			break;
		case 2:
			VectorCopy(pos, ropeEntity->s.apos.trBase);
			break;
		case 3:
			VectorCopy(pos, ropeEntity->s.apos.trDelta);
			break;
		case 4:
			VectorCopy(pos, ropeEntity->s.origin);
			break;
		case 5:
			VectorCopy(pos, ropeEntity->s.origin2);
			break;
		case 6:
			VectorCopy(pos, ropeEntity->s.angles);
			break;
		case 7:
			VectorCopy(pos, ropeEntity->s.angles2);
			break;
		}
		ropeEntity->s.modelindex = (i & 7) + 1;
	}

	// delete unused rope entities
	for (i = (i+7) / 8; i < MAX_ROPE_ELEMENTS / 8; i++) {
		if (!client->ropeEntities[i]) continue;

		G_FreeEntity(client->ropeEntities[i]);
		client->ropeEntities[i] = NULL;
	}

	// chain the rope entities together
	for (i = 0; i < MAX_ROPE_ELEMENTS / 8; i++) {
		if (!client->ropeEntities[i]) continue;

		if (i <= 0) {
			client->ropeEntities[i]->s.otherEntityNum = client->hook->s.number;
		}
		else if (client->ropeEntities[i - 1]) {
			//client->ropeEntities[i - 1]->s.otherEntityNum2 = client->ropeEntities[i]->s.number;
			client->ropeEntities[i]->s.otherEntityNum = client->ropeEntities[i - 1]->s.number;
		}
		else {
			client->ropeEntities[i]->s.otherEntityNum = ENTITYNUM_NONE;
		}

		if (i >= MAX_ROPE_ELEMENTS / 8 - 1) {
			client->ropeEntities[i]->s.otherEntityNum2 = ent->s.number;
		}
		else {
			client->ropeEntities[i]->s.otherEntityNum2 = ENTITYNUM_NONE;
		}
	}
}
#endif

/*
==============
JUHOX: InsertRopeElement
==============
*/
#if GRAPPLE_ROPE
static qboolean InsertRopeElement(gclient_t* client, int index, const vec3_t pos) {
	int i;
	vec3_t predPos;
	vec3_t predVel;
	vec3_t succPos;
	ropeElement_t* re;

	if (client->numRopeElements >= MAX_ROPE_ELEMENTS) return qfalse;

	for (i = client->numRopeElements-1; i >= index; i--) {
		client->ropeElements[i+1] = client->ropeElements[i];
	}
	client->numRopeElements++;

	if (index > 0) {
		VectorCopy(client->ropeElements[index-1].pos, predPos);
		VectorCopy(client->ropeElements[index-1].velocity, predVel);
	}
	else {
		VectorCopy(client->hook->r.currentOrigin, predPos);
		BG_EvaluateTrajectoryDelta(&client->hook->s.pos, level.time, predVel);
	}

	if (index < client->numRopeElements-1) {
		VectorCopy(client->ropeElements[index+1].pos, succPos);
	}
	else {
		VectorCopy(client->ps.origin, succPos);
	}

	re = &client->ropeElements[index];

	if (DistanceSquared(pos, predPos) < DistanceSquared(pos, succPos)) {
		if (!MoveRopeElement(predPos, pos, re->pos, &re->touch)) return qfalse;
	}
	else {
		if (!MoveRopeElement(succPos, pos, re->pos, &re->touch)) return qfalse;
	}
	VectorCopy(predVel, re->velocity);
	return qtrue;
}
#endif

/*
==============
JUHOX: ThinkGrapple
==============
*/
#if GRAPPLE_ROPE
static void ThinkGrapple(gentity_t* ent, int msec) {
	float dt;
	gclient_t* client;
	int i;
	int n;
	vec3_t pullpoint;
	vec3_t start;
	vec3_t dir;
	float dist;
	qboolean autoCut;
	float pullSpeed;

	if (g_grapple.integer <= HM_disabled || g_grapple.integer >= HM_num_modes) return;

	client = ent->client;

	if (g_grapple.integer == HM_classic) {
		if (
			client->ps.weapon == WP_GRAPPLING_HOOK &&
			!client->offHandHook &&
			client->hook &&
			!(client->pers.cmd.buttons & BUTTON_ATTACK)
		) {
			Weapon_HookFree(client->hook);
			return;
		}

		if (!client->hook) return;

		if (client->hook->s.eType != ET_GRAPPLE) {
			client->ps.stats[STAT_GRAPPLE_STATE] = GST_windoff;
		}
		else if (
			VectorLengthSquared(client->ps.velocity) > 160*160 &&
			(client->ps.pm_flags & PMF_TIME_KNOCKBACK) == 0
		) {
			client->ps.stats[STAT_GRAPPLE_STATE] = GST_pulling;
		}
		else {
			client->ps.stats[STAT_GRAPPLE_STATE] = GST_silent;
		}
		return;
	}

	if (
		client->hook &&
		client->pers.cmd.upmove < 0 &&
		client->pers.crouchingCutsRope
	) {
		Weapon_HookFree(client->hook);
		return;
	}

	client->ps.pm_flags &= ~PMF_GRAPPLE_PULL;
	client->ps.stats[STAT_GRAPPLE_STATE] = GST_unused;
	if (!client->hook) return;

	switch (g_grapple.integer) {
	case HM_tool:
	default:
		autoCut = qtrue;
		pullSpeed = GRAPPLE_PULL_SPEED_TOOL;
		break;
	case HM_anchor:
		autoCut = qfalse;
		pullSpeed = GRAPPLE_PULL_SPEED_ANCHOR;
		break;
	case HM_combat:
		autoCut = qfalse;
		pullSpeed = GRAPPLE_PULL_SPEED_COMBAT;
		break;
	}

	if (
		client->hook->s.eType == ET_GRAPPLE &&
		(
			client->numRopeElements <= 0 ||
			DistanceSquared(client->ps.origin, client->hook->r.currentOrigin) < 40*40
		)
	) {
		client->numRopeElements = 0;	// no rope explosion
		if (autoCut) {
			Weapon_HookFree(client->hook);
			return;
		}
		else if (
			VectorLengthSquared(client->ps.velocity) > 160*160 &&
			(client->ps.pm_flags & PMF_TIME_KNOCKBACK) == 0
		) {
			client->ps.stats[STAT_GRAPPLE_STATE] = GST_pulling;
		}
		else {
			client->ps.stats[STAT_GRAPPLE_STATE] = GST_silent;
		}
		VectorCopy(client->hook->r.currentOrigin, client->ps.grapplePoint);
		client->ps.pm_flags |= PMF_GRAPPLE_PULL;
		goto CreateRope;
	}

	dt = msec / 1000.0;

	for (i = client->numRopeElements - 1; i >= 0; i--) {
		if (!ThinkRopeElement(client, i, 0, dt / 2)) {
			Weapon_HookFree(client->hook);
			return;
		}
	}

	VectorCopy(client->hook->r.currentOrigin, pullpoint);
	n = 0;
	for (i = 0; i < client->numRopeElements; i++) {
		if (!ThinkRopeElement(client, i, 1, dt / 2)) {
			Weapon_HookFree(client->hook);
			return;
		}
		if (client->ropeElements[i].touch) {
			VectorCopy(client->ropeElements[i].pos, pullpoint);
			n = i;
		}
	}

	/*
	{
		int m;

		m = client->numRopeElements - 1;
		if (m < n) m = n;
		VectorCopy(client->ropeElements[m].pos, pullpoint);
	}
	*/

	VectorCopy(client->ropeElements[client->numRopeElements-1].pos, start);
	VectorSubtract(client->ps.origin, start, dir);
	dist = VectorNormalize(dir);

	if (client->hook->s.eType == ET_GRAPPLE) {
		// hook is attached to wall
		qboolean isRopeTaut;

		isRopeTaut = IsRopeTaut(ent, client->ropeIsTaut);
		client->ropeIsTaut = isRopeTaut;
		/*
		if (client->pers.cmd.buttons & BUTTON_GESTURE) {	// JUHOX DEBUG
			// fixed
			vec3_t v;
			float s;

			client->ps.stats[STAT_GRAPPLE_STATE] = GST_fixed;
			if (client->numRopeElements > 0) {
				VectorCopy(client->ropeElements[client->numRopeElements-1].pos, pullpoint);
			}
			VectorCopy(pullpoint, client->ps.grapplePoint);
			client->ps.pm_flags |= PMF_GRAPPLE_PULL;

			VectorCopy(client->ps.velocity, v);
			s = VectorNormalize(v);
			for (i = 1; i < client->numRopeElements; i++) {
				ropeElement_t* re;
				vec3_t vel;
				float speed;
				float oldspeed;
				float totalspeed;

				re = &client->ropeElements[i];
				speed = (s * i) / client->numRopeElements;
				oldspeed = VectorLength(re->velocity);
				totalspeed = speed + oldspeed;
				VectorScale(v, speed * speed / totalspeed, vel);
				VectorMA(vel, oldspeed / totalspeed, re->velocity, re->velocity);
			}
			goto CreateRope;
		}
		else*/
		if (client->lastTimeWinded < level.time - 250) {
			// blocked
			client->ps.stats[STAT_GRAPPLE_STATE] = GST_blocked;
			VectorCopy(pullpoint, client->ps.grapplePoint);
			client->ps.pm_flags |= PMF_GRAPPLE_PULL;

			{
				vec3_t v;
				float speed;

				v[0] = crandom();
				v[1] = crandom();
				v[2] = crandom();
				speed = 0.5 * ((level.time - client->lastTimeWinded) / 1000.0);
				if (speed > 2) speed = 2;
				VectorMA(client->ps.velocity, 400 * speed, v, client->ps.velocity);
			}
		}
		else if (isRopeTaut) {
			// pulling
			client->ps.stats[STAT_GRAPPLE_STATE] = GST_pulling;
			VectorCopy(pullpoint, client->ps.grapplePoint);
			client->ps.pm_flags |= PMF_GRAPPLE_PULL;
		}
		else {
			// winding
			client->ps.stats[STAT_GRAPPLE_STATE] = GST_rewind;
		}

		{
			TautRopePos(client, -1, NULL);
			for (i = 0; i < client->numRopeElements; i++) {
				ropeElement_t* re;
				vec3_t dest;
				vec3_t v;
				float f;
				//float speed;
				//float oldspeed;
				//float totalspeed;

				re = &client->ropeElements[i];
				TautRopePos(client, i, dest);
				VectorSubtract(dest, re->pos, v);
				VectorScale(v, 16, v);
				f = (float)i / client->numRopeElements;
				VectorMA(v, Square(f), client->ps.velocity, v);
				//speed = VectorNormalize(v);

				/*
				oldspeed = VectorLength(re->velocity);
				totalspeed = speed + oldspeed;
				VectorScale(v, speed * speed / totalspeed, v);
				VectorMA(v, oldspeed / totalspeed, re->velocity, re->velocity);
				*/
				/*
				VectorAdd(re->velocity, v, v);
				VectorScale(v, 0.5, re->velocity);
				*/
				VectorCopy(v, re->velocity);
			}
		}

		while (dist < ROPE_ELEMENT_SIZE) {
			trace_t trace;

			trap_Trace(&trace, start, NULL, NULL, client->ps.origin, -1, CONTENTS_SOLID);
			if (trace.startsolid || trace.allsolid || trace.fraction < 1) {
				VectorCopy(pullpoint, client->ps.grapplePoint);
				client->ps.pm_flags |= PMF_GRAPPLE_PULL;
				client->ps.stats[STAT_GRAPPLE_STATE] = GST_pulling;
				goto CreateRope;
			}

			client->lastTimeWinded = level.time;
			client->numRopeElements--;
			if (client->numRopeElements <= 0) {
				/*
				Weapon_HookFree(client->hook);
				return;
				*/
				goto CreateRope;
			}

			VectorCopy(client->ropeElements[client->numRopeElements-1].pos, start);
			dist = Distance(start, client->ps.origin);
		}
	}
	else {
		// hook is flying

		client->ps.stats[STAT_GRAPPLE_STATE] = GST_windoff;
		client->ropeIsTaut = qfalse;
		client->lastTimeWinded = level.time;
		/*
		if (dist > 2 * ROPE_ELEMENT_SIZE) {
			int n;

			n = (int) ((dist - ROPE_ELEMENT_SIZE) / ROPE_ELEMENT_SIZE);
			if (client->numRopeElements + n >= MAX_ROPE_ELEMENTS) {
				Weapon_HookFree(client->hook);
				return;
			}

			for (i = 0; i < n; i++) {
				vec3_t pos;

				VectorMA(start, (i+1) * ROPE_ELEMENT_SIZE, dir, pos);
				VectorCopy(pos, client->ropeElements[client->numRopeElements].pos);
				VectorCopy(
					client->ropeElements[client->numRopeElements-1].velocity,
					client->ropeElements[client->numRopeElements].velocity
				);
				client->numRopeElements++;
			}
		}
		*/
		{
			vec3_t prevPos;

			VectorCopy(client->hook->r.currentOrigin, prevPos);
			for (i = 0; i <= client->numRopeElements; i++) {
				vec3_t dir;
				float dist;
				float maxdist;
				vec3_t destPos;

				if (i < client->numRopeElements) {
					VectorCopy(client->ropeElements[i].pos, destPos);
					maxdist = 1.7 * ROPE_ELEMENT_SIZE;
				}
				else {
					VectorCopy(client->ps.origin, destPos);
					maxdist = 1.2 * ROPE_ELEMENT_SIZE;
				}

				VectorSubtract(destPos, prevPos, dir);
				dist = VectorLength(dir);
				if (dist > maxdist) {
					int j;

					n = (int) ((dist - ROPE_ELEMENT_SIZE) / ROPE_ELEMENT_SIZE) + 1;
					for (j = 0; j < n; j++) {
						vec3_t pos;

						VectorMA(prevPos, (float)(j+1) / (n+1), dir, pos);
						if (!InsertRopeElement(client, i + j, pos)) {
							Weapon_HookFree(client->hook);
							return;
						}
					}
					i += n;
				}
				VectorCopy(destPos, prevPos);
			}

		}
	}

	CreateRope:

	dist = Distance(client->ps.origin, client->hook->r.currentOrigin);
	if (dist < 200) {
		if (dist < 40) dist = 40;
		pullSpeed *= dist / 200;
	}
	client->ps.stats[STAT_GRAPPLE_SPEED] = pullSpeed;

	CreateGrappleRope(ent);
}
#endif
	}
	for ( i = oldEventSequence ; i < client->ps.eventSequence ; i++ ) {
		event = client->ps.events[ i & (MAX_PS_EVENTS-1) ];

		switch ( event ) {
		case EV_FALL_MEDIUM:
		case EV_FALL_FAR:
			if ( ent->s.eType != ET_PLAYER ) {
				break;		// not in the player model
			}
			if ( g_dmflags.integer & DF_NO_FALLING ) {
				break;
			}
			if ( event == EV_FALL_FAR ) {
				damage = 10;
			} else {
				damage = 5;
			}
			VectorSet (dir, 0, 0, 1);
			ent->pain_debounce_time = level.time + 200;	// no normal pain sound
			G_Damage (ent, NULL, NULL, NULL, NULL, damage, 0, MOD_FALLING);
			break;

		case EV_FIRE_WEAPON:
			FireWeapon( ent );
			break;

		case EV_USE_ITEM1:		// teleporter
			// drop flags in CTF
			item = NULL;
			j = 0;

			if ( ent->client->ps.powerups[ PW_REDFLAG ] ) {
				item = BG_FindItemForPowerup( PW_REDFLAG );
				j = PW_REDFLAG;
			} else if ( ent->client->ps.powerups[ PW_BLUEFLAG ] ) {
				item = BG_FindItemForPowerup( PW_BLUEFLAG );
				j = PW_BLUEFLAG;
			} else if ( ent->client->ps.powerups[ PW_NEUTRALFLAG ] ) {
				item = BG_FindItemForPowerup( PW_NEUTRALFLAG );
				j = PW_NEUTRALFLAG;
			}

			if ( item ) {
				drop = Drop_Item( ent, item, 0 );
				// decide how many seconds it has left
				drop->count = ( ent->client->ps.powerups[ j ] - level.time ) / 1000;
				if ( drop->count < 1 ) {
					drop->count = 1;
				}

				ent->client->ps.powerups[ j ] = 0;
			}

#ifdef MISSIONPACK
			if ( g_gametype.integer == GT_HARVESTER ) {
				if ( ent->client->ps.generic1 > 0 ) {
					if ( ent->client->sess.sessionTeam == TEAM_RED ) {
						item = BG_FindItem( "Blue Cube" );
					} else {
						item = BG_FindItem( "Red Cube" );
					}
					if ( item ) {
						for ( j = 0; j < ent->client->ps.generic1; j++ ) {
							drop = Drop_Item( ent, item, 0 );
							if ( ent->client->sess.sessionTeam == TEAM_RED ) {
								drop->spawnflags = TEAM_BLUE;
							} else {
								drop->spawnflags = TEAM_RED;
							}
						}
					}
					ent->client->ps.generic1 = 0;
				}
			}
#endif
			SelectSpawnPoint( ent->client->ps.origin, origin, angles );
			TeleportPlayer( ent, origin, angles );
			break;

		case EV_USE_ITEM2:		// medkit
			ent->health = ent->client->ps.stats[STAT_MAX_HEALTH] + 25;

			break;

#ifdef MISSIONPACK
		case EV_USE_ITEM3:		// kamikaze
			// make sure the invulnerability is off
			ent->client->invulnerabilityTime = 0;
			// start the kamikze
			G_StartKamikaze( ent );
			break;

		case EV_USE_ITEM4:		// portal
			if( ent->client->portalID ) {
				DropPortalSource( ent );
			}
			else {
				DropPortalDestination( ent );
			}
			break;
		case EV_USE_ITEM5:		// invulnerability
			ent->client->invulnerabilityTime = level.time + 10000;
			break;
#endif

		default:
			break;
		}
	}

}

#ifdef MISSIONPACK
/*
==============
StuckInOtherClient
==============
*/
static int StuckInOtherClient(gentity_t *ent) {
	int i;
	gentity_t	*ent2;

	ent2 = &g_entities[0];
	for ( i = 0; i < MAX_CLIENTS; i++, ent2++ ) {
		if ( ent2 == ent ) {
			continue;
		}
		if ( !ent2->inuse ) {
			continue;
		}
		if ( !ent2->client ) {
			continue;
		}
		if ( ent2->health <= 0 ) {
			continue;
		}
		//
		if (ent2->r.absmin[0] > ent->r.absmax[0])
			continue;
		if (ent2->r.absmin[1] > ent->r.absmax[1])
			continue;
		if (ent2->r.absmin[2] > ent->r.absmax[2])
			continue;
		if (ent2->r.absmax[0] < ent->r.absmin[0])
			continue;
		if (ent2->r.absmax[1] < ent->r.absmin[1])
			continue;
		if (ent2->r.absmax[2] < ent->r.absmin[2])
			continue;
		return qtrue;
	}
	return qfalse;
}
#endif

void BotTestSolid(vec3_t origin);

/*
==============
SendPendingPredictableEvents
==============
*/
void SendPendingPredictableEvents( playerState_t *ps ) {
	gentity_t *t;
	int event, seq;
	int extEvent, number;

	// if there are still events pending
	if ( ps->entityEventSequence < ps->eventSequence ) {
		// create a temporary entity for this event which is sent to everyone
		// except the client who generated the event
		seq = ps->entityEventSequence & (MAX_PS_EVENTS-1);
		event = ps->events[ seq ] | ( ( ps->entityEventSequence & 3 ) << 8 );
		// set external event to zero before calling BG_PlayerStateToEntityState
		extEvent = ps->externalEvent;
		ps->externalEvent = 0;
		// create temporary entity for event
		t = G_TempEntity( ps->origin, event );
		number = t->s.number;
		BG_PlayerStateToEntityState( ps, &t->s, qtrue );
		t->s.number = number;
		t->s.eType = ET_EVENTS + event;
		t->s.eFlags |= EF_PLAYER_EVENT;
		t->s.otherEntityNum = ps->clientNum;
		// send to everyone except the client who generated the event
		t->r.svFlags |= SVF_NOTSINGLECLIENT;
		t->r.singleClient = ps->clientNum;
		// set back external event
		ps->externalEvent = extEvent;
	}
}

/*
==============
ClientThink

This will be called once for each client frame, which will
usually be a couple times for each server frame on fast clients.

If "g_synchronousClients 1" is set, this will be called exactly
once for each server frame, which makes for smooth demo recording.
==============
*/
void ClientThink_real( gentity_t *ent ) {
	gclient_t	*client;
	pmove_t		pm;
	int			oldEventSequence;
	int			msec;
	usercmd_t	*ucmd;
	
	//pm.timer = 0;

	client = ent->client;

	// don't think if the client is not yet connected (and thus not yet spawned in)
	if (client->pers.connected != CON_CONNECTED) {
		return;
	}
	// mark the time, so the connection sprite can be removed
	ucmd = &ent->client->pers.cmd;

	// sanity check the command time to prevent speedup cheating
	if ( ucmd->serverTime > level.time + 200 ) {
		ucmd->serverTime = level.time + 200;
//		G_Printf("serverTime <<<<<\n" );
	}
	if ( ucmd->serverTime < level.time - 1000 ) {
		ucmd->serverTime = level.time - 1000;
//		G_Printf("serverTime >>>>>\n" );
	} 

	msec = ucmd->serverTime - client->ps.commandTime;
	// following others may result in bad times, but we still want
	// to check for follow toggles
	if ( msec < 1 && client->sess.spectatorState != SPECTATOR_FOLLOW ) {
		return;
	}
	if ( msec > 200 ) {
		msec = 200;
	}

	if ( pmove_msec.integer < 8 ) {
		trap_Cvar_Set("pmove_msec", "8");
	}
	else if (pmove_msec.integer > 33) {
		trap_Cvar_Set("pmove_msec", "33");
	}

	if ( pmove_fixed.integer || client->pers.pmoveFixed ) {
		ucmd->serverTime = ((ucmd->serverTime + pmove_msec.integer-1) / pmove_msec.integer) * pmove_msec.integer;
		//if (ucmd->serverTime - client->ps.commandTime <= 0)
		//	return;
	}

	//
	// check for exiting intermission
	//
	if ( level.intermissiontime ) {
		ClientIntermissionThink( client );
		return;
	}

	// spectators don't do much
	if ( client->sess.sessionTeam == TEAM_SPECTATOR ) {
		if ( client->sess.spectatorState == SPECTATOR_SCOREBOARD ) {
			return;
		}
		SpectatorThink( ent, ucmd );
		return;
	}

	// check for inactivity timer, but never drop the local client of a non-dedicated server
	if ( !ClientInactivityTimer( client ) ) {
		return;
	}

	// clear the rewards if time
	if ( level.time > client->rewardTime ) {
		client->ps.eFlags &= ~(EF_AWARD_IMPRESSIVE | EF_AWARD_EXCELLENT | EF_AWARD_GAUNTLET | EF_AWARD_ASSIST | EF_AWARD_DEFEND | EF_AWARD_CAP );
	}

	if ( client->noclip ) {
		client->ps.pm_type = PM_NOCLIP;
	} else if ( client->ps.stats[STAT_HEALTH] <= 0 ) {
		client->ps.pm_type = PM_DEAD;
	} else {
		client->ps.pm_type = PM_NORMAL;
	}

	client->ps.gravity = g_gravity.value;

	// set speed
	client->ps.speed = g_speed.value;

#ifdef MISSIONPACK
	if( bg_itemlist[client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_SCOUT ) {
		client->ps.speed *= 1.5;
	}
	else
#endif
	if ( client->ps.powerups[PW_HASTE] ) {
		client->ps.speed *= 1.3;
	}

	// Let go of the hook if we aren't firing
	if ( client->ps.weapon == WP_GRAPPLING_HOOK &&
		client->hook && !( ucmd->buttons & BUTTON_ATTACK ) ) {
		Weapon_HookFree(client->hook);
	}

	// set up for pmove
	oldEventSequence = client->ps.eventSequence;

	memset (&pm, 0, sizeof(pm));

	// check for the hit-scan gauntlet, don't let the action
	// go through as an attack unless it actually hits something
	if ( client->ps.weapon == WP_GAUNTLET && !( ucmd->buttons & BUTTON_TALK ) &&
		( ucmd->buttons & BUTTON_ATTACK ) && client->ps.weaponTime <= 0 ) {
		pm.gauntletHit = CheckGauntletAttack( ent );
	}

	if ( ent->flags & FL_FORCE_GESTURE ) {
		ent->flags &= ~FL_FORCE_GESTURE;
		ent->client->pers.cmd.buttons |= BUTTON_GESTURE;
	}

#ifdef MISSIONPACK
	// check for invulnerability expansion before doing the Pmove
	if (client->ps.powerups[PW_INVULNERABILITY] ) {
		if ( !(client->ps.pm_flags & PMF_INVULEXPAND) ) {
			vec3_t mins = { -42, -42, -42 };
			vec3_t maxs = { 42, 42, 42 };
			vec3_t oldmins, oldmaxs;

			VectorCopy (ent->r.mins, oldmins);
			VectorCopy (ent->r.maxs, oldmaxs);
			// expand
			VectorCopy (mins, ent->r.mins);
			VectorCopy (maxs, ent->r.maxs);
			trap_LinkEntity(ent);
			// check if this would get anyone stuck in this player
			if ( !StuckInOtherClient(ent) ) {
				// set flag so the expanded size will be set in PM_CheckDuck
				client->ps.pm_flags |= PMF_INVULEXPAND;
			}
			// set back
			VectorCopy (oldmins, ent->r.mins);
			VectorCopy (oldmaxs, ent->r.maxs);
			trap_LinkEntity(ent);
		}
	}
#endif

	pm.ps = &client->ps;
	pm.cmd = *ucmd;
	if ( pm.ps->pm_type == PM_DEAD ) {
		pm.tracemask = MASK_PLAYERSOLID & ~CONTENTS_BODY;
	}
	else if ( ent->r.svFlags & SVF_BOT ) {
		pm.tracemask = MASK_PLAYERSOLID | CONTENTS_BOTCLIP;
	}
	else {
		pm.tracemask = MASK_PLAYERSOLID;
	}
	pm.trace = trap_PlayerTrace;
	//pm.trace = trap_Trace;
	pm.pointcontents = trap_PointContents;
	pm.debugLevel = g_debugMove.integer;
	pm.noFootsteps = ( g_dmflags.integer & DF_NO_FOOTSTEPS ) > 0;

	pm.pmove_fixed = pmove_fixed.integer | client->pers.pmoveFixed;
	pm.pmove_msec = pmove_msec.integer;

	VectorCopy( client->ps.origin, client->oldOrigin );

	pm.ps->module = 2;

#ifdef MISSIONPACK
		if (level.intermissionQueued != 0 && g_singlePlayer.integer) {
			if ( level.time - level.intermissionQueued >= 1000  ) {
				pm.cmd.buttons = 0;
				pm.cmd.forwardmove = 0;
				pm.cmd.rightmove = 0;
				pm.cmd.upmove = 0;
				if ( level.time - level.intermissionQueued >= 2000 && level.time - level.intermissionQueued <= 2500 ) {
					trap_SendConsoleCommand( EXEC_APPEND, "centerview\n");
				}
				ent->client->ps.pm_type = PM_SPINTERMISSION;
			}
		}
		Pmove (&pm);
#else
		Pmove (&pm);
#endif

	// save results of pmove
	if ( ent->client->ps.eventSequence != oldEventSequence ) {
		ent->eventTime = level.time;
	}
	if (g_smoothClients.integer) {
		BG_PlayerStateToEntityStateExtraPolate( &ent->client->ps, &ent->s, ent->client->ps.commandTime, qtrue );
	}
	else {
		BG_PlayerStateToEntityState( &ent->client->ps, &ent->s, qtrue );
	}
	SendPendingPredictableEvents( &ent->client->ps );

	if ( !( ent->client->ps.eFlags & EF_FIRING ) ) {
		client->fireHeld = qfalse;		// for grapple
	}

	// use the snapped origin for linking so it matches client predicted versions
	//VectorCopy( ent->s.pos.trBase, ent->r.currentOrigin );
	VectorCopy (pm.mins, ent->r.mins);
	VectorCopy (pm.maxs, ent->r.maxs);

	ent->waterlevel = pm.waterlevel;
	ent->watertype = pm.watertype;

	// execute client events
	ClientEvents( ent, oldEventSequence );

	//update and create weapon angles here


	// link entity now, after any personal teleporters have been used
	trap_LinkEntity (ent);
	if ( !ent->client->noclip ) {
		G_TouchTriggers( ent );
	}

	// NOTE: now copy the exact origin over otherwise clients can be snapped into solid
	VectorCopy( ent->client->ps.origin, ent->r.currentOrigin );

	//test for solid areas in the AAS file
	BotTestAAS(ent->r.currentOrigin);

	// touch other objects
	ClientImpacts( ent, &pm );

	// save results of triggers and client events
	if (ent->client->ps.eventSequence != oldEventSequence) {
		ent->eventTime = level.time;
	}

	// swap and latch button actions
	client->oldbuttons = client->buttons;
	client->buttons = ucmd->buttons;
	client->latched_buttons |= client->buttons & ~client->oldbuttons;

	// check for respawning
	if ( client->ps.stats[STAT_HEALTH] <= 0 ) {
		// wait for the attack button to be pressed
		if ( level.time > client->respawnTime ) {
			// forcerespawn is to prevent users from waiting out powerups
			if ( g_forcerespawn.integer > 0 && 
				( level.time - client->respawnTime ) > g_forcerespawn.integer * 1000 ) {
				respawn( ent );
				return;
			}
		
			// pressing attack or use is the normal respawn method
			if ( ucmd->buttons & ( BUTTON_ATTACK | BUTTON_USE_HOLDABLE ) ) {
				respawn( ent );
			}
		}
		return;
	}

	// perform once-a-second actions
	ClientTimerActions( ent, msec );
}

/*
==================
ClientThink

A new command has arrived from the client
==================
*/
void ClientThink( int clientNum ) {
	gentity_t *ent;

	ent = g_entities + clientNum;
	trap_GetUsercmd( clientNum, &ent->client->pers.cmd );

	// mark the time we got info, so we can display the
	// phone jack if they don't get any for a while
	ent->client->lastCmdTime = level.time;

	if ( !(ent->r.svFlags & SVF_BOT) && !g_synchronousClients.integer ) {
		ClientThink_real( ent );
	}
}


void G_RunClient( gentity_t *ent ) {
	if ( !(ent->r.svFlags & SVF_BOT) && !g_synchronousClients.integer ) {
		return;
	}
	ent->client->pers.cmd.serverTime = level.time;
	ClientThink_real( ent );
}


/*
==================
SpectatorClientEndFrame

==================
*/
void SpectatorClientEndFrame( gentity_t *ent ) {
	gclient_t	*cl;

	// if we are doing a chase cam or a remote view, grab the latest info
	if ( ent->client->sess.spectatorState == SPECTATOR_FOLLOW ) {
		int		clientNum, flags;

		clientNum = ent->client->sess.spectatorClient;

		// team follow1 and team follow2 go to whatever clients are playing
		if ( clientNum == -1 ) {
			clientNum = level.follow1;
		} else if ( clientNum == -2 ) {
			clientNum = level.follow2;
		}
		if ( clientNum >= 0 ) {
			cl = &level.clients[ clientNum ];
			if ( cl->pers.connected == CON_CONNECTED && cl->sess.sessionTeam != TEAM_SPECTATOR ) {
				flags = (cl->ps.eFlags & ~(EF_VOTED | EF_TEAMVOTED)) | (ent->client->ps.eFlags & (EF_VOTED | EF_TEAMVOTED));
				ent->client->ps = cl->ps;
				ent->client->ps.pm_flags |= PMF_FOLLOW;
				ent->client->ps.eFlags = flags;
				return;
			} else {
				// drop them to free spectators unless they are dedicated camera followers
				if ( ent->client->sess.spectatorClient >= 0 ) {
					ent->client->sess.spectatorState = SPECTATOR_FREE;
					ClientBegin( ent->client - level.clients );
				}
			}
		}
	}

	if ( ent->client->sess.spectatorState == SPECTATOR_SCOREBOARD ) {
		ent->client->ps.pm_flags |= PMF_SCOREBOARD;
	} else {
		ent->client->ps.pm_flags &= ~PMF_SCOREBOARD;
	}
}

/*
==============
ClientEndFrame

Called at the end of each server frame for each connected client
A fast client will have multiple ClientThink for each ClientEdFrame,
while a slow client may have multiple ClientEndFrame between ClientThink.
==============
*/
void ClientEndFrame( gentity_t *ent ) {
	int			i;
	clientPersistant_t	*pers;

	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		SpectatorClientEndFrame( ent );
		return;
	}

	pers = &ent->client->pers;

	// turn off any expired powerups
	for ( i = 0 ; i < MAX_POWERUPS ; i++ ) {
		if ( ent->client->ps.powerups[ i ] < level.time ) {
			ent->client->ps.powerups[ i ] = 0;
		}
	}

#ifdef MISSIONPACK
	// set powerup for player animation
	if( bg_itemlist[ent->client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_GUARD ) {
		ent->client->ps.powerups[PW_GUARD] = level.time;
	}
	if( bg_itemlist[ent->client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_SCOUT ) {
		ent->client->ps.powerups[PW_SCOUT] = level.time;
	}
	if( bg_itemlist[ent->client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_DOUBLER ) {
		ent->client->ps.powerups[PW_DOUBLER] = level.time;
	}
	if( bg_itemlist[ent->client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_AMMOREGEN ) {
		ent->client->ps.powerups[PW_AMMOREGEN] = level.time;
	}
	if ( ent->client->invulnerabilityTime > level.time ) {
		ent->client->ps.powerups[PW_INVULNERABILITY] = level.time;
	}
#endif

	// save network bandwidth
#if 0
	if ( !g_synchronousClients->integer && ent->client->ps.pm_type == PM_NORMAL ) {
		// FIXME: this must change eventually for non-sync demo recording
		VectorClear( ent->client->ps.viewangles );
	}
#endif

	//
	// If the end of unit layout is displayed, don't give
	// the player any normal movement attributes
	//
	if ( level.intermissiontime ) {
		return;
	}

	// burn from lava, etc
	P_WorldEffects (ent);

	// apply all the damage taken this frame
	P_DamageFeedback (ent);

	// add the EF_CONNECTION flag if we haven't gotten commands recently
	if ( level.time - ent->client->lastCmdTime > 1000 ) {
		ent->s.eFlags |= EF_CONNECTION;
	} else {
		ent->s.eFlags &= ~EF_CONNECTION;
	}

	ent->client->ps.stats[STAT_HEALTH] = ent->health;	// FIXME: get rid of ent->health...

	G_SetClientSound (ent);

	// set the latest infor
	if (g_smoothClients.integer) {
		BG_PlayerStateToEntityStateExtraPolate( &ent->client->ps, &ent->s, ent->client->ps.commandTime, qtrue );
	}
	else {
		BG_PlayerStateToEntityState( &ent->client->ps, &ent->s, qtrue );
	}
	SendPendingPredictableEvents( &ent->client->ps );

	// set the bit for the reachability area the client is currently in
//	i = trap_AAS_PointReachabilityAreaIndex( ent->client->ps.origin );
//	ent->client->areabits[i >> 3] |= 1 << (i & 7);
}


