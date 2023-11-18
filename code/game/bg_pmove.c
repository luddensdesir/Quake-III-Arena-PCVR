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
// bg_pmove.c -- both games player movement code
// takes a playerstate and a usercmd as input and returns a modifed playerstate

#include "bg_promode.h"
#include "q_shared.h"
#include "bg_public.h"
#include "bg_local.h"

pmove_t		*pm;
pml_t		pml;

// movement parameters
float	pm_stopspeed = 100.0f;
float	pm_duckScale = 0.25f;
float	pm_swimScale = 0.50f;
float	pm_wadeScale = 0.70f;

float	pm_accelerate = 10.0f;
float	pm_airaccelerate = 1.0f;
float	pm_wateraccelerate = 4.0f;
float	pm_flyaccelerate = 8.0f;

float	pm_friction = 6.0f;
float	pm_waterfriction = 1.0f;
float	pm_flightfriction = 3.0f;
float	pm_spectatorfriction = 5.0f;

float	pm_sprintSpeedForward; //zcm
float	pm_sprintSpeedSide;
float	pm_sprintSpeedBack;

float   sightsPosSpread = .95;
float	standingPosSpread = 1.35;
float	airPosSpread = 1.5;
float	sprintPosSpread = 1.75; 
	
float minSightsGap =	10;
float maxSightsGap =	40;

float minBaseGap =	15;
float maxTwoHandedGap = 30;
float maxBaseGap =	60;
float		armLength = 10;

//the endgoal is to create a curved 3D space rather than a boxthat represents the bounds of where you can place your onscreen itemw/weappon
vec3_t	maxWeapPos = { -5, 12, -12};

vec3_t	minWeapPos = { 1, -12, -20};

vec3_t	tempSightsOffset = { 7.5, -.2, -6};
vec3_t	baseWeapOffset = { -3, 0, -12};
vec3_t	sprintWeapAngle = { 20, 60, 20};
vec3_t	sprintRocketJumpAngle = { 90, 0, 0};//should point directly under player origin
vec3_t	sprintWeapOffset = { 10, 10, -12};

vec3_t viewAccel = {1,1,1};
vec3_t moveAccel = {1,1,1};

vec3_t		viewOffsBlend, weapOffsBlend, weapAngBlend, viewAngBlend = {0, 0, 0};

vec3_t traceBody[8]; //zcm

qboolean twoHanded; 
qboolean leaning;
int testingNewSprint;

int			hand; 

int		c_pmove = 0;

int oldAim = 1;

/*
===============
PM_AddEvent

===============
*/
void PM_AddEvent( int newEvent ) {
	BG_AddPredictableEventToPlayerstate( newEvent, 0, pm->ps );
}

/*
===============
PM_AddTouchEnt
===============
*/
void PM_AddTouchEnt( int entityNum ) {
	int		i;

	if ( entityNum == ENTITYNUM_WORLD ) {
		return;
	}
	if ( pm->numtouch == MAXTOUCH ) {
		return;
	}

	// see if it is already added
	for ( i = 0 ; i < pm->numtouch ; i++ ) {
		if ( pm->touchents[ i ] == entityNum ) {
			return;
		}
	}

	// add it
	pm->touchents[pm->numtouch] = entityNum;
	pm->numtouch++;
}

/*
===================
PM_StartTorsoAnim
===================
*/
static void PM_StartTorsoAnim( int anim ) {
	if ( pm->ps->pm_type >= PM_DEAD ) {
		return;
	}
	pm->ps->torsoAnim = ( ( pm->ps->torsoAnim & ANIM_TOGGLEBIT ) ^ ANIM_TOGGLEBIT )
		| anim;
}
static void PM_StartLegsAnim( int anim ) {
	if ( pm->ps->pm_type >= PM_DEAD ) {
		return;
	}
	if ( pm->ps->legsTimer > 0 ) {
		return;		// a high priority animation is running
	}
	pm->ps->legsAnim = ( ( pm->ps->legsAnim & ANIM_TOGGLEBIT ) ^ ANIM_TOGGLEBIT )
		| anim;
}

static void PM_ContinueLegsAnim( int anim ) {
	if ( ( pm->ps->legsAnim & ~ANIM_TOGGLEBIT ) == anim ) {
		return;
	}
	if ( pm->ps->legsTimer > 0 ) {
		return;		// a high priority animation is running
	}
	PM_StartLegsAnim( anim );
}

static void PM_ContinueTorsoAnim( int anim ) {
	if ( ( pm->ps->torsoAnim & ~ANIM_TOGGLEBIT ) == anim ) {
		return;
	}
	if ( pm->ps->torsoTimer > 0 ) {
		return;		// a high priority animation is running
	}
	PM_StartTorsoAnim( anim );
}

static void PM_ForceLegsAnim( int anim ) {
	pm->ps->legsTimer = 0;
	PM_StartLegsAnim( anim );
}


/*
==================
PM_ClipVelocity

Slide off of the impacting surface
==================
*/
void PM_ClipVelocity( vec3_t in, vec3_t normal, vec3_t out, float overbounce ) {
	float	backoff;
	float	change;
	int		i;
	
	backoff = DotProduct (in, normal);
	
	if ( backoff < 0 ) {
		backoff *= overbounce;
	} else {
		backoff /= overbounce;
	}

	for ( i=0 ; i<3 ; i++ ) {
		change = normal[i]*backoff;
		out[i] = in[i] - change;
	}
}


/*
==================
PM_Friction

Handles both ground friction and water friction
==================
*/
static void PM_Friction( void ) {
	vec3_t	vec;
	float	*vel;
	float	speed, newspeed, control;
	float	drop;
	
	vel = pm->ps->velocity;
	
	VectorCopy( vel, vec );
	if ( pml.walking ) {
		vec[2] = 0;	// ignore slope movement
	}

	speed = VectorLength(vec);
	if (speed < 1) {
		vel[0] = 0;
		vel[1] = 0;		// allow sinking underwater
		// FIXME: still have z friction underwater?
		return;
	}

	drop = 0;

	// apply ground friction
	if ( pm->waterlevel <= 1 ) {
		if ( pml.walking && !(pml.groundTrace.surfaceFlags & SURF_SLICK) ) {
			// if getting knocked back, no friction
			if ( ! (pm->ps->pm_flags & PMF_TIME_KNOCKBACK) ) {
				control = speed < pm_stopspeed ? pm_stopspeed : speed;
				drop += control*pm_friction*pml.frametime;
			}
		}
	}

	// apply water friction even if just wading
	if ( pm->waterlevel ) {
		drop += speed*pm_waterfriction*pm->waterlevel*pml.frametime;
	}

	// apply flying friction
	if ( pm->ps->powerups[PW_FLIGHT]) {
		drop += speed*pm_flightfriction*pml.frametime;
	}

	if ( pm->ps->pm_type == PM_SPECTATOR) {
		drop += speed*pm_spectatorfriction*pml.frametime;
	}

	// scale the velocity
	newspeed = speed - drop;
	if (newspeed < 0) {
		newspeed = 0;
	}
	newspeed /= speed;

	vel[0] = vel[0] * newspeed;
	vel[1] = vel[1] * newspeed;
	vel[2] = vel[2] * newspeed;
}


/*
==============
PM_Accelerate

Handles user intended acceleration
==============
*/
static void PM_Accelerate( vec3_t wishdir, float wishspeed, float accel ) {
//#if 1
	// q2 style
	int			i;
	float		addspeed, accelspeed, currentspeed;

	currentspeed = DotProduct (pm->ps->velocity, wishdir);
	addspeed = wishspeed - currentspeed;
	if (addspeed <= 0) {
		return;
	}
	accelspeed = accel*pml.frametime*wishspeed;
	if (accelspeed > addspeed) {
		accelspeed = addspeed;
	}
	
	for (i=0 ; i<3 ; i++) {
		pm->ps->velocity[i] += accelspeed*wishdir[i];	
	}

//#else
//	// proper way (avoids strafe jump maxspeed bug), but feels bad
//	vec3_t		wishVelocity;
//	vec3_t		pushDir;
//	float		pushLen;
//	float		canPush;
//
//	VectorScale( wishdir, wishspeed, wishVelocity );
//	VectorSubtract( wishVelocity, pm->ps->velocity, pushDir );
//	pushLen = VectorNormalize( pushDir );
//
//	canPush = accel*pml.frametime*wishspeed;
//	if (canPush > pushLen) {
//		canPush = pushLen;
//	}
//
//	VectorMA( pm->ps->velocity, canPush, pushDir, pm->ps->velocity );
//#endif
}



/*
============
PM_CmdScale

Returns the scale factor to apply to cmd movements
This allows the clients to use axial -127 to 127 values for all directions
without getting a sqrt(2) distortion in speed.
============
*/
static float PM_CmdScale( usercmd_t *cmd ) {
	int		max;
	float	total;
	float	scale;

	max = abs( cmd->forwardmove );
	if ( abs( cmd->rightmove ) > max ) {
		max = abs( cmd->rightmove );
	}
	if ( abs( cmd->upmove ) > max ) {
		max = abs( cmd->upmove );
	}
	if ( !max ) {
		return 0;
	}

	total = sqrt( cmd->forwardmove * cmd->forwardmove + cmd->rightmove * cmd->rightmove + cmd->upmove * cmd->upmove );

	scale = (float)pm->ps->speed * max / ( 127.0 * total );

	return scale;
}


/*
================
PM_SetMovementDir

Determine the rotation of the legs reletive
to the facing dir
================
*/
static void PM_SetMovementDir( void ) {
	if ( pm->cmd.forwardmove || pm->cmd.rightmove ) {
		if ( pm->cmd.rightmove == 0 && pm->cmd.forwardmove > 0 ) {
			pm->ps->movementDir = 0;
		} else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove > 0 ) {
			pm->ps->movementDir = 1;
		} else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove == 0 ) {
			pm->ps->movementDir = 2;
		} else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove < 0 ) {
			pm->ps->movementDir = 3;
		} else if ( pm->cmd.rightmove == 0 && pm->cmd.forwardmove < 0 ) {
			pm->ps->movementDir = 4;
		} else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove < 0 ) {
			pm->ps->movementDir = 5;
		} else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove == 0 ) {
			pm->ps->movementDir = 6;
		} else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove > 0 ) {
			pm->ps->movementDir = 7;
		}
	} else {
		// if they aren't actively going directly sideways,
		// change the animation to the diagonal so they
		// don't stop too crooked
		if ( pm->ps->movementDir == 2 ) {
			pm->ps->movementDir = 1;
		} else if ( pm->ps->movementDir == 6 ) {
			pm->ps->movementDir = 7;
		} 
	}
}


static void PM_JetpackMove( void ){

	pm->ps->velocity[2] += 0;

	PM_StepSlideMove( qfalse );

}


static void PM_SprintMove( void ){
	//I could set up an enum that matches the direction of each point then sets them in a loop
	int i;
	int dir = 0; //used to specify which side of the planes are being operated on, FR,RL.
	int dir2 = 0;
	int dir3 = 0;
	vec3_t forward, right, up;

	vec3_t endPoint = {0,0,0};
	 
	//trace_t trace;

	AngleVectors(pm->ps->viewangles, forward, right, up);
	//AngleVectors(pm->ps->weaponAngles, forward, right, up);

	//void		(*trace)( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentMask );
	//pm->trace(&trace, pm->ps->origin, pm->mins, pm->maxs, endPoint, pm->ps->clientNum, pm->tracemask);
	
	//if(trace.fraction){
		//Com_Printf("%f   ", trace.endpos[0]);
		//Com_Printf("%f   ", trace.endpos[1]);
		//Com_Printf("%f   ", trace.endpos[2]);
		//Com_Printf("%f   ", trace.fraction);
		//Com_Printf("\n");
	//}

	for(i = 0; i < TOTAL_COLLISION_POINTS; i++){
		if( i & 1)		dir = 1; 
		else			dir = -1;

		if((i/2) & 1)	dir2 = 1;
		else			dir2 = -1;

		if(i>3)			dir3 = 1;
		else			dir3 = -1;
		

		VectorClear(pm->body.points[i]);

		//Com_Printf( "%f  %f  %f\n", pm->body.points[i][0], pm->body.points[i][1], pm->body.points[i][2]);

		VectorMA( pm->body.points[i], dir * BOX_GIRTH,		forward,	pm->body.points[i]);
		VectorMA( pm->body.points[i], dir2 * BOX_GIRTH,		right,		pm->body.points[i]);
		VectorMA( pm->body.points[i], dir3 * BOX_HEIGHT,	up,			pm->body.points[i]);
		//Com_Printf( "%f  %f  %f\n", pm->body.points[i][0], pm->body.points[i][1], pm->body.points[i][2]);
	}
}
 
/*
=============
PM_CheckJump
=============
*/
static qboolean PM_CheckJump( void ) {
	float leapMult = 1;
	float height = RUN_HEIGHT;
	float flatSpeed = sqrt(( pm->ps->velocity[0] *  pm->ps->velocity[0]) + (pm->ps->velocity[1] * pm->ps->velocity[1]) );
	float jumpRunDiff = 60;
	float baseJumpHeightMult = (1-(flatSpeed/320));

	jumpRunDiff = jumpRunDiff * baseJumpHeightMult;

	if ( pm->ps->pm_flags & PMF_RESPAWNED ) {
		return qfalse;		// don't allow jump until all buttons are up
	}

	if ( pm->cmd.upmove < 10 ) {
		// not holding jump
		return qfalse;
	}

	if (pm->ps->pm_weapFlags & PWF_WEAPONUP){
		height = height * .75;
	}

	// must wait for jump to be released


	//zcm automatic run
	//if ( pm->ps->pm_flags & PMF_JUMP_HELD ) {
	//	// clear upmove so cmdscale doesn't lower running speed
	//	pm->cmd.upmove = 0;
	//	return qfalse;
	//}

	pml.groundPlane = qfalse;		// jumping away
	pml.walking = qfalse;
	pm->ps->pm_flags |= PMF_JUMP_HELD;

	pm->ps->groundEntityNum = ENTITYNUM_NONE;
	//leapMult = sin(DEG2RAD(-pm->ps->weaponAngles[0]));
	leapMult = sin(DEG2RAD(-pm->ps->viewangles[0]));

	if (leapMult < 0) leapMult = 0;
	

	if(baseJumpHeightMult <= 0) baseJumpHeightMult = 0;

	//leapMult = 0; //turn it off for now

	//this needs to be modified so that you can jump higher if you are: 
		//coming out of a crouch
		//if you are moving the angle you need to leap at should be lowered until it hits 45

	if(pm->ps->pm_flags & PMF_SPRINT){
		pm->ps->velocity[2] = height + jumpRunDiff + (leapMult * MAX_LEAP_VELOCITY);
		PM_AddEvent( EV_JUMP );
	} else {
		//pm->ps->velocity[2] = JUMP_VELOCITY;
		pm->ps->velocity[2] = height + jumpRunDiff;
	}

// CPM: check for double-jump
	if (cpm_pm_jump_z) {
		if (pm->ps->stats[STAT_JUMPTIME] > 0) {
			pm->ps->velocity[2] += cpm_pm_jump_z;
		}

		pm->ps->stats[STAT_JUMPTIME] = 400;
	}
// !CPM

	if ( pm->cmd.forwardmove >= 0 ) {
		PM_ForceLegsAnim( LEGS_JUMP );
		pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
	} else {
		PM_ForceLegsAnim( LEGS_JUMPB );
		pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
	}

	return qtrue;
}

/*
=============
PM_CheckWaterJump
=============
*/
static qboolean	PM_CheckWaterJump( void ) {
	vec3_t	spot;
	int		cont;
	vec3_t	flatforward;

	if (pm->ps->pm_time) {
		return qfalse;
	}

	// check for water jump
	if ( pm->waterlevel != 2 ) {
		return qfalse;
	}

	flatforward[0] = pml.forward[0];
	flatforward[1] = pml.forward[1];
	flatforward[2] = 0;
	VectorNormalize (flatforward);

	VectorMA (pm->ps->origin, 30, flatforward, spot);
	spot[2] += 4;
	cont = pm->pointcontents (spot, pm->ps->clientNum );
	if ( !(cont & CONTENTS_SOLID) ) {
		return qfalse;
	}

	spot[2] += 16;
	cont = pm->pointcontents (spot, pm->ps->clientNum );
	if ( cont ) {
		return qfalse;
	}

	// jump out of water
	VectorScale (pml.forward, 200, pm->ps->velocity);
	pm->ps->velocity[2] = 350;

	pm->ps->pm_flags |= PMF_TIME_WATERJUMP;
	pm->ps->pm_time = 2000;

	return qtrue;
}

/*
=============
PM_CheckLedgeJump
=============
*/
static qboolean	PM_CheckLedgeJump( void ) {
	vec3_t	spot;
	int		cont;
	vec3_t	flatforward;

	if (pm->ps->pm_time) {
		return qfalse;
	}

	// check for water jump
	if ( pm->waterlevel != 2 ) {
		return qfalse;
	}

	flatforward[0] = pml.forward[0];
	flatforward[1] = pml.forward[1];
	flatforward[2] = 0;
	VectorNormalize (flatforward);

	VectorMA (pm->ps->origin, 30, flatforward, spot);
	spot[2] += 4;
	cont = pm->pointcontents (spot, pm->ps->clientNum );
	if ( !(cont & CONTENTS_SOLID) ) {
		return qfalse;
	}

	spot[2] += 16;
	cont = pm->pointcontents (spot, pm->ps->clientNum );
	if ( cont ) {
		return qfalse;
	}

	// jump out of water
	VectorScale (pml.forward, 200, pm->ps->velocity);
	pm->ps->velocity[2] = 350;

	pm->ps->pm_flags |= PMF_TIME_WATERJUMP;
	pm->ps->pm_time = 2000;

	return qtrue;
}


//============================================================================


/*
===================
PM_WaterJumpMove

Flying out of the water
===================
*/
static void PM_WaterJumpMove( void ) {
	// waterjump has no control, but falls

	PM_StepSlideMove( qtrue );

	pm->ps->velocity[2] -= pm->ps->gravity * pml.frametime;
	if (pm->ps->velocity[2] < 0) {
		// cancel as soon as we are falling down again
		pm->ps->pm_flags &= ~PMF_ALL_TIMES;
		pm->ps->pm_time = 0;
	}
}

/*
===================
PM_WaterJumpMove

Flying out of the water
===================
*/
static void PM_LedgeJumpMove( void ) {
	// waterjump has no control, but falls

	PM_StepSlideMove( qtrue );

	pm->ps->velocity[2] -= pm->ps->gravity * pml.frametime;
	if (pm->ps->velocity[2] < 0) {
		// cancel as soon as we are falling down again
		pm->ps->pm_flags &= ~PMF_ALL_TIMES;
		pm->ps->pm_time = 0;
	}
}

/*
===================
PM_WaterMove

===================
*/
static void PM_WaterMove( void ) {
	int		i;
	vec3_t	wishvel;
	float	wishspeed;
	vec3_t	wishdir;
	float	scale;
	float	vel;

	if ( PM_CheckWaterJump() ) {
		PM_WaterJumpMove();
		return;
	}
#if 0
	// jump = head for surface
	if ( pm->cmd.upmove >= 10 ) {
		if (pm->ps->velocity[2] > -300) {
			if ( pm->watertype == CONTENTS_WATER ) {
				pm->ps->velocity[2] = 100;
			} else if (pm->watertype == CONTENTS_SLIME) {
				pm->ps->velocity[2] = 80;
			} else {
				pm->ps->velocity[2] = 50;
			}
		}
	}
#endif
	PM_Friction ();

	scale = PM_CmdScale( &pm->cmd );
	//
	// user intentions
	//
	if ( !scale ) {
		wishvel[0] = 0;
		wishvel[1] = 0;
		wishvel[2] = -60;		// sink towards bottom
	} else {
		for (i=0 ; i<3 ; i++)
			wishvel[i] = scale * pml.forward[i]*pm->cmd.forwardmove + scale * pml.right[i]*pm->cmd.rightmove;

		wishvel[2] += scale * pm->cmd.upmove;
	}

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);

	if ( wishspeed > pm->ps->speed * pm_swimScale ) {
		wishspeed = pm->ps->speed * pm_swimScale;
	}

	PM_Accelerate (wishdir, wishspeed, pm_wateraccelerate);

	// make sure we can go up slopes easily under water
	if ( pml.groundPlane && DotProduct( pm->ps->velocity, pml.groundTrace.plane.normal ) < 0 ) {
		vel = VectorLength(pm->ps->velocity);
		// slide along the ground plane
		PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal, 
			pm->ps->velocity, OVERCLIP );

		VectorNormalize(pm->ps->velocity);
		VectorScale(pm->ps->velocity, vel, pm->ps->velocity);
	}

	PM_SlideMove( qfalse );
}

#ifdef MISSIONPACK
/*
===================
PM_InvulnerabilityMove

Only with the invulnerability powerup
===================
*/
static void PM_InvulnerabilityMove( void ) {
	pm->cmd.forwardmove = 0;
	pm->cmd.rightmove = 0;
	pm->cmd.upmove = 0;
	VectorClear(pm->ps->velocity);
}
#endif

/*
===================
PM_FlyMove

Only with the flight powerup
===================
*/
static void PM_FlyMove( void ) {
	int		i;
	vec3_t	wishvel;
	float	wishspeed;
	vec3_t	wishdir;
	float	scale;

	// normal slowdown
	PM_Friction ();

	scale = PM_CmdScale( &pm->cmd );
	//
	// user intentions
	//
	if ( !scale ) {
		wishvel[0] = 0;
		wishvel[1] = 0;
		wishvel[2] = 0;
	} else {
		for (i=0 ; i<3 ; i++) {
			wishvel[i] = scale * pml.forward[i]*pm->cmd.forwardmove + scale * pml.right[i]*pm->cmd.rightmove;
		}

		wishvel[2] += scale * pm->cmd.upmove;
	}

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);

	PM_Accelerate (wishdir, wishspeed, pm_flyaccelerate);

	PM_StepSlideMove( qfalse );
}


/*
===================
PM_AirMove

===================
*/
static void PM_AirMove( void ) {
	int			i;
	vec3_t		wishvel;
	float		fmove, smove;
	vec3_t		wishdir;
	float		wishspeed;
	float		scale;
	usercmd_t	cmd;
	float		accel; // CPM
	float		wishspeed2; // CPM

	//if(PM_CheckLedgeJump){
	//	PM_LedgeJumpMove();
	//	return;
	//}

	PM_Friction();

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.rightmove;

	cmd = pm->cmd;
	scale = PM_CmdScale( &cmd );

	// set the movementDir so clients can rotate the legs for strafing
	PM_SetMovementDir();

	// project moves down to flat plane
	pml.forward[2] = 0;
	pml.right[2] = 0;
	VectorNormalize (pml.forward);
	VectorNormalize (pml.right);

	for ( i = 0 ; i < 2 ; i++ ) {
		wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
	}
	wishvel[2] = 0;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);
	wishspeed *= scale;

	// CPM: Air Control
	wishspeed2 = wishspeed;
	if (DotProduct(pm->ps->velocity, wishdir) < 0)
		accel = cpm_pm_airstopaccelerate;
	else
		accel = pm_airaccelerate;
	if (pm->ps->movementDir == 2 || pm->ps->movementDir == 6)
	{
		if (wishspeed > cpm_pm_wishspeed)
			wishspeed = cpm_pm_wishspeed;	
		accel = cpm_pm_strafeaccelerate;
	}
	// !CPM

	// not on ground, so little effect on velocity

	// CPM: Air control
	PM_Accelerate (wishdir, wishspeed, accel);
	if (cpm_pm_aircontrol)
		CPM_PM_Aircontrol (pm, wishdir, wishspeed2);
	// !CPM


	// we may have a ground plane that is very steep, even
	// though we don't have a groundentity
	// slide along the steep plane
	if ( pml.groundPlane ) {
		PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal, 
			pm->ps->velocity, OVERCLIP );
	}

#if 0
	//ZOID:  If we are on the grapple, try stair-stepping
	//this allows a player to use the grapple to pull himself
	//over a ledge
	if (pm->ps->pm_flags & PMF_GRAPPLE_PULL)
		PM_StepSlideMove ( qtrue );
	else
		PM_SlideMove ( qtrue );
#endif

	PM_StepSlideMove ( qtrue );
}

/*
===================
PM_GrappleMove

===================
*/
static void PM_GrappleMove( void ) {
	vec3_t vel, v;
	float vlen;

	VectorScale(pml.forward, -16, v);
	VectorAdd(pm->ps->grapplePoint, v, v);
	VectorSubtract(v, pm->ps->origin, vel);
	vlen = VectorLength(vel);
	VectorNormalize( vel );

	if (vlen <= 100)
		VectorScale(vel, 10 * vlen, vel);
	else
		VectorScale(vel, 3000, vel);

	VectorCopy(vel, pm->ps->velocity);

	pml.groundPlane = qfalse;
}
 
static void PM_GrappleMoveTarzan( void ) {
	vec3_t vel, oldvel, v;
	float vlen;
	int pullspeed;

	pullspeed = pm->ps->stats[STAT_GRAPPLEPULL];

	VectorSubtract(pm->ps->grapplePoint, pm->ps->origin, vel);
	vlen = VectorLength(vel);
	VectorNormalize( vel );

	if (vlen < ( pullspeed / 2 ) )
		PM_Accelerate(vel, 30  * vlen, vlen * ( 40.0 / (float)pullspeed ) );
	else
		PM_Accelerate(vel, 300, 20);

	if ( vel[2] > 0.5 && pml.walking ) {
		pml.walking = qfalse;
		PM_ForceLegsAnim( LEGS_JUMP );
	}

	pml.groundPlane = qfalse;
}
  
/*
===================
PM_WalkMove

===================
*/
static void PM_WalkMove( void ) {
	int			i;
	vec3_t		wishvel;
	float		fmove, smove;
	vec3_t		wishdir;
	float		wishspeed;
	float		scale;
	usercmd_t	cmd;
	float		accelerate;
	float		vel;

	if ( pm->waterlevel > 2 && DotProduct( pml.forward, pml.groundTrace.plane.normal ) > 0 ) {
		// begin swimming
		PM_WaterMove();
		return;
	}


	if ( PM_CheckJump () ) {
		// jumped away
		if ( pm->waterlevel > 1 ) {
			PM_WaterMove();
		} else {
			PM_AirMove();
		}
		return;
	}

	PM_Friction ();

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.rightmove;

	if(pm->ps->pm_flags & PMF_SPRINT){ 
		/*I mostly want to make the effectsof bunny hopping stronger, and not simply up the speed when you land from a turn left or right the added velocity should be much higher than regular move*/
		fmove *= 1.35;
		smove *= 1.1;
	} else if (pm->ps->pm_weapFlags & PWF_WEAPONUP){
		fmove *= .6;
		smove *= .4;		 
	} /* else if (ps->pm_weapFlags & PMF_SPRINT) && (ps->pm_weapFlags & PWF_WEAPONUP){//should be a position here for running and having sights. not near same speed as sprinting, but a real stamina eater.
	}*/ else {	  

	}

	cmd = pm->cmd;
	scale = PM_CmdScale( &cmd );

	// set the movementDir so clients can rotate the legs for strafing
	PM_SetMovementDir();

	// project moves down to flat plane
	pml.forward[2] = 0;
	pml.right[2] = 0;

	// project the forward and right directions onto the ground plane
	PM_ClipVelocity (pml.forward, pml.groundTrace.plane.normal, pml.forward, OVERCLIP );
	PM_ClipVelocity (pml.right, pml.groundTrace.plane.normal, pml.right, OVERCLIP );
	//
	VectorNormalize (pml.forward);
	VectorNormalize (pml.right);

	for ( i = 0 ; i < 3 ; i++ ) {
		wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
	}
	// when going up or down slopes the wish velocity should Not be zero
//	wishvel[2] = 0;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);
	wishspeed *= scale;

	

	//Com_Printf("%f \n", sqrt( pm->ps->velocity[0] * pm->ps->velocity[0] + pm->ps->velocity[1] * pm->ps->velocity[1] ));

	// clamp the speed lower if ducking
	if ( pm->ps->pm_flags & PMF_DUCKED ) {
		if ( wishspeed > pm->ps->speed * pm_duckScale ) {
			wishspeed = pm->ps->speed * pm_duckScale;
		}
	}

	// clamp the speed lower if wading or walking on the bottom
	if ( pm->waterlevel ) {
		float	waterScale;

		waterScale = pm->waterlevel / 3.0;
		waterScale = 1.0 - ( 1.0 - pm_swimScale ) * waterScale;
		if ( wishspeed > pm->ps->speed * waterScale ) {
			wishspeed = pm->ps->speed * waterScale;
		}
	}

	// when a player gets hit, they temporarily lose
	// full control, which allows them to be moved a bit
	if ( ( pml.groundTrace.surfaceFlags & SURF_SLICK ) || pm->ps->pm_flags & PMF_TIME_KNOCKBACK ) {
		accelerate = pm_airaccelerate;
	} else {
		accelerate = pm_accelerate;
	}

	PM_Accelerate (wishdir, wishspeed, accelerate);

	//Com_Printf("velocity = %1.1f %1.1f %1.1f\n", pm->ps->velocity[0], pm->ps->velocity[1], pm->ps->velocity[2]);
	//Com_Printf("velocity1 = %1.1f\n", VectorLength(pm->ps->velocity));

	if ( ( pml.groundTrace.surfaceFlags & SURF_SLICK ) || pm->ps->pm_flags & PMF_TIME_KNOCKBACK ) {
		pm->ps->velocity[2] -= pm->ps->gravity * pml.frametime;
	} else {
		// don't reset the z velocity for slopes
//		pm->ps->velocity[2] = 0;
	}

	vel = VectorLength(pm->ps->velocity);

	// slide along the ground plane
	PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal, 
		pm->ps->velocity, OVERCLIP );

	// don't decrease velocity when going up or down a slope
	VectorNormalize(pm->ps->velocity);
	VectorScale(pm->ps->velocity, vel, pm->ps->velocity);

	// don't do anything if standing still
	if (!pm->ps->velocity[0] && !pm->ps->velocity[1]) {
		return;
	}

	PM_StepSlideMove( qfalse );

	//Com_Printf("velocity2 = %1.1f\n", VectorLength(pm->ps->velocity));

}


/*
==============
PM_DeadMove
==============
*/
static void PM_DeadMove( void ) {
	float	forward;

	if ( !pml.walking ) {
		return;
	}

	// extra friction

	forward = VectorLength (pm->ps->velocity);
	forward -= 20;
	if ( forward <= 0 ) {
		VectorClear (pm->ps->velocity);
	} else {
		VectorNormalize (pm->ps->velocity);
		VectorScale (pm->ps->velocity, forward, pm->ps->velocity);
	}
}


/*
===============
PM_NoclipMove
===============
*/
static void PM_NoclipMove( void ) {
	float	speed, drop, friction, control, newspeed;
	int			i;
	vec3_t		wishvel;
	float		fmove, smove;
	vec3_t		wishdir;
	float		wishspeed;
	float		scale;

	pm->ps->viewPos[1] = DEFAULT_VIEWHEIGHT;

	// friction

	speed = VectorLength (pm->ps->velocity);
	if (speed < 1)
	{
		VectorCopy (vec3_origin, pm->ps->velocity);
	}
	else
	{
		drop = 0;

		friction = pm_friction*1.5;	// extra friction
		control = speed < pm_stopspeed ? pm_stopspeed : speed;
		drop += control*friction*pml.frametime;

		// scale the velocity
		newspeed = speed - drop;
		if (newspeed < 0)
			newspeed = 0;
		newspeed /= speed;

		VectorScale (pm->ps->velocity, newspeed, pm->ps->velocity);
	}

	// accelerate
	scale = PM_CmdScale( &pm->cmd );

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.rightmove;
	
	for (i=0 ; i<3 ; i++)
		wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
	wishvel[2] += pm->cmd.upmove;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);
	wishspeed *= scale;

	PM_Accelerate( wishdir, wishspeed, pm_accelerate );

	// move
	VectorMA (pm->ps->origin, pml.frametime, pm->ps->velocity, pm->ps->origin);
}

//============================================================================

/*
================
PM_FootstepForSurface

Returns an event number apropriate for the groundsurface
================
*/
static int PM_FootstepForSurface( void ) {
	if ( pml.groundTrace.surfaceFlags & SURF_NOSTEPS ) {
		return 0;
	}
	if ( pml.groundTrace.surfaceFlags & SURF_METALSTEPS ) {
		return EV_FOOTSTEP_METAL;
	}
	return EV_FOOTSTEP;
}


/*
=================
PM_CrashLand

Check for hard landings that generate sound events
=================
*/
static void PM_CrashLand( void ) {
	float		delta;
	float		dist;
	float		vel, acc;
	float		t;
	float		a, b, c, den;

	// decide which landing animation to use
	if ( pm->ps->pm_flags & PMF_BACKWARDS_JUMP ) {
		PM_ForceLegsAnim( LEGS_LANDB );
	} else {
		PM_ForceLegsAnim( LEGS_LAND );
	}

	pm->ps->legsTimer = TIMER_LAND;

	// calculate the exact velocity on landing
	dist = pm->ps->origin[2] - pml.previous_origin[2];
	vel = pml.previous_velocity[2];
	acc = -pm->ps->gravity;

	a = acc / 2;
	b = vel;
	c = -dist;

	den =  b * b - 4 * a * c;
	if ( den < 0 ) {
		return;
	}
	t = (-b - sqrt( den ) ) / ( 2 * a );

	delta = vel + t * acc;
	delta = delta*delta * 0.0001;

	// ducking while falling doubles damage
	if ( pm->ps->pm_flags & PMF_DUCKED ) {
		//delta *= 2;
	}

	// never take falling damage if completely underwater
	if ( pm->waterlevel == 3 ) {
		return;
	}

	// reduce falling damage if there is standing water
	if ( pm->waterlevel == 2 ) {
		delta *= 0.25;
	}
	if ( pm->waterlevel == 1 ) {
		delta *= 0.5;
	}

	if ( delta < 1 ) {
		return;
	}

	// create a local entity event to play the sound

	// SURF_NODAMAGE is used for bounce pads where you don't ever
	// want to take damage or play a crunch sound
	if ( !(pml.groundTrace.surfaceFlags & SURF_NODAMAGE) )  {
		if ( delta > 60 ) {
			PM_AddEvent( EV_FALL_FAR );
		} else if ( delta > 40 ) {
			// this is a pain grunt, so don't play it if dead
			if ( pm->ps->stats[STAT_HEALTH] > 0 ) {
				PM_AddEvent( EV_FALL_MEDIUM );
			}
		} else if ( delta > 7 ) {
			PM_AddEvent( EV_FALL_SHORT );
		} else {
			PM_AddEvent( PM_FootstepForSurface() );
		}
	}

	// start footstep cycle over
	pm->ps->bobCycle = 0;

}

/*
=============
PM_CorrectAllSolid
=============
*/
static int PM_CorrectAllSolid( trace_t *trace ) {
	int			i, j, k;
	vec3_t		point;

	if ( pm->debugLevel ) {
		Com_Printf("%i:allsolid\n", c_pmove);
	}

	// jitter around
	for (i = -1; i <= 1; i++) {
		for (j = -1; j <= 1; j++) {
			for (k = -1; k <= 1; k++) {
				VectorCopy(pm->ps->origin, point);
				point[0] += (float) i;
				point[1] += (float) j;
				point[2] += (float) k;
				
				//not sent to ETXreal
				pm->trace(trace, point, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask, traceBody, pm->ps->origin, pm->ps->viewangles);
				//pm->trace(trace, point, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
				if ( !trace->allsolid ) {
					point[0] = pm->ps->origin[0];
					point[1] = pm->ps->origin[1];
					point[2] = pm->ps->origin[2] - 0.25;

					//not sent to ETXreal
					pm->trace(trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask, traceBody, pm->ps->origin, pm->ps->viewangles );
					//pm->trace(trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
					pml.groundTrace = *trace;
					return qtrue;
				}
			}
		}
	}

	pm->ps->groundEntityNum = ENTITYNUM_NONE;
	pml.groundPlane = qfalse;
	pml.walking = qfalse;

	return qfalse;
}


/*
=============
PM_GroundTraceMissed

The ground trace didn't hit a surface, so we are in freefall
=============
*/
static void PM_GroundTraceMissed( void ) {
	trace_t		trace;
	vec3_t		point;

	if ( pm->ps->groundEntityNum != ENTITYNUM_NONE ) {
		// we just transitioned into freefall
		if ( pm->debugLevel ) {
			Com_Printf("%i:lift\n", c_pmove);
		}

		// if they aren't in a jumping animation and the ground is a ways away, force into it
		// if we didn't do the trace, the player would be backflipping down staircases
		VectorCopy( pm->ps->origin, point );
		point[2] -= 64;

		//not sent to ETXreal
		pm->trace(&trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask, traceBody, pm->ps->origin, pm->ps->viewangles);
		//pm->trace(&trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
		if ( trace.fraction == 1.0 ) {
			if ( pm->cmd.forwardmove >= 0 ) {
				PM_ForceLegsAnim( LEGS_JUMP );
				pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
			} else {
				PM_ForceLegsAnim( LEGS_JUMPB );
				pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
			}
		}
	}

	pm->ps->groundEntityNum = ENTITYNUM_NONE;
	pml.groundPlane = qfalse;
	pml.walking = qfalse;
}


/*
=============
PM_GroundTrace
=============
*/
static void PM_GroundTrace( void ) {
	vec3_t		point;
	trace_t		trace;

	point[0] = pm->ps->origin[0];
	point[1] = pm->ps->origin[1];
	point[2] = pm->ps->origin[2] - 0.25;
	
//not sent to ETXreal
	pm->trace(&trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask, traceBody, pm->ps->origin, pm->ps->viewangles);
	//pm->trace(&trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
	pml.groundTrace = trace;

	// do something corrective if the trace starts in a solid...
	if ( trace.allsolid ) {
		if ( !PM_CorrectAllSolid(&trace) )
			return;
	}

	// if the trace didn't hit anything, we are in free fall
	if ( trace.fraction == 1.0 ) {
		//Com_Printf("trace.fraction %f", trace.fraction);
		PM_GroundTraceMissed();
		pml.groundPlane = qfalse;
		pml.walking = qfalse;
		return;
	}

	// check if getting thrown off the ground
	if ( pm->ps->velocity[2] > 0 && DotProduct( pm->ps->velocity, trace.plane.normal ) > 10 ) {
		if ( pm->debugLevel ) {
			Com_Printf("%i:kickoff\n", c_pmove);
		}
		// go into jump animation
		if ( pm->cmd.forwardmove >= 0 ) {
			PM_ForceLegsAnim( LEGS_JUMP );
			pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
		} else {
			PM_ForceLegsAnim( LEGS_JUMPB );
			pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
		}

		pm->ps->groundEntityNum = ENTITYNUM_NONE;
		pml.groundPlane = qfalse;
		pml.walking = qfalse;
		return;
	}
	
	// slopes that are too steep will not be considered onground
	if ( trace.plane.normal[2] < MIN_WALK_NORMAL ) {
		if ( pm->debugLevel ) {
			Com_Printf("%i:steep\n", c_pmove);
		}
		// FIXME: if they can't slide down the slope, let them
		// walk (sharp crevices)
		pm->ps->groundEntityNum = ENTITYNUM_NONE;
		pml.groundPlane = qtrue;
		pml.walking = qfalse;
		return;
	}

	pml.groundPlane = qtrue;
	pml.walking = qtrue;

	// hitting solid ground will end a waterjump
	if (pm->ps->pm_flags & PMF_TIME_WATERJUMP)
	{
		pm->ps->pm_flags &= ~(PMF_TIME_WATERJUMP | PMF_TIME_LAND);
		pm->ps->pm_time = 0;
	}

	if ( pm->ps->groundEntityNum == ENTITYNUM_NONE ) {
		// just hit the ground
		if ( pm->debugLevel ) {
			Com_Printf("%i:Land\n", c_pmove);
		}
		
		PM_CrashLand();

		// don't do landing time if we were just going down a slope
		if ( pml.previous_velocity[2] < -200 ) {
			// don't allow another jump for a little while
			pm->ps->pm_flags |= PMF_TIME_LAND;
			pm->ps->pm_time = 250;
		}
	}

	pm->ps->groundEntityNum = trace.entityNum;

	// don't reset the z velocity for slopes
//	pm->ps->velocity[2] = 0;

	PM_AddTouchEnt( trace.entityNum );
}


/*
=============
PM_SetWaterLevel	FIXME: avoid this twice?  certainly if not moving
=============
*/
static void PM_SetWaterLevel( void ) {
	vec3_t		point;
	int			cont;
	int			sample1;
	int			sample2;

	//
	// get waterlevel, accounting for ducking
	//
	pm->waterlevel = 0;
	pm->watertype = 0;

	point[0] = pm->ps->origin[0];
	point[1] = pm->ps->origin[1];
	point[2] = pm->ps->origin[2] + MINS_Z + 1;	
	cont = pm->pointcontents( point, pm->ps->clientNum );

	if ( cont & MASK_WATER ) {
		sample2 = pm->ps->viewPos[1] - MINS_Z;
		sample1 = sample2 / 2;

		pm->watertype = cont;
		pm->waterlevel = 1;
		point[2] = pm->ps->origin[2] + MINS_Z + sample1;
		cont = pm->pointcontents (point, pm->ps->clientNum );
		if ( cont & MASK_WATER ) {
			pm->waterlevel = 2;
			point[2] = pm->ps->origin[2] + MINS_Z + sample2;
			cont = pm->pointcontents (point, pm->ps->clientNum );
			if ( cont & MASK_WATER ){
				pm->waterlevel = 3;
			}
		}
	}

}

/*
==============
PM_CheckDuck

Sets mins, maxs, and pm->ps->viewPos[1]
==============
*/
static void PM_CheckDuck (void)
{
	trace_t	trace;

	if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
		if ( pm->ps->pm_flags & PMF_INVULEXPAND ) {
			// invulnerability sphere has a 42 units radius
			VectorSet( pm->mins, -42, -42, -42 );
			VectorSet( pm->maxs, 42, 42, 42 );
		}
		else {
			VectorSet( pm->mins, -15, -15, MINS_Z );
			VectorSet( pm->maxs, 15, 15, 16 );
		}
		pm->ps->pm_flags |= PMF_DUCKED;
		pm->ps->viewPos[1] = CROUCH_VIEWHEIGHT;
		return;
	}
	pm->ps->pm_flags &= ~PMF_INVULEXPAND;
	
	//crouchsize ZCM
	pm->mins[0] = -15;
	pm->mins[1] = -15;

	pm->maxs[0] = 15;
	pm->maxs[1] = 15;

	pm->mins[2] = MINS_Z;

	if (pm->ps->pm_type == PM_DEAD)
	{
		pm->maxs[2] = -8;
		pm->ps->viewPos[1] = DEAD_VIEWHEIGHT;
		return;
	}

	if (pm->cmd.upmove < 0)
	{	// duck
		pm->ps->pm_flags |= PMF_DUCKED;
	}
	else
	{	// stand up if possible
		if (pm->ps->pm_flags & PMF_DUCKED)
		{
			// try to stand up
			pm->maxs[2] = 28;
			pm->trace(&trace, pm->ps->origin, pm->mins, pm->maxs, pm->ps->origin, pm->ps->clientNum, pm->tracemask, traceBody, pm->ps->origin, pm->ps->viewangles);
			//pm->trace(&trace, pm->ps->origin, pm->mins, pm->maxs, pm->ps->origin, pm->ps->clientNum, pm->tracemask );
			if (!trace.allsolid)
				pm->ps->pm_flags &= ~PMF_DUCKED;
		}
	}

	if (pm->ps->pm_flags & PMF_DUCKED)
	{
		pm->maxs[2] = 16; //is this correct? since minz doesnt change shouldnt this be 14?
		pm->ps->viewPos[1] = CROUCH_VIEWHEIGHT;
	}
	else
	{
		pm->maxs[2] = 28;
		pm->ps->viewPos[1] = DEFAULT_VIEWHEIGHT;
	}
}



//===================================================================


/*
===============
PM_Footsteps
===============
*/
static void PM_Footsteps( void ) {
	float		bobmove;
	int			old;
	qboolean	footstep;

	//
	// calculate speed and cycle to be used for
	// all cyclic walking effects
	//
	pm->xyspeed = sqrt( pm->ps->velocity[0] * pm->ps->velocity[0]
		+  pm->ps->velocity[1] * pm->ps->velocity[1] );

	if ( pm->ps->groundEntityNum == ENTITYNUM_NONE ) {

		if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
			PM_ContinueLegsAnim( LEGS_IDLECR );
		}
		// airborne leaves position in cycle intact, but doesn't advance
		if ( pm->waterlevel > 1 ) {
			PM_ContinueLegsAnim( LEGS_SWIM );
		}
		return;
	}

	// if not trying to move
	if ( !pm->cmd.forwardmove && !pm->cmd.rightmove ) {
		if (  pm->xyspeed < 5 ) {
			pm->ps->bobCycle = 0;	// start at beginning of cycle again
			if ( pm->ps->pm_flags & PMF_DUCKED ) {
				PM_ContinueLegsAnim( LEGS_IDLECR );
			} else {
				PM_ContinueLegsAnim( LEGS_IDLE );
			}
		}
		return;
	}
	

	footstep = qfalse;

	if ( pm->ps->pm_flags & PMF_DUCKED ) {
		bobmove = 0.5;	// ducked characters bob much faster
		if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
			PM_ContinueLegsAnim( LEGS_BACKCR );
		}
		else {
			PM_ContinueLegsAnim( LEGS_WALKCR );
		}
		// ducked characters never play footsteps
	/*
	} else 	if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
		if ( !( pm->cmd.buttons & BUTTON_WALKING ) ) {
			bobmove = 0.4;	// faster speeds bob faster
			footstep = qtrue;
		} else {
			bobmove = 0.3;
		}
		PM_ContinueLegsAnim( LEGS_BACK );
	*/
	} else {
		if ( !( pm->cmd.buttons & BUTTON_WALKING ) ) {
			bobmove = 0.4f;	// faster speeds bob faster
			if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
				PM_ContinueLegsAnim( LEGS_BACK );
			}
			else {
				PM_ContinueLegsAnim( LEGS_RUN );
			}
			footstep = qtrue;
		} else {
			bobmove = 0.3f;	// walking bobs slow
			if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
				PM_ContinueLegsAnim( LEGS_BACKWALK );
			}
			else {
				PM_ContinueLegsAnim( LEGS_WALK );
			}
		}
	}

	// check for footstep / splash sounds
	old = pm->ps->bobCycle;
	pm->ps->bobCycle = (int)( old + bobmove * pml.msec ) & 255;

	// if we just crossed a cycle boundary, play an apropriate footstep event
	if ( ( ( old + 64 ) ^ ( pm->ps->bobCycle + 64 ) ) & 128 ) {
		if ( pm->waterlevel == 0 ) {
			// on ground will only play sounds if running
			if ( footstep && !pm->noFootsteps ) {
				PM_AddEvent( PM_FootstepForSurface() );
			}
		} else if ( pm->waterlevel == 1 ) {
			// splashing
			PM_AddEvent( EV_FOOTSPLASH );
		} else if ( pm->waterlevel == 2 ) {
			// wading / swimming at surface
			PM_AddEvent( EV_SWIM );
		} else if ( pm->waterlevel == 3 ) {
			// no sound when completely underwater

		}
	}
}

/*
==============
PM_WaterEvents

Generate sound events for entering and leaving water
==============
*/
static void PM_WaterEvents( void ) {		// FIXME?
	//
	// if just entered a water volume, play a sound
	//
	if (!pml.previous_waterlevel && pm->waterlevel) {
		PM_AddEvent( EV_WATER_TOUCH );
	}

	//
	// if just completely exited a water volume, play a sound
	//
	if (pml.previous_waterlevel && !pm->waterlevel) {
		PM_AddEvent( EV_WATER_LEAVE );
	}

	//
	// check for head just going under water
	//
	if (pml.previous_waterlevel != 3 && pm->waterlevel == 3) {
		PM_AddEvent( EV_WATER_UNDER );
	}

	//
	// check for head just coming out of water
	//
	if (pml.previous_waterlevel == 3 && pm->waterlevel != 3) {
		PM_AddEvent( EV_WATER_CLEAR );
	}
}


/*
===============
PM_BeginWeaponChange
===============
*/
static void PM_BeginWeaponChange( int weapon ) {
	if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS ) {
		return;
	}

	if ( !( pm->ps->stats[STAT_WEAPONS] & ( 1 << weapon ) ) ) {
		return;
	}
	
	if ( pm->ps->weaponstate == WEAPON_DROPPING ) {
		return;
	}

	PM_AddEvent( EV_CHANGE_WEAPON );
	pm->ps->weaponstate = WEAPON_DROPPING;
	pm->ps->weaponTime += 200;
	//PM_StartTorsoAnim( TORSO_DROP );
}


/*
===============
PM_FinishWeaponChange
===============
*/
static void PM_FinishWeaponChange( void ) {
	int		weapon;

	weapon = pm->cmd.weapon;
	if ( weapon < WP_NONE || weapon >= WP_NUM_WEAPONS ) {
		weapon = WP_NONE;
	}

	if ( !( pm->ps->stats[STAT_WEAPONS] & ( 1 << weapon ) ) ) {
		weapon = WP_NONE;
	}

	pm->ps->weapon = weapon;
	pm->ps->weaponstate = WEAPON_RAISING;
	pm->ps->weaponTime += 250;
	//PM_StartTorsoAnim( TORSO_RAISE );
}


/*
==============
PM_TorsoAnimation

==============
*/
static void PM_TorsoAnimation( void ) {
	if ( pm->ps->weaponstate == WEAPON_READY ) {
		if ( pm->ps->weapon == WP_GAUNTLET ) {
			PM_ContinueTorsoAnim( TORSO_STAND2 );
		} else {
			PM_ContinueTorsoAnim( TORSO_STAND );
		}
		return;
	}
}


/*
==============
PM_Weapon

Generates weapon events and modifes the weapon counter
==============
*/
static void PM_Weapon( void ) {
	int		addTime;

	// don't allow attack until all buttons are up
	if ( pm->ps->pm_flags & PMF_RESPAWNED ) {
		return;
	}

	// ignore if spectator
	if ( pm->ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}

	// check for dead player
	if ( pm->ps->stats[STAT_HEALTH] <= 0 ) {
		pm->ps->weapon = WP_NONE;
		return;
	}

	// check for item using
	if ( pm->cmd.buttons & BUTTON_USE_HOLDABLE ) {
		if ( ! ( pm->ps->pm_flags & PMF_USE_ITEM_HELD ) ) {
			if ( bg_itemlist[pm->ps->stats[STAT_HOLDABLE_ITEM]].giTag == HI_MEDKIT
				&& pm->ps->stats[STAT_HEALTH] >= (pm->ps->stats[STAT_MAX_HEALTH] + 25) ) {
				// don't use medkit if at max health
			} else {
				pm->ps->pm_flags |= PMF_USE_ITEM_HELD;
				PM_AddEvent( EV_USE_ITEM0 + bg_itemlist[pm->ps->stats[STAT_HOLDABLE_ITEM]].giTag );
				pm->ps->stats[STAT_HOLDABLE_ITEM] = 0;
			}
			return;
		}
	} else {
		pm->ps->pm_flags &= ~PMF_USE_ITEM_HELD;
	}


	// make weapon function
	if ( pm->ps->weaponTime > 0 ) {
		pm->ps->weaponTime -= pml.msec;
	}

	// check for weapon change
	// can't change if weapon is firing, but can change
	// again if lowering or raising
	//if ( /*pm->ps->weaponTime <= 0 *//*|| pm->ps->weaponstate != WEAPON_FIRING*/ ) { //ZCM
		if ( pm->ps->weapon != pm->cmd.weapon ) {
			PM_BeginWeaponChange( pm->cmd.weapon );
		}
	//}

	if ( pm->ps->weaponTime > 0 ) {
		return;
	}

	// change weapon if time
	if ( pm->ps->weaponstate == WEAPON_DROPPING ) {
		PM_FinishWeaponChange();
		return;
	}

	if ( pm->ps->weaponstate == WEAPON_RAISING ) {
		pm->ps->weaponstate = WEAPON_READY;
		if ( pm->ps->weapon == WP_GAUNTLET ) {
			PM_StartTorsoAnim( TORSO_STAND2 );
		} else {
			PM_StartTorsoAnim( TORSO_STAND );
		}
		return;
	}

	// check for fire
	if ( ! (pm->cmd.buttons & BUTTON_ATTACK) ) {
		pm->ps->weaponTime = 0;
		pm->ps->weaponstate = WEAPON_READY;
		return;
	}

	// start the animation even if out of ammo
	if ( pm->ps->weapon == WP_GAUNTLET ) {
		// the guantlet only "fires" when it actually hits something
		if ( !pm->gauntletHit ) {
			pm->ps->weaponTime = 0;
			pm->ps->weaponstate = WEAPON_READY;
			return;
		}
		PM_StartTorsoAnim( TORSO_ATTACK2 );
	} else {
		PM_StartTorsoAnim( TORSO_ATTACK );
	}

	pm->ps->weaponstate = WEAPON_FIRING;

	// check for out of ammo
	if ( ! pm->ps->ammo[ pm->ps->weapon ] ) {
		PM_AddEvent( EV_NOAMMO );
		pm->ps->weaponTime += 500;
		return;
	}

	// take an ammo away if not infinite
	if ( pm->ps->ammo[ pm->ps->weapon ] != -1 ) {
		pm->ps->ammo[ pm->ps->weapon ]--;
	}

	// fire weapon
	PM_AddEvent( EV_FIRE_WEAPON );

	switch( pm->ps->weapon ) {
	default:
	case WP_GAUNTLET:
		addTime = 400;
		break;
	case WP_LIGHTNING:
		addTime = 50;
		break;
	case WP_SHOTGUN:
		addTime = 1000;
		break;
	case WP_MACHINEGUN:
		addTime = 100;
		//addTime = 50;
		break;
	case WP_GRENADE_LAUNCHER:
		addTime = 150;
	//case WP_GRENADE_LAUNCHER:
	//	addTime = 800;
		break;
	case WP_ROCKET_LAUNCHER:
		//addTime = 150;
		addTime = 900;
		break;
	case WP_PLASMAGUN:
		addTime = 100;
		break;
	case WP_RAILGUN:
		addTime = 1500;
		//addTime = 300;
		break;
	case WP_BFG:
		addTime = 200;
		break;
	case WP_GRAPPLING_HOOK:
		addTime = 400;
		break;
#ifdef MISSIONPACK
	case WP_NAILGUN:
		addTime = 1000;
		break;
	case WP_PROX_LAUNCHER:
		addTime = 800;
		break;
	case WP_CHAINGUN:
		addTime = 30;
		break;
#endif
	}

#ifdef MISSIONPACK
	if( bg_itemlist[pm->ps->stats[STAT_PERSISTANT_POWERUP]].giTag == PW_SCOUT ) {
		addTime /= 1.5;
	}
	else
	if( bg_itemlist[pm->ps->stats[STAT_PERSISTANT_POWERUP]].giTag == PW_AMMOREGEN ) {
		addTime /= 1.3;
  }
  else
#endif
	if ( pm->ps->powerups[PW_HASTE] ) {
		addTime /= 1.3;
	}

	pm->ps->weaponTime += addTime;
}

/*
================
PM_Animate
================
*/

static void PM_Animate( void ) {
	if ( pm->cmd.buttons & BUTTON_GESTURE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_GESTURE );
			pm->ps->torsoTimer = TIMER_GESTURE;
			PM_AddEvent( EV_TAUNT );
		}
#ifdef MISSIONPACK
	} else if ( pm->cmd.buttons & BUTTON_GETFLAG ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_GETFLAG );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_GUARDBASE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_GUARDBASE );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_PATROL ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_PATROL );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_FOLLOWME ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_FOLLOWME );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_AFFIRMATIVE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_AFFIRMATIVE);
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_NEGATIVE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_NEGATIVE );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
#endif
	}
}


/*
================
PM_DropTimers
================
*/
static void PM_DropTimers( void ) {
	// drop misc timing counter
	if ( pm->ps->pm_time ) {
		if ( pml.msec >= pm->ps->pm_time ) {
			pm->ps->pm_flags &= ~PMF_ALL_TIMES;
			pm->ps->pm_time = 0;
		} else {
			pm->ps->pm_time -= pml.msec;
		}
	}

	// drop animation counter
	if ( pm->ps->legsTimer > 0 ) {
		pm->ps->legsTimer -= pml.msec;
		if ( pm->ps->legsTimer < 0 ) {
			pm->ps->legsTimer = 0;
		}
	}

	if ( pm->ps->torsoTimer > 0 ) {
		pm->ps->torsoTimer -= pml.msec;
		if ( pm->ps->torsoTimer < 0 ){ 
			pm->ps->torsoTimer = 0;
		}
	}
}

//I don't really want this here. Why doesn't it work in qmath? Is there more than one qmath?
//put this in qshared with lerpangle to get it to work
float LerpPosition (float from, float to, float frac) {
	float	a;
	a = from + (frac) * (to - from);
	return a;
}

float LerpPositionSq (float from, float to, float frac) {
	float	a;
	a = from + (frac) * (to - from);
	return a;
}



signed short WrapShort( int i ) {
	if ( i < -32768 ) {
		i += 65535;
	}

	if ( i > 32767 ) {
		i -= 65536;
	}

	return i;
}

int FlipShort( int i ) {
	//this flips the angles
	i += 32767;
	if(i > 32767){
		i -= 65534;
	} 

	return i;
			
}


void PM_CalcPlayerStance( playerState_t *ps, const usercmd_t *cmd ) {
	//is the player using two weapons
	//is the weapon shouldered
	//is the player using ironsights
	//is the player using one hand
	//is the player using the left or right dominant hand
	//is the player leaning forward
	//is the player sprinting
}


weaponhandling_t PM_CheckWeaponHandling( playerState_t *ps ){

	if(ps->pm_weapFlags & PWF_GAPUP){
		if (ps->pm_weapFlags & PWF_WEAPONUP){
			
			ps->maxSightsGap += 3;
		} else {
			ps->maxBaseGap += 3;
		}
	}

	if(ps->pm_weapFlags & PWF_GAPDOWN){
		if (ps->pm_weapFlags & PWF_WEAPONUP){
			ps->maxSightsGap -= 3;
		} else {
			ps->maxBaseGap -= 3;
		}
	}


	if(ps->maxSightsGap >= maxSightsGap){
		ps->maxSightsGap = maxSightsGap;
	}

	if(ps->maxBaseGap >= maxTwoHandedGap){
		twoHanded = qtrue;
	} else {
		twoHanded = qfalse;
	}

	if(ps->maxBaseGap >= maxBaseGap){
		if(ps->pm_weapFlags & PWF_GAPUP){
			//when you are increasing the gap but the gap is at max
			leaning = qtrue;
		}
		ps->maxBaseGap = maxBaseGap;
	}

	if(ps->maxSightsGap <= minSightsGap){
		ps->maxSightsGap = minSightsGap;
	}

	if(ps->maxBaseGap <= minBaseGap){
		ps->maxBaseGap = minBaseGap;
		if(ps->pm_weapFlags & PWF_GAPDOWN){
			return WEAPON_SHOULDERED;
		} else {
			return WEAPON_NORMAL;
		}
	} else {
			return WEAPON_NORMAL;
	}
}

void PM_SetLastBodyStance( playerState_t *ps ){
	if(ps->pm_flags & PMF_SPRINT){ 

 		if ( ps->groundEntityNum == ENTITYNUM_NONE ) {
		}  else {
		}

		ps->lastStance = 0;
	} else if (ps->pm_weapFlags & PWF_WEAPONUP){
		if(ps->lastStance + .01 >= 1){
			ps->lastStance = 1;
		}else if(PM_CheckWeaponHandling(ps) == WEAPON_SHOULDERED){
			//sightsGap = ps->lastStance = LerpPosition( ps->lastStance, 1, .05);
			//sightsGap = sightsGap * sightsGap * sightsGap;
		}

	} /* else if (ps->pm_weapFlags & PMF_SPRINT) && (ps->pm_weapFlags & PWF_WEAPONUP){//should be a position here for running and having sights. not near same speed as sprinting, but a real stamina eater.
	}*/ else {	
		ps->lastStance = 0;
	}
}
  
void PM_GetSprintMultiplier(playerState_t * ps){
	
	float		flatSpeed;
	float		speed;
	float speedMult;
	float sprintMultiplier;
	float sprintAddAng;
	float sightsGap; 

	sightsGap = 1;

	flatSpeed = sqrt( (pm->ps->velocity[0] *  pm->ps->velocity[0]) + (pm->ps->velocity[1] * pm->ps->velocity[1]) );
	speed = sqrt( (pm->ps->velocity[0] *  pm->ps->velocity[0]) + (pm->ps->velocity[1] * pm->ps->velocity[1]) + (pm->ps->velocity[2] * pm->ps->velocity[2]) );
	pm->dist = sqrt(fabs((pm->weapViewGap[0] * pm->weapViewGap[0]) + (pm->weapViewGap[1] *  pm->weapViewGap[1])));
	speedMult = speed/320;
	sprintMultiplier = (flatSpeed-320) * .02;
	if(sprintMultiplier <0) sprintMultiplier = 0;
	if(sprintMultiplier > 5) sprintMultiplier = 5;

	//sprinting //merge this with the area below
	if( pm->ps->pm_flags & PMF_SPRINT){
		if(!testingNewSprint){
 			if ( pm->ps->groundEntityNum == ENTITYNUM_NONE ) {
				//pm->ps->weapPosLerpFrac = LerpPositionSq( pm->ps->weapPosLerpFrac, .07, 1);
			}  else {
				//pm->ps->weapPosLerpFrac = LerpPosition( pm->ps->weapPosLerpFrac, .025, 1);
			}
		} else if(testingNewSprint){
			//pm->ps->weapPosLerpFrac = LerpPosition( pm->ps->weapPosLerpFrac, .1, 1);
		}
	} else if(pm->cmd.buttons & BUTTON_WALKING){
			//pm->ps->weapPosLerpFrac = LerpPositionSq( pm->ps->weapPosLerpFrac, .2, .5);
	} else if (pm->ps->pm_weapFlags & PWF_WEAPONUP){
		
 		if ( pm->ps->groundEntityNum == ENTITYNUM_NONE ) {
			//pm->ps->weapPosLerpFrac = LerpPositionSq( pm->ps->weapPosLerpFrac, .05, .5);
		}  else {
			//pm->ps->weapPosLerpFrac = LerpPosition( pm->ps->weapPosLerpFrac, .2, .5);
		}
		
	} /* else if (ps->pm_weapFlags & PMF_SPRINT) && (ps->pm_weapFlags & PWF_WEAPONUP){//should be a position here for running and having sights. not near same speed as sprinting, but a real stamina eater.
	}*/ else {	
		//pm->ps->weapPosLerpFrac = LerpPosition( pm->ps->weapPosLerpFrac, .1 , 1);
	}

	//sprinting
	if(ps->pm_flags & PMF_SPRINT){ 
		if(!testingNewSprint){
 			if ( ps->groundEntityNum == ENTITYNUM_NONE ) {
				ps->weapAngLerpFrac = LerpPositionSq( ps->weapAngLerpFrac, .1, .65); 
				ps->viewAngLerpFrac = LerpPositionSq( ps->viewAngLerpFrac, 1, .5);
				ps->weapPosLerpFrac = LerpPositionSq( ps->weapPosLerpFrac, .07, 1);
 
			}  else {
				//lower weapon to appropriate position
				if(hand == 0){
					weapAngBlend[0] = 3 * sprintWeapAngle[0]; 
				} else {
					weapAngBlend[0] = sprintWeapAngle[0]; 
				}


				weapAngBlend[1] = ( hand * sprintWeapAngle[1]) + viewAngBlend[1];

				ps->weapAngLerpFrac = LerpPositionSq( ps->weapAngLerpFrac, .075, 1); //lerpfrac's lerp in/out needs to change depending on the gap between position and sprint position

				if(pm->dist >= ps->gapLerp/2 && pm->dist < ps->gapLerp){
					ps->viewAngLerpFrac = LerpPositionSq( ps->viewAngLerpFrac, .8, 1);
				} else {
					ps->viewAngLerpFrac = LerpPositionSq( ps->viewAngLerpFrac, .5, 1); 
				} 

				ps->weapPosLerpFrac = LerpPosition( ps->weapPosLerpFrac, .025, 1);
		
				ps->pm_weapFlags &= ~PWF_WEAPONUP;
				ps->zoomed = 0;
			}
		} else if(testingNewSprint){
			ps->weapAngLerpFrac = LerpPositionSq( ps->weapAngLerpFrac, 1, .5); //great handling
			//ps->weapAngLerpFrac = LerpPositionSq( ps->weapAngLerpFrac, .1, 1); //terrible handling, very dramatic looking, however. would make sense for 1 handed simulation
			ps->weapPosLerpFrac = LerpPosition( ps->weapPosLerpFrac, .1, 1);

			ps->viewAngLerpFrac = LerpPositionSq( ps->viewAngLerpFrac, .75, 1);
			
			sprintAddAng += .25 * pm->weapViewGap[0] * hand;
			sprintAddAng += .1 * -pm->weapViewGap[1];
			sprintAddAng += hand * 5;

			sprintAddAng += .12 * pm->weapViewGap[0] * hand *	(sprintMultiplier);
			sprintAddAng += .1 * -pm->weapViewGap[1]	*		(sprintMultiplier);
			sprintAddAng += hand * 2 *							(sprintMultiplier);
			

			ps->zoomed = 0;
		}

		//Com_Printf("%f, %f, %f\n",  pm->weapViewGap[0],  pm->weapViewGap[1],  pm->weapViewGap[2]);

		viewAngBlend[2] += sprintAddAng;

	} else if(pm->cmd.buttons & BUTTON_WALKING){
			ps->weapPosLerpFrac = LerpPositionSq( ps->weapPosLerpFrac, .2, .5);
	} else if (ps->pm_weapFlags & PWF_WEAPONUP){
		
 		if ( ps->groundEntityNum == ENTITYNUM_NONE ) {
			ps->weapAngLerpFrac = LerpPositionSq( ps->weapAngLerpFrac, .25, .5); 
			ps->viewAngLerpFrac = LerpPositionSq( ps->viewAngLerpFrac, 1, ps->lastStance);
			//ps->viewAngLerpFrac = LerpPositionSq( ps->viewAngLerpFrac, 1, ps->lastStance);
			ps->weapPosLerpFrac = LerpPositionSq( ps->weapPosLerpFrac, .05, .5);

			//if(ps->velocity[2] > 150 && (ps->pm_weap`Flags & PWF_WEAPONUP)){
			//	ps->pm_weapFlags &= ~PWF_WEAPONUP;
				//lower air acceleration if zoomed
			//}
		}  else {
			ps->weapAngLerpFrac = LerpPositionSq( ps->weapAngLerpFrac, 1, .5);
			ps->viewAngLerpFrac = LerpPositionSq( ps->viewAngLerpFrac,  sightsGap, 1); 
			//ps->viewAngLerpFrac = LerpPositionSq( ps->viewAngLerpFrac, 1, ps->lastStance); 
			ps->weapPosLerpFrac = LerpPosition( ps->weapPosLerpFrac, .2, .5);
		}

		ps->zoomed = 1;
		//Com_Printf( "%f \n", ps->weapPosLerpFrac );
	} /* else if (ps->pm_weapFlags & PMF_SPRINT) && (ps->pm_weapFlags & PWF_WEAPONUP){//should be a position here for running and having sights. not near same speed as sprinting, but a real stamina eater.
	}*/ else {	

		ps->weapAngLerpFrac = LerpPositionSq( ps->weapAngLerpFrac, 1, .5); //great handling
		//ps->weapAngLerpFrac = LerpPositionSq( ps->weapAngLerpFrac, .1, 1); //terrible handling, very dramatic looking, however. would make sense for 1 handed simulation
		ps->viewAngLerpFrac = LerpPositionSq( ps->viewAngLerpFrac, 1, .25);
		ps->weapPosLerpFrac = LerpPosition( ps->weapPosLerpFrac, .1 , 1);
 
		ps->zoomed = 0;
	}
}



void PM_UpdateViewAngles(playerState_t *ps, const usercmd_t *cmd){
	int			parent[2];		//angles that contact the bounds and drag the child[i]
	int			child[2];
	float		zoomBoundMult;
	short		viewChild[2];	
	short		weapChild[2];	
	float		viewMult;
	int			transferView;
	int			flipped[2];
	short		shortGap;
	int			shortTotal = 65535;
	int			i;
	float		gap[2];
	int			root;
	float		pitchMult;
	float		newPos[2];


	if((ps->pm_flags & PMF_RESPAWNED)){			//the whole else block should be put in a function for later use with vehicles for instance
		ps->switchingParent = 0;
		VectorCopy(viewAngBlend, weapAngBlend);		//weapon should really start off pointing towards the center and downwards
	} else {
		for (i=0 ; i<2 ; i++) {
			zoomBoundMult = 1;
			if(ps->pm_flags & PMF_SPRINT){ // I'm not satisfied with this. view ang should not snap to input angles	
				if(!testingNewSprint){
					ps->gapLerp = LerpPositionSq( ps->gapLerp, 30, .25);	
					viewMult = .5;
				} else {
					ps->gapLerp = LerpPositionSq( ps->gapLerp, 60, .25);	
					viewMult = -.5;
					viewMult = -1;
				}
				//when the cap is hit during sprinting the screen should tilt
			} else if (ps->pm_weapFlags & PWF_WEAPONUP) {
				if(ps->lastStance != 1){
					ps->gapLerp = LerpPositionSq( ps->gapLerp, 1, ps->lastStance);
				} else {
					ps->gapLerp = LerpPositionSq( ps->gapLerp, ps->maxSightsGap, 1);
				}


				zoomBoundMult = .25;
				viewMult = .8;//you should be able to edit that value between -i and .75	
			} else {
				ps->gapLerp = LerpPositionSq( ps->gapLerp, ps->maxBaseGap, .25); 
				viewMult = .5;
			}

			if ( pm->ps->pm_weapFlags & BUTTON_UNLOCK_AIM ){
				ps->gapLerp = 60;
			} else {
			}

			//if( (pm->cmd.buttons & BUTTON_WALKING)){
			//	ps->gapLerp /= 2;
			//	//gotta lerp this like i do irons
			//}

			viewChild[i] = ANGLE2SHORT(ps->viewangles[i]);
			weapChild[i] = ANGLE2SHORT(ps->weaponAngles[i]);

			if(!(ps->pm_weapFlags & PWF_PARENTSWITCH)){
				if(ps->switchingParent == 1){
						ps->swapGap[i] = WrapShort(cmd->angles[i]) - weapChild[i];
				}
				child[i] = viewChild[i];
			} else { 
				if(ps->switchingParent == 1){
					ps->swapGap[i] = WrapShort(cmd->angles[i]) - viewChild[i];
				}
				child[i] = weapChild[i];
			}

			parent[i] = WrapShort(cmd->angles[i]) - ps->swapGap[i];

			flipped[i] = 0;
			//check which half of the world weapon is pointing towards
			if( (parent[i] > (shortTotal / 4)) || (parent[i] < -(shortTotal / 4))){ 
				if(!(ps->pm_weapFlags & PWF_PARENTSWITCH)){
						
				} else {

				}

				parent[i] = FlipShort( parent[i] );
				child[i] = FlipShort( child[i] );
				ps->oldAngles[i] = FlipShort( ps->oldAngles[i] );
				flipped[i] = 1;
			}  else {

			}
			
			gap[i] = parent[i] - child[i];

			//Com_Printf("%i ", parent[i]);
			//Com_Printf("%i ", child[i]);`
			//Com_Printf("%f ", gap[i]);
			//Com_Printf("%i ", ps->swapGap[i] );
			//Com_Printf("%i ", WrapShort(ANGLE2SHORT(ps->weaponAngles[i]) ));
			//Com_Printf("%i ", WrapShort(cmd->angles[i]));
			//Com_Printf("%i ", WrapShort(cmd->angles[i]) - ps->swapGap[i]);
			//Com_Printf("%i ", WrapShort(ANGLE2SHORT(ps->viewangles[i])));
			//Com_Printf("\n");

			shortGap = ANGLE2SHORT(ps->gapLerp );

			//radial
			if(i == 1){
				root = sqrt(fabs(( gap[0] * gap[0] ) + (gap[1] * gap[1])));

				newPos[0] = shortGap * fabs(sin((atan(gap[0]/gap[1]))));
				newPos[1] = shortGap * cos((atan(gap[0]/gap[1])));

				if ( root > shortGap){ 
					if(gap[0] < 0){
						child[0] = parent[0] + newPos[0];
					} else {
						child[0] = parent[0] - newPos[0];
					}

					if(gap[1] < 0){	
						child[1] = parent[1] + newPos[1];
					} else {
						child[1] = parent[1] - newPos[1];
					}
				} else {
					if(!testingNewSprint){
						pitchMult = viewMult;
						
					} else {
						if(!(ps->pm_flags & PMF_SPRINT)){ 
							pitchMult = viewMult;
						} else {
							pitchMult = .1;
							viewMult = -1;
						}
					}
					child[0] += ((parent[0] - ps->oldAngles[0]) * pitchMult);
					child[1] += ((parent[1] - ps->oldAngles[1]) * viewMult);
				}
			}
		}

		for (i=0 ; i<2 ; i++) {
			//reorient angles if screen was flipped[i]
			if( flipped[i] ){
				parent[i] = FlipShort(parent[i]);
				child[i] = FlipShort(child[i]);
			}

			ps->oldAngles[i] = parent[i]; //store oldangles to calculate turn velocity and ramp gun position

			//it's a problem with the transition from parent[i] to child[i]

			if(!(ps->pm_weapFlags & PWF_PARENTSWITCH)){
				weapAngBlend[i] = SHORT2ANGLE( parent[i] );
				viewAngBlend[i] = SHORT2ANGLE( child[i] );
			} else {
				viewAngBlend[i] = SHORT2ANGLE( parent[i] );
				weapAngBlend[i] = SHORT2ANGLE( child[i] );
			}

			pm->weapViewGap[i] = weapAngBlend[i] - viewAngBlend[i];
			pm->weapViewGap[i] = RAD2DEG(asin(sin(DEG2RAD(pm->weapViewGap[i])))); 
		}
	}
}


float LerpWeaponAngles( playerState_t *ps ){
	ps->weaponAngles[0] = LerpAngle( ps->weaponAngles[0], weapAngBlend[0], ps->weapAngLerpFrac);
	ps->weaponAngles[1] = LerpAngle( ps->weaponAngles[1], weapAngBlend[1], ps->weapAngLerpFrac);
	ps->weaponAngles[2] = LerpAngle( ps->weaponAngles[2], weapAngBlend[2], .05);
}

float LerpViewAngles( playerState_t *ps ){
	ps->viewangles[0] = LerpAngle( ps->viewangles[0], viewAngBlend[0], ps->viewAngLerpFrac);
	ps->viewangles[1] = LerpAngle( ps->viewangles[1], viewAngBlend[1], ps->viewAngLerpFrac);
	ps->viewangles[2] = LerpAngle( ps->viewangles[2], viewAngBlend[2], .05);
}

float LerpWeaponOffset( playerState_t *ps, vec3_t speedMult ){
	pm->ps->weaponOffset[0] = LerpPosition( pm->ps->weaponOffset[0], weapOffsBlend[0], speedMult[0] +  (pm->ps->weapPosLerpFrac/1.25));
	pm->ps->weaponOffset[1] = LerpPosition( pm->ps->weaponOffset[1], weapOffsBlend[1], speedMult[1] +  (pm->ps->weapPosLerpFrac/1.25));
	pm->ps->weaponOffset[2] = LerpPosition( pm->ps->weaponOffset[2], weapOffsBlend[2], (speedMult[2]/2) +  (pm->ps->weapPosLerpFrac/1.25));
}


/*
================
PM_UpdateArticulatedAngles

This can be used as anothDer entry point when only the viewangles
are being updated isntead of a full move
================%

*/
void PM_UpdateArticulatedAngles( playerState_t *ps, const usercmd_t *cmd ) {	//angles that form the bounds, but follow the parent[i]
	int			i;
	float		pitchBoundMult = .75;
	float		oldYaw;
	float		forwardMult = ps->viewangles[0]/90; 
	float		upMult = (90 - fabs(ps->viewangles[0]))/90;
	int			gapMult = 1;
	vec3_t		vMat[3];
	vec3_t		velAddAngles;
	vec3_t		turnAddAngles;
	int			quad;
	int			testingParenting;
	float		sprintMultiplier;
	weaponhandling_t weaponShouldered;

	if ( ps->pm_type == PM_INTERMISSION || ps->pm_type == PM_SPINTERMISSION)  return;
	if ( ps->pm_type != PM_SPECTATOR && ps->stats[STAT_HEALTH] <= 0 )	return;  

	testingNewSprint = 1;
	leaning = qfalse;
	testingParenting = 0;
	armLength = 10; 

	/*
		document all types of weapon handling and body positioning and make a chart of how they should work
		
		weapon and movement positioning should be influenced by multiple keypresses?
			press right click to center weapon on screen can only do this in air or standing still
								
				on min gap shoulder 
				on max one handed move

				when holding down right hand or left hand ???
				when holding down max gap?

				The function should first take care of variables that influence everything else, then make subsequent calculations 
	*/

	weaponShouldered = PM_CheckWeaponHandling( ps );
	PM_SetLastBodyStance(ps);
	PM_UpdateViewAngles(ps, &pm->cmd);
	PM_GetSprintMultiplier(ps);


	VectorCopy( baseWeapOffset, weapOffsBlend);

	//if(!((ps->pm_weapFlags & PWF_WEAPONRIGHT) && (ps->pm_weapFlags & PWF_WEAPONLEFT))){ I dont think this is ever used
	//	hand = 0;
	//	if(ps->pm_weapFlags & PWF_WEAPONRIGHT){
	//		Com_Printf("righthand"); 
	//		weapOffsBlend[1] = maxWeapPos[1];
	//		hand = 1;
	//	}
	//
	//	if (ps->pm_weapFlags & PWF_WEAPONLEFT){
	//		Com_Printf("lefthand");
	//		weapOffsBlend[1] = minWeapPos[1];
	//		hand = -1;
	//	}
	//}

	weapAngBlend[2] = 0;
	if(leaning){
		//sprintAddAng = 1 * pm->weapViewGap[1];
		//weapAngBlend[2] = upMult * (-2.5 * pm->weapViewGap[1]);
		// adjust viewoffset 1
	}

	if(pm->ps->pm_weapFlags & PWF_WEAPONRIGHT){
		// if youi're holding this and it's already on one hand, do something extra?
		//weapAngBlend[0] = 45;
		//viewAngBlend[2] = 45;
	}
	if(pm->ps->pm_weapFlags & PWF_WEAPONLEFT){
		//weapAngBlend[0] = -45;
		//viewAngBlend[2] = -45;
	}

////weapon translation   
	if((ps->pm_weapFlags & PWF_WEAPONLEFT) && (ps->pm_weapFlags & PWF_WEAPONRIGHT)){
		ps->pm_weapFlags &= ~PWF_WEAPONLEFT;
		ps->pm_weapFlags &= ~PWF_WEAPONRIGHT;
		if(fabs(ps->weaponOffset[1]) > 0){
		} else if (fabs(ps->weaponOffset[1]) < 0) {
		}
	}

	//set all of the caps so that they are interpolated as well
	if(weapOffsBlend[0] < minWeapPos[0] - armLength){
		weapOffsBlend[0] = minWeapPos[0] - armLength;
	}

	if(weapOffsBlend[0] > maxWeapPos[0] + armLength && !(ps->pm_weapFlags & PWF_WEAPONUP)){
		weapOffsBlend[0] = maxWeapPos[0] + armLength;
	}

	if(weapOffsBlend[1] < minWeapPos[1] - armLength){
		ps->pm_weapFlags &= ~PWF_WEAPONLEFT;
		weapOffsBlend[1] = minWeapPos[1] - armLength;
	}

	if(weapOffsBlend[1] > maxWeapPos[1] + armLength){
		ps->pm_weapFlags &= ~PWF_WEAPONRIGHT;
		weapOffsBlend[1] = maxWeapPos[1] + armLength;
	}
	 
	if(weapOffsBlend[2] < minWeapPos[2] - armLength){
		weapOffsBlend[2] = minWeapPos[2] - armLength;
	}

	if(weapOffsBlend[2] > maxWeapPos[2] + armLength){
		weapOffsBlend[2] = maxWeapPos[2] + armLength;
	}

	LerpWeaponAngles(ps);
	LerpViewAngles(ps);

	oldAim = 0;
	//oldAim = 1;
	if(oldAim){
		VectorCopy(ps->weaponAngles, ps->viewangles);	 
	}

	for(i = 0; i < 3; i++){		
		if(ps->viewangles[i] >= 360){
			ps->viewangles[i] -= 360;
		}
		if(ps->viewangles[i] <= -360){
			ps->viewangles[i] += 360;
		}
		if(ps->weaponAngles[i] >= 360){
			ps->weaponAngles[i] -= 360;
		}
		if(ps->weaponAngles[i] <= -360){
			ps->weaponAngles[i] += 360;
		}
	}
}


 
void PM_ImpactViewItem( playerState_t * ps){
	int			i;
	float		armRigidity;
	float		impactMult;
	vec3_t		impactDir;
	float		pushBack;
	vec3_t		offset;
	vec3_t		spreadOffsFrac;
	vec3_t		weapForward, weapRight, weapUp;
	vec3_t		viewForward, viewRight, viewUp;
	float		sightsPosSpread = .25;
	float		standingPosSpread = .75;
	float		airPosSpread = 1.25;
	float		sprintPosSpread = 1.5;
	float		curAngSpread;
	int			vr;
	float		diffLerp;
	float		diffMult;
	float		diffOffs;
	vec3_t		leanAddOffset;
	vec3_t		viewOffs;
	vec3_t		speedMult;
	float		upMult =   (pm->ps->viewangles[0]/90);
	float		forwardMult = (90 - fabs(pm->ps->viewangles[0]))/90;
	float		curPosSpread = 1;
	float		bounceScale = 1;
	int			r, f, u;
	
	AngleVectors(pm->ps->weaponAngles, weapForward, weapRight, weapUp);
	AngleVectors(pm->ps->viewangles, viewForward, viewRight, viewUp);

	VectorClear(viewOffs);
	VectorClear(leanAddOffset);

	//Com_Printf( "%f  %f  %f\n", weapOffsBlend[0], weapOffsBlend[1], weapOffsBlend[2] );

	viewOffsBlend[0] = 26;
	viewOffsBlend[1] = 0;
	viewOffsBlend[2] = 0;
	
	if((ps->pm_weapFlags & PWF_WEAPONLEFT) && (ps->pm_weapFlags & PWF_WEAPONRIGHT)){
		ps->pm_weapFlags &= ~PWF_WEAPONLEFT;
		ps->pm_weapFlags &= ~PWF_WEAPONRIGHT;
		if(fabs(ps->weaponOffset[1]) > 0){
		} else if (fabs(ps->weaponOffset[1]) < 0) {
		}
	}

	//try to stop view weapon from pushing halfway across the screen when turning
	if(pm->ps->pm_weapFlags & PWF_WEAPONLEFT){
		if(weapOffsBlend[1] >= -armLength){
			pushBack = -(-armLength -  weapOffsBlend[1]);
			weapOffsBlend[0] -= pushBack;
			weapOffsBlend[1] = -armLength;	
		}
	}

	if(pm->ps->pm_weapFlags & PWF_WEAPONRIGHT){
		if(weapOffsBlend[1] <= armLength){
			pushBack = armLength -  weapOffsBlend[1];
			weapOffsBlend[0] -= pushBack;
			weapOffsBlend[1] = armLength;
		}
	} 

	vr = 0;
	 
	//offset position by view
	//sprinting
	if(pm->ps->pm_flags & PMF_SPRINT){
		if(!testingNewSprint){
			diffLerp = .1;
			diffMult = 1;
			bounceScale = .25;

			curPosSpread *= sprintPosSpread;
			//while sprinting move to this position and ...
			weapOffsBlend[0] = 7;
			weapOffsBlend[2] = -5;
		} else {
			bounceScale = .15;
			diffLerp = .1;
			diffMult = 1;
			weapOffsBlend[0] = baseWeapOffset[0] + 12;
			weapOffsBlend[2] = -7 - (sin(DEG2RAD(-pm->ps->viewangles[0]))*7); //second part offsets depending on if you're looking up or down
		}

	} else if (pm->ps->pm_weapFlags & PWF_WEAPONUP){
		diffLerp = .1;
		diffMult = .25;
		bounceScale = .075;

		//offset horizontal to eye
		if(vr){
			if(weapOffsBlend[1] > tempSightsOffset[1] + 3.75){ //should be replaced with some eye separation variable
				weapOffsBlend[1] = tempSightsOffset[1] + 3.75;
			}
			if(weapOffsBlend[1] < tempSightsOffset[1] - 3.75){
				weapOffsBlend[1] = tempSightsOffset[1] - 3.75;
			}
		} else {
				weapOffsBlend[1] = tempSightsOffset[1];
		}


		//offset vertical
		if(weapOffsBlend[0] > tempSightsOffset[0] + 1){
			weapOffsBlend[0] = tempSightsOffset[0] + .1;
		}

		if(weapOffsBlend[0] < tempSightsOffset[0] - 1){
			weapOffsBlend[0] = tempSightsOffset[0] - .1;
		}

		weapOffsBlend[2] = tempSightsOffset[2];

		if(fabs(pm->ps->weaponOffset[2] - tempSightsOffset[2]) < .01){
			weapOffsBlend[2] = tempSightsOffset[2];
		}
	} /* else if (ps->pm_weapFlags & PMF_SPRINT) && (ps->pm_weapFlags & PWF_WEAPONUP){//should be a position here for running and having sights. not near same speed as sprinting, but a real stamina eater.
	}*/ else {	 
		bounceScale = .15;
		diffLerp = .1;
		diffMult = 1;
		weapOffsBlend[0] = baseWeapOffset[0] + 12;
		weapOffsBlend[2] = -7 - (sin(DEG2RAD(-pm->ps->viewangles[0]))*7); //second part offsets depending on if you're looking up or down
	}

	//if(leaning){
	//	//leanAddOffset[0] = -5;
	//	//leanAddOffset[1] =  0 * hand;
	//	//leanAddOffset[2] = -25;
	//} 

	if( pm->ps->groundEntityNum == ENTITYNUM_NONE){
		bounceScale *= 2;
		curPosSpread *= airPosSpread;
	} else {
		curPosSpread *= standingPosSpread;
		if(pm->ps->pm_flags & PMF_TIME_LAND){
			impactMult =  -pml.previous_velocity[2];
		} else {
			impactMult =  pml.previous_velocity[2]/900;
		} 
	}

	if( oldAim || PM_CheckWeaponHandling( ps ) == WEAPON_SHOULDERED ){ 
		weapOffsBlend[0] = baseWeapOffset[0];
		weapOffsBlend[1] *= .5;
		weapOffsBlend[2] = baseWeapOffset[2];
	}

	//this could actually be a way to adjust the weapon position with movement jumping and crouching
	f = pm->cmd.forwardmove/127 *	.1;
	r = pm->cmd.rightmove/127 *		.1;
	u = pm->cmd.upmove/127 *		1;

	VectorClear(spreadOffsFrac);
	VectorMA( spreadOffsFrac,	curPosSpread,		weapForward,	spreadOffsFrac);
	VectorMA( spreadOffsFrac,	curPosSpread,		weapRight,		spreadOffsFrac);
	VectorMA( spreadOffsFrac,	curPosSpread,		weapUp,			spreadOffsFrac); 

	VectorClear(viewOffs);
	VectorMA( viewOffs,		viewOffsBlend[0],		viewForward,	viewOffs);
	VectorMA( viewOffs,		viewOffsBlend[1],		viewRight,		viewOffs);
	VectorMA( viewOffs,		viewOffsBlend[2],		viewUp,			viewOffs);
	VectorCopy(viewOffs,	viewOffsBlend);	 
	VectorClear(viewOffs);

	VectorClear(offset);
	VectorAdd(leanAddOffset, weapOffsBlend, weapOffsBlend);
	VectorMA( offset,	weapOffsBlend[0],		weapForward,	offset);
	VectorMA( offset,	weapOffsBlend[1],		weapRight,		offset);
	VectorMA( offset,	weapOffsBlend[2],		weapUp,			offset);
	VectorCopy(offset, weapOffsBlend);	 
	VectorClear(offset);

	//offset position by body alignment
	//if not walking offset from body
	if(PM_CheckWeaponHandling(ps) != WEAPON_SHOULDERED){
		if(!oldAim){
			if(pm->ps->pm_flags & PMF_SPRINT){ 
				
				//this shouldn't be here but it stopped working when i started animating viewpos
				weapOffsBlend[2] += DEFAULT_VIEWHEIGHT - 10;
				armRigidity = 200;

				if(!testingNewSprint){
				}
			} else if (pm->ps->pm_weapFlags & PWF_WEAPONUP){

				weapOffsBlend[2] += DEFAULT_VIEWHEIGHT/* - 6*/;
				armRigidity = 10000;
			} /* else if (ps->pm_weapFlags & PMF_SPRINT) && (ps->pm_weapFlags & PWF_WEAPONUP){//should be a position here for running and having sights. not near same speed as sprinting, but a real stamina eater.
			}*/ else {	
				weapOffsBlend[2] += DEFAULT_VIEWHEIGHT - 10;
				armRigidity = 200;
			}
		}
	} else { 
		//if you are walking offset it from the view
		weapOffsBlend[2] += DEFAULT_VIEWHEIGHT;
		if(pm->ps->groundEntityNum == ENTITYNUM_NONE){
			armRigidity = -1000;
		} else {
			armRigidity = -205;
		}
	} 

	if(oldAim){
		weapOffsBlend[2] += DEFAULT_VIEWHEIGHT;
	}

	if(pm->cmd.upmove < 0){
		//pm->ps->weapPosSpread[2] = CROUCH_VIEWHEIGHT - 29.28;
		pm->ps->weaponOffset[2] =  CROUCH_VIEWHEIGHT - 29.28;
	}

	/*float GetImpactDir(playerState_t * ps){

	}*/
	impactDir[0] =  -1.5 * pm->ps->velocity[0]/armRigidity;
	impactDir[1] =  -1.5 * pm->ps->velocity[1]/armRigidity;
	impactDir[2] =  -1	* pm->ps->velocity[2]/armRigidity;

	for( i = 0; i < 3; i++){
		if( impactDir[i] > armLength){
			impactDir[i] = armLength;
		} else if (impactDir[i] < -armLength){
			impactDir[i] = -armLength;
		}
	}


	for(i = 0; i < 3; i++){
		float mult = .015;
		float diff = pm->ps->velocity[i] - pml.previous_velocity[i];

		if(i == 2){
			mult *= 2 * diffMult;
		} else {
			mult *= diffMult;
		}
		pm->ps->weapPosSpread[i] += -mult * (diff);
	}
	//

	VectorClear(offset);
	VectorSubtract(weapOffsBlend,  pm->ps->weaponOffset, offset);
	VectorScale(offset, bounceScale, offset);
	VectorAdd(offset, pm->ps->weapPosSpread, pm->ps->weapPosSpread);
	

	//if(pm->cmd.buttons & BUTTON_WALKING){
	//	pm->ps->weapPosSpread[2] = 10;
	//}

	//fix the weird gap thing while in the air
	diffLerp = .1;
	//Com_Printf("difflerp: %f\n", diffLerp);
	pm->ps->weapPosSpread[0] = LerpPosition( pm->ps->weapPosSpread[0], 0, diffLerp);
	pm->ps->weapPosSpread[1] = LerpPosition( pm->ps->weapPosSpread[1], 0, diffLerp);
	pm->ps->weapPosSpread[2] = LerpPosition( pm->ps->weapPosSpread[2], 0, diffLerp); 
	
	if(!oldAim){
		VectorAdd(impactDir, weapOffsBlend, weapOffsBlend);
		VectorAdd( pm->ps->weapPosSpread, weapOffsBlend, weapOffsBlend); //probably behaves strangely because the last frametime is different from current
	} else {
	
		pm->ps->weapPosLerpFrac = 1;
	}

	speedMult[0] = fabs(pm->ps->velocity[0]/160);
	speedMult[1] = fabs(pm->ps->velocity[1]/160);
	speedMult[2] = fabs(pm->ps->velocity[2]/160);

	for(i = 0; i <3; i++){

		if(speedMult[i] > 1){
			speedMult[i] = 1;
		} else if(speedMult[i] < 0) {
			speedMult[i] = 0;
		}
		speedMult[i] = 1 - speedMult[i];
		speedMult[i] *= speedMult[i];
		speedMult[i]/= 5;
	}


	LerpWeaponOffset( ps, speedMult );
	
	/*if(pm->cmd.upmove < 0){
		pm->ps->weaponOffset[2] -=  (DEFAULT_VIEWHEIGHT - CROUCH_VIEWHEIGHT);
	} else {
		pm->ps->weaponOffset[2] =  0;
	}*/
}



static void PM_ArticulateWeapon(vec3_t angles) {
	float	scale;
	float	fracsin;
	float	flatSpeed, bobFrac;
	float	phase;
	float	spreadfrac;
	float	yawDiff, pitchDiff;
	int		delta;
	int		bobCycle;
	int		i;
	vec3_t oldAng;

	flatSpeed = sqrt(( pm->ps->velocity[0] *  pm->ps->velocity[0]) + (pm->ps->velocity[1] * pm->ps->velocity[1]) );

	bobCycle = ( pm->ps->bobCycle & 128 ) >> 7;
	bobFrac = fabs( sin( ( pm->ps->bobCycle & 127 ) / 127.0 * M_PI ) );

	if ( bobCycle & 1 ) {
		scale = -flatSpeed;
	} else {
		scale = flatSpeed;
	}
 
	//pitchDiff = pm->ps->viewangles[0] - pm->oldViewAng[0];
	//yawDiff= pm->ps->viewangles[1] - pm->oldViewAng[1];

//DOESN'T WORK RIGHT NOW.
	//angles[PITCH] += pitchDiff * 4;
	//angles[YAW] += yawDiff * 4;

	//angles[ROLL] += scale * bobFrac * 0.005;
	//angles[YAW] += scale * bobFrac * 0.01;
	//angles[PITCH] += flatSpeed * bobFrac * 0.005;

//DOESN'T WORK RIGHT NOW.


	//the beginning stage of pm->deadZone aiming

	// DROP THE WEAPON WHEN LANDING //DON'T DO THESE YET DO THEM LATER PLEASE
	//CAUSE IT'S COOL
	//delta = cg.time - cg.landTime;
	//if ( delta < LAND_DEFLECT_TIME ) {
	//	origin[2] += cg.landChange*0.25 * delta / LAND_DEFLECT_TIME;
	//} else if ( delta < LAND_DEFLECT_TIME + LAND_RETURN_TIME ) {
	//	origin[2] += cg.landChange*0.25 * 
	//		(LAND_DEFLECT_TIME + LAND_RETURN_TIME - delta) / LAND_RETURN_TIME;
	//}

#if 0
	// DROP THE WEAPON WHEN STAIR CLIMBING
	//delta = cg.time - cg.stepTime;
	//if ( delta < STEP_TIME/2 ) {
	//	origin[2] -= cg.stepChange*0.25 * delta / (STEP_TIME/2);
	//} else if ( delta < STEP_TIME ) {
	//	origin[2] -= cg.stepChange*0.25 * (STEP_TIME - delta) / (STEP_TIME/2);
	//}
#endif

	// idle drift
	//if(pm->cmd.buttons & BUTTON_ATTACK ){ //need to do a lot to get zoom to work
		spreadfrac = 5 + (flatSpeed/50);
			  
		//rand()%90 doesn't work perfeclty for making all players on server different
		phase = (pm->cmd.serverTime) / 1000.0 * SWAY_PITCH_FREQUENCY * M_PI * 2;
		angles[PITCH] += SWAY_PITCH_AMPLITUDE * sin( phase ) * ( spreadfrac + SWAY_PITCH_MIN_AMPLITUDE );
		phase = (pm->cmd.serverTime) / 1000.0 * SWAY_YAW_FREQUENCY * M_PI * 2;
		angles[YAW] += SWAY_YAW_AMPLITUDE * sin( phase ) * ( spreadfrac + SWAY_YAW_MIN_AMPLITUDE );
	//}
	//Com_Printf("ang %f %f %f  \n",angles[0],angles[1],angles[2]); //ZCM track accuracy between game/cgame modules
}

/*
================
PmoveSingle

================
*/
void trap_SnapVector( float *v );

void PmoveSingle (pmove_t *pmove) {
	short   temp;
	int		i;
	pm = pmove;

	// this counter lets us debug movement problems with a journal
	// by setting a conditional breakpoint fot the previous frame
	c_pmove++;

	// clear results
	pm->numtouch = 0;
	pm->watertype = 0;
	pm->waterlevel = 0;

	if ( pm->ps->stats[STAT_HEALTH] <= 0 ) {
		pm->tracemask &= ~CONTENTS_BODY;	// corpses can fly through bodies
		pm->ps->zoomed = 0;
	}

	// make sure walking button is clear if they are running, to avoid
	// proxy no-footsteps cheats
	if ( abs( pm->cmd.forwardmove ) > 64 || abs( pm->cmd.rightmove ) > 64 ) {
		pm->cmd.buttons &= ~BUTTON_WALKING;
	}

	// set the talk balloon flag
	if ( pm->cmd.buttons & BUTTON_TALK ) {
		pm->ps->eFlags |= EF_TALK;
	} else {
		pm->ps->eFlags &= ~EF_TALK;
	}

	// set the firing flag for continuous beam weapons
	if ( !(pm->ps->pm_flags & PMF_RESPAWNED) && pm->ps->pm_type != PM_INTERMISSION
		&& ( pm->cmd.buttons & BUTTON_ATTACK ) && pm->ps->ammo[ pm->ps->weapon ] ) {
		pm->ps->eFlags |= EF_FIRING;
	} else {
		pm->ps->eFlags &= ~EF_FIRING;
	}

	// clear the respawned flag if attack and use are cleared
	if ( pm->ps->stats[STAT_HEALTH] > 0 && 
		!( pm->cmd.buttons & (BUTTON_ATTACK | BUTTON_USE_HOLDABLE) ) ) {
		pm->ps->pm_flags &= ~PMF_RESPAWNED;
	}

	// if talk button is down, dissallow all other input
	// this is to prevent any possible intercept proxy from
	// adding fake talk balloons
	if ( pmove->cmd.buttons & BUTTON_TALK ) {
		// keep the talk button set tho for when the cmd.serverTime > 66 msec
		// and the same cmd is used multiple times in Pmove
		pmove->cmd.buttons = BUTTON_TALK;
		pmove->cmd.forwardmove = 0;
		pmove->cmd.rightmove = 0;
		pmove->cmd.upmove = 0;
	}

	// clear all pmove local vars
	memset (&pml, 0, sizeof(pml));

	// determine the time
	pml.msec = pmove->cmd.serverTime - pm->ps->commandTime;
	if ( pml.msec < 1 ) {
		pml.msec = 1;
	} else if ( pml.msec > 200 ) {
		pml.msec = 200;
	}
	pm->ps->commandTime = pmove->cmd.serverTime;

	// save old org in case we get stuck
	// save old velocity for crashlanding
	// save old angles for angular velocity
	VectorCopy (pm->ps->origin, pml.previous_origin);
	VectorCopy (pm->ps->velocity, pml.previous_velocity);
	//VectorCopy (pm->ps->viewangles, ps->oldAngles);

	pml.frametime = pml.msec * 0.001;


	//Sights up
	if ( pm->cmd.buttons & BUTTON_AIM){
		pm->ps->pm_weapFlags |= PWF_WEAPONUP;
	} else {
		pm->ps->pm_weapFlags &= ~PWF_WEAPONUP;
	}

	//sprinting
	if ( pm->cmd.buttons & BUTTON_SPRINT ){
		pm->ps->pm_flags |= PMF_SPRINT;
	} else {
		pm->ps->pm_flags &= ~PMF_SPRINT;
	}
	
	//moving weapon left
	if ( pm->cmd.buttons & BUTTON_HAND_LEFT){
		pm->ps->pm_weapFlags |= PWF_WEAPONLEFT;
		pm->ps->lastHand = -1;

	} 

	//moving weapon right
	if ( pm->cmd.buttons & BUTTON_HAND_RIGHT ){
		pm->ps->pm_weapFlags |= PWF_WEAPONRIGHT;
		pm->ps->lastHand = 1;
	} 

	//unlocking aim from view or view from aim
	if ( pm->cmd.buttons & BUTTON_UNLOCK_AIM ){
		pm->ps->pm_weapFlags |= PWF_VIEWUNLOCK;
	} else {
		pm->ps->pm_weapFlags &= ~PWF_VIEWUNLOCK;
	}
	
	//switching parenting
	//if(pm->ps->module == 2){
		//if ( pm->cmd.buttons & BUTTON_AIM_SWITCH){
	//		if ( !(pm->ps->pm_weapFlags & PWF_PARENTSWITCH)){
	//			//	if( pm->ps->switchingParent == 1){
	//			//		pm->ps->switchingParent = 0;
	//			//	} else {
	//			//		pm->ps->switchingParent = 1;
	//			//	}
	//			//} else {
	//					pm->ps->switchingParent = 1;
	//			//}
	//			Com_Printf("%i %f: \n", pm->ps->module, pm->ps->switchingParent);
	//		} else {
	//			pm->ps->switchingParent = 0;
	//		}

		//	pm->ps->pm_weapFlags |= PWF_PARENTSWITCH;
		//} else {
	//		if ( (pm->ps->pm_weapFlags & PWF_PARENTSWITCH)){
	//			//if(pm->ps->module == 1){
	//			//	if( pm->ps->switchingParent == 1){
	//			//		pm->ps->switchingParent = 0;
	//			//	} else {
	//			//		pm->ps->switchingParent = 1;
	//			//	}
	//			//} else {
	//					pm->ps->switchingParent = 1;
	//			//}
	//			Com_Printf("%i %f: \n", pm->ps->module,  pm->ps->switchingParent);
	//		} else {
	//			pm->ps->switchingParent = 0;
	//		}

		//	pm->ps->pm_weapFlags &= ~PWF_PARENTSWITCH;
		//}
	//}



	if ( pm->cmd.buttons & BUTTON_GAPUP){
		pm->ps->pm_weapFlags |= PWF_GAPUP;
	} else {
		pm->ps->pm_weapFlags &= ~PWF_GAPUP;
	}

	if ( pm->cmd.buttons & BUTTON_GAPDOWN){
		pm->ps->pm_weapFlags |= PWF_GAPDOWN;
	} else {
		pm->ps->pm_weapFlags &= ~PWF_GAPDOWN;
	}


	// update the viewangles  
	//if(pm->ps->pm_flags & PMF_SPRINT){ 
	//	AngleVectors (pm->ps->viewangles, pml.forward, pml.right, pml.up);	
	//} else {  
	//	AngleVectors (pm->ps->weaponAngles, pml.forward, pml.right, pml.up);	
	//} 

	if(pm->ps->maxBaseGap >= maxTwoHandedGap){
		AngleVectors (pm->ps->viewangles, pml.forward, pml.right, pml.up);	
	} else {
		AngleVectors (pm->ps->weaponAngles, pml.forward, pml.right, pml.up);	
	}

	
	PM_CalcPlayerStance( pm->ps, &pm->cmd );

	if ( !(pm->ps->pm_flags & PMF_SPRINT )) {  
		PM_UpdateArticulatedAngles( pm->ps, &pm->cmd );
	} 

	//NEEDs to be handled differently. idle anim shouldn't play while moving, aiming, or airborne
	//PM_ArticulateWeapon(pm->ps->weaponAngles);

	if ( pm->cmd.upmove < 10 ) {
		// not holding jump
		pm->ps->pm_flags &= ~PMF_JUMP_HELD;
	}

	// decide if backpedaling animations should be used
	if ( pm->cmd.forwardmove < 0 ) {
		pm->ps->pm_flags |= PMF_BACKWARDS_RUN;
	} else if ( pm->cmd.forwardmove > 0 || ( pm->cmd.forwardmove == 0 && pm->cmd.rightmove ) ) {
		pm->ps->pm_flags &= ~PMF_BACKWARDS_RUN;
	}

	if ( pm->ps->pm_type >= PM_DEAD ) {
		pm->cmd.forwardmove = 0;
		pm->cmd.rightmove = 0;
		pm->cmd.upmove = 0;
	}

	if ( pm->ps->pm_flags & PMF_SPRINT ) {
		PM_SprintMove(); 
	}

	//Com_Printf("floatName: %i ", pm->cmd.buttons );

	//if ( pm->cmd.buttons & BUTTON_SPRINT){
	//	//Com_Printf("true");
	//	PM_SprintMove(); 
	//ent->client->ps.pm_flags ^= PMF_SPRINT;
	//}

	if ( pm->ps->pm_type == PM_SPECTATOR ) {
		PM_CheckDuck ();
		PM_FlyMove ();
		PM_DropTimers ();
		return;
	}

	if ( pm->ps->pm_type == PM_NOCLIP ) {
		PM_NoclipMove ();
		PM_DropTimers ();
		return;
	}

	if (pm->ps->pm_type == PM_FREEZE) {
		return;		// no movement at all
	}

	if ( pm->ps->pm_type == PM_INTERMISSION || pm->ps->pm_type == PM_SPINTERMISSION) {
		return;		// no movement at all
	}

	// set watertype, and waterlevel
	PM_SetWaterLevel();
	pml.previous_waterlevel = pmove->waterlevel;

	// set mins, maxs, and viewheight
	PM_CheckDuck ();

	// set groundentity
	PM_GroundTrace();

	if ( pm->ps->pm_type == PM_DEAD ) {
		PM_DeadMove ();
	}

	PM_DropTimers();

	if (pm->ps->pm_flags & PMF_GRAPPLE_PULL) {
		Com_Printf("test");
		PM_GrappleMoveTarzan();
	}
// CPM: Double-jump timer
	if (pm->ps->stats[STAT_JUMPTIME] > 0) pm->ps->stats[STAT_JUMPTIME] -= pml.msec;
// !CPM

#ifdef MISSIONPACK
	if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
		PM_InvulnerabilityMove();
	} else
#endif
	if ( pm->ps->powerups[PW_FLIGHT] ) {
		// flight powerup doesn't allow jump and has different friction
		PM_FlyMove();
	} else if (pm->ps->pm_flags & PMF_GRAPPLE_PULL) {
		//PM_GrappleMove();
		PM_GrappleMoveTarzan();
		// We can wiggle a bit
		PM_AirMove();
	} else if (pm->ps->pm_flags & PMF_TIME_WATERJUMP) {
		PM_WaterJumpMove();
	} else if ( pm->waterlevel > 1 ) {
		// swimming
		PM_WaterMove();
	} else if ( pml.walking ) {
		// walking on ground
		PM_WalkMove();
	} else {
		// airborne
		PM_AirMove();
	}

	PM_Animate();

	// set groundentity, watertype, and waterlevel
	PM_GroundTrace(); 

	if ( !(pm->ps->pm_flags & PMF_SPRINT )) { 
		PM_ImpactViewItem( pm->ps );
	} else {
		pm->ps->viewangles[0] = 0;
		pm->ps->viewangles[1] = 0;
		pm->ps->viewangles[2] = 0;
		pm->ps->weaponAngles[0] = 0;
		pm->ps->weaponAngles[1] = 0;
		pm->ps->weaponAngles[2] = 0;
		//pm->ps->origin[0] = -200;
		//pm->ps->origin[1] = 0;
		//pm->ps->origin[2] = 50;
		pm->ps->weaponOffset[0] = 50;
		pm->ps->weaponOffset[1] = -0;
		pm->ps->weaponOffset[2] = -25;


		if ( pm->ps->pm_type == PM_INTERMISSION || pm->ps->pm_type == PM_SPINTERMISSION) {
			return;		// no view changes at all
		}

		if ( pm->ps->pm_type != PM_SPECTATOR && pm->ps->stats[STAT_HEALTH] <= 0 ) {
			return;		// no view changes at all
		}

		// circularly clamp the angles with deltas
		for ( i = 0 ; i < 3 ; i++ ) {
			temp = pm->cmd.angles[i] + pm->ps->delta_angles[i];
			if ( i == PITCH ) {
				// don't let the player look up or down more than 90 degrees
				if ( temp > 16000 ) {
					pm->ps->delta_angles[i] = 16000 - pm->cmd.angles[i];
					temp = 16000;
				} else if ( temp < -16000 ) {
					pm->ps->delta_angles[i] = -16000 - pm->cmd.angles[i];
					temp = -16000;
				}
			}
			pm->ps->viewangles[i] = SHORT2ANGLE(temp);
		}
	}


	PM_SetWaterLevel();

	// weapons
	PM_Weapon();
	//PM_WeaponLeft();
	//PM_WeaponRight();

	// torso animation
	PM_TorsoAnimation();

	// footstep events / legs animations
	PM_Footsteps();

	// entering / leaving water splashes
	PM_WaterEvents();

	// snap some parts of playerstate to save network bandwidth
	trap_SnapVector( pm->ps->velocity );
}


/*
================
Pmove

Can be called by either the server or the client
================
*/
void Pmove (pmove_t *pmove) {
	int			finalTime;

	finalTime = pmove->cmd.serverTime;

	if ( finalTime < pmove->ps->commandTime ) {
		return;	// should not happen
	}

	if ( finalTime > pmove->ps->commandTime + 1000 ) {
		pmove->ps->commandTime = finalTime - 1000;
	}

	pmove->ps->pmove_framecount = (pmove->ps->pmove_framecount+1) & ((1<<PS_PMOVEFRAMECOUNTBITS)-1);

	// chop the move up if it is too long, to prevent framerate
	// dependent behavior
	while ( pmove->ps->commandTime != finalTime ) {
		int		msec;

		msec = finalTime - pmove->ps->commandTime;

		if ( pmove->pmove_fixed ) {
			if ( msec > pmove->pmove_msec ) {
				msec = pmove->pmove_msec;
			}
		}
		else {
			if ( msec > 66 ) {
				msec = 66;
			}
		}
		pmove->cmd.serverTime = pmove->ps->commandTime + msec;
		PmoveSingle( pmove );

		if ( pmove->ps->pm_flags & PMF_JUMP_HELD ) {
			pmove->cmd.upmove = 20;
		}
	}

	//PM_CheckStuck();

}

