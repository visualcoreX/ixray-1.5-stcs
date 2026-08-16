#include "stdafx.h"

#include "actor.h"
#include "inventory.h"
#include "weapon.h"
#include "../xrEngine/CameraBase.h"
#include "xrMessages.h"

#include "level.h"
#include "HUDManager.h"
#include "UI.h"
#include "string_table.h"
#include "actorcondition.h"
#include "game_cl_base.h"
#include "WeaponMagazined.h"
#include "CharacterPhysicsSupport.h"
#include "actoreffector.h"
#include "static_cast_checked.hpp"

#ifdef DEBUG
#include "phdebug.h"
#endif
// How long the landing state is held. It is not only a timer: while it lasts the legs play the
// landing cycle, and the moment it ends the movement cycle takes over. At the stock 0.1s a landing
// taken while walking was cut after 100ms -- the character barely touched the ground before walking
// on, and the footstep mark for norm_jump_end (0.2, stalker_step_manager) was never reached, so the
// landing sound went missing with it. Standing still hid the bug: with no movement cycle to replace
// it, the landing animation simply played on.
// RAISING THIS IS NOT THE FIX: the same state also drives the camera dip (ActorCameras) and the
// hud offset hud_move_landing_offset (player_hud.cpp:1302), so a longer landing holds the hands
// down far longer than a normal jump should -- it reads as broken first-person inertia. Left at
// the stock value; the legs need their own hold, separate from this state.
// Console: actor_landing_time / actor_landing_time_hard.
float	s_fLandingTime1		= 0.1f;		// soft landing (no damage taken)
// How long the LEGS hold the landing cycle, independent of the state above. Long enough to reach
// the footstep mark of norm_jump_end (0.2 in stalker_step_manager), which is what makes a landing
// audible, and long enough to actually see the animation from third person.
float	s_fLegsLandingHold	= 0.3f;		// console: actor_landing_legs_time
float	s_fLandingTime2		= 0.3f;		// hard landing (damage taken)
static const float	s_fJumpTime			= 0.3f;
static const float	s_fJumpGroundTime	= 0.1f;	// ��� ������ ������ Jump ���� �� �����
	   const float	s_fFallTime			= 0.2f;

IC static void generate_orthonormal_basis1(const Fvector& dir,Fvector& updir, Fvector& right)
{

	right.crossproduct(dir,updir); //. <->
	right.normalize();
	updir.crossproduct(right,dir);
}


void CActor::g_cl_ValidateMState(float dt, u32 mstate_wf)
{
	// Lookout
	if (mstate_wf&mcLookout)	mstate_real		|= mstate_wf&mcLookout;
	else						mstate_real		&= ~mcLookout;
	
	if (mstate_real&(mcJump|mcFall|mcLanding|mcLanding2))
		mstate_real		&= ~mcLookout;

	// ��������� �����������
	if (m_fLegsLandingHold > 0.f)	m_fLegsLandingHold -= dt;
	if (mstate_real&(mcLanding|mcLanding2)){
		m_fLandingTime		-= dt;
		if (m_fLandingTime<=0.f){
			mstate_real		&=~	(mcLanding|mcLanding2);
			mstate_real		&=~	(mcFall|mcJump);
		}
	}
	// ��������� �������
	if (character_physics_support()->movement()->gcontact_Was){
		if (mstate_real&mcFall){
			if (character_physics_support()->movement()->GetContactSpeed()>4.f){
				if (fis_zero(character_physics_support()->movement()->gcontact_HealthLost)){	
					m_fLandingTime	= s_fLandingTime1;
					m_uLegsLandingIdx = 0;
					mstate_real		|= mcLanding;
				}else{
					m_fLandingTime	= s_fLandingTime2;
					m_uLegsLandingIdx = 1;
					mstate_real		|= mcLanding2;
				}
				// The legs get their own, longer hold: the state above is over in 0.1s and the walk
				// cycle would replace the landing animation before it has played -- taking the footstep
				// mark, and with it the landing sound, along with it.
				m_fLegsLandingHold	= s_fLegsLandingHold;
			}
		}
		m_bJumpKeyPressed	=	TRUE;
		m_fJumpTime			=	s_fJumpTime;
		mstate_real			&=~	(mcFall|mcJump);
	}
	if ((mstate_wf&mcJump)==0)	
		m_bJumpKeyPressed	=	FALSE;

	// ������-�� ����/������ - �� ��������
	if (((character_physics_support()->movement()->GetVelocityActual()<0.2f)&&(!(mstate_real&(mcFall|mcJump)))) || character_physics_support()->movement()->bSleep) 
	{
		mstate_real				&=~ mcAnyMove;
	}
	if (character_physics_support()->movement()->Environment()==CPHMovementControl::peOnGround || character_physics_support()->movement()->Environment()==CPHMovementControl::peAtWall)
	{
		// ���� �� ����� �������������� ������� ������ Jump
		if (((s_fJumpTime-m_fJumpTime)>s_fJumpGroundTime)&&(mstate_real&mcJump))
		{
			mstate_real			&=~	mcJump;
			m_fJumpTime			= s_fJumpTime;
		}
	}
	if(character_physics_support()->movement()->Environment()==CPHMovementControl::peAtWall)
	{
		if(!(mstate_real & mcClimb))
		{
			mstate_real				|=mcClimb;
			mstate_real				&=~mcSprint;
			cam_SetLadder();
		}
	}
	else
	{
		if (mstate_real & mcClimb)
		{
			cam_UnsetLadder();
		}
		mstate_real				&=~mcClimb;		
	};

	if (mstate_wf != mstate_real){
		if ((mstate_real&mcCrouch)&&((0==(mstate_wf&mcCrouch)) || mstate_real&mcClimb)){
			if (character_physics_support()->movement()->ActivateBoxDynamic(0)){
				mstate_real &= ~mcCrouch;
			}
		}
	}

	if(!CanAccelerate()&&isActorAccelerated(mstate_real, IsZoomAimingMode()))
	{
		mstate_real				^=mcAccel;
	};	

	if (this == Level().CurrentControlEntity())
	{
		bool bOnClimbNow			= !!(mstate_real&mcClimb);
		bool bOnClimbOld			= !!(mstate_old&mcClimb);

		if (bOnClimbNow != bOnClimbOld )
		{
			SetWeaponHideState		(INV_STATE_LADDER, bOnClimbNow );
		};
	};
};

void CActor::g_cl_CheckControls(u32 mstate_wf, Fvector &vControlAccel, float &Jump, float dt)
{
	float					cam_eff_factor = 0.0f;
	mstate_old				= mstate_real;
	vControlAccel.set		(0,0,0);

	if (!(mstate_real&mcFall) && (character_physics_support()->movement()->Environment()==CPHMovementControl::peInAir)) 
	{
		m_fFallTime				-=	dt;
		if (m_fFallTime<=0.f)
		{
			m_fFallTime			=	s_fFallTime;
			mstate_real			|=	mcFall;
			mstate_real			&=~	mcJump;
		}
	}

	if(!CanMove()) 
	{
		if(mstate_wf&mcAnyMove) 
		{
			StopAnyMove();
			mstate_wf &= ~mcAnyMove;
			mstate_wf &= ~mcJump;
		}
	}

	// update player accel
	if (mstate_wf&mcFwd)		vControlAccel.z +=  1;
	if (mstate_wf&mcBack)		vControlAccel.z += -1;
	if (mstate_wf&mcLStrafe)	vControlAccel.x += -1;
	if (mstate_wf&mcRStrafe)	vControlAccel.x +=  1;

	CPHMovementControl::EEnvironment curr_env = character_physics_support()->movement()->Environment();
	if(curr_env==CPHMovementControl::peOnGround || curr_env==CPHMovementControl::peAtWall )
	{
		// crouch
		if ((0==(mstate_real&mcCrouch))&&(mstate_wf&mcCrouch))
		{
			if(mstate_real&mcClimb)
			{
				mstate_wf&=~mcCrouch;
			}
			else
			{
				character_physics_support()->movement()->EnableCharacter();
				bool Crouched = false;
				if(isActorAccelerated(mstate_wf, IsZoomAimingMode()))
					Crouched = character_physics_support()->movement()->ActivateBoxDynamic(1);
				else
					Crouched = character_physics_support()->movement()->ActivateBoxDynamic(2);
				
				if(Crouched) 
					mstate_real			|=	mcCrouch;
			}
		}
		// jump
		m_fJumpTime				-=	dt;

		if( CanJump() && (mstate_wf&mcJump) )
		{
			mstate_real			|=	mcJump;
			m_bJumpKeyPressed	=	TRUE;
			Jump				= m_fJumpSpeed;
			m_fJumpTime			= s_fJumpTime;


			//��������� ���� ������ ��-�� ����������� ������
			if (!GodMode())
				conditions().ConditionJump(inventory().TotalWeight() / MaxCarryWeight());
		}

		// mask input into "real" state
		u32 move	= mcAnyMove|mcAccel;

		if(mstate_real&mcCrouch)
		{
			if (!isActorAccelerated(mstate_real, IsZoomAimingMode()) && isActorAccelerated(mstate_wf, IsZoomAimingMode()))
			{
				character_physics_support()->movement()->EnableCharacter();
				if(!character_physics_support()->movement()->ActivateBoxDynamic(1))move	&=~mcAccel;
			}

			if (isActorAccelerated(mstate_real, IsZoomAimingMode()) && !isActorAccelerated(mstate_wf, IsZoomAimingMode()))
			{
				character_physics_support()->movement()->EnableCharacter();
				if(character_physics_support()->movement()->ActivateBoxDynamic(2))mstate_real	&=~mcAccel;
			}
		}

		if ((mstate_wf&mcSprint) && !CanSprint())
			mstate_wf				&= ~mcSprint;

		mstate_real &= (~move);
		mstate_real |= (mstate_wf & move);

		if(mstate_wf&mcSprint)
			mstate_real|=mcSprint;
		else
			mstate_real&=~mcSprint;
		if(!(mstate_real&(mcFwd|mcLStrafe|mcRStrafe))||mstate_real&(mcCrouch|mcClimb)|| !isActorAccelerated(mstate_wf, IsZoomAimingMode()))
		{
			mstate_real&=~mcSprint;
			mstate_wishful&=~mcSprint;
		}

		// smooth sprint acceleration ramp (0..1): while sprinting, ease up over m_fSprintAccelTime; reset the
		// instant we're not sprinting so each fresh sprint accelerates from run speed again (see the scale use).
		if(mstate_real&mcSprint)
		{
			m_fSprintRamp += (m_fSprintAccelTime>EPS) ? (dt/m_fSprintAccelTime) : 1.f;
			clamp(m_fSprintRamp, 0.f, 1.f);
		}
		else
			m_fSprintRamp = 0.f;

		// check player move state
		if(mstate_real&mcAnyMove)
		{
			BOOL	bAccelerated		= isActorAccelerated(mstate_real, IsZoomAimingMode())&&CanAccelerate();

			// correct "mstate_real" if opposite keys pressed
			if (_abs(vControlAccel.z)<EPS)	mstate_real &= ~(mcFwd+mcBack		);
			if (_abs(vControlAccel.x)<EPS)	mstate_real &= ~(mcLStrafe+mcRStrafe);

			// normalize and analyze crouch and run
			float	scale			= vControlAccel.magnitude();
			if(scale>EPS)	
			{
				scale	=	m_fWalkAccel/scale;
				if (bAccelerated)
					if (mstate_real&mcBack)
						scale *= m_fRunBackFactor;
					else
						scale *= m_fRunFactor;
				else
					if (mstate_real&mcBack)
						scale *= m_fWalkBackFactor;



				if (mstate_real&mcCrouch)	scale *= m_fCrouchFactor;
				if (mstate_real&mcClimb)	scale *= m_fClimbFactor;
				// ramp the sprint boost from 1x (run speed) up to m_fSprintFactor so top speed eases in
				if (mstate_real&mcSprint)	scale *= (1.f + (m_fSprintFactor - 1.f) * m_fSprintRamp);

				if (mstate_real&(mcLStrafe|mcRStrafe) && !(mstate_real&mcCrouch))
				{
					if (bAccelerated)
						scale *= m_fRun_StrafeFactor;
					else
						scale *= m_fWalk_StrafeFactor;
				}

				// GS GetCurrentSuicideWalkKoef: a controlled victim walks at controlled_actor_speed_koef
				scale						*= ControlledSpeedKoef();

				vControlAccel.mul			(scale);
				cam_eff_factor				= scale;
			}//scale>EPS
		}//(mstate_real&mcAnyMove)
	}//peOnGround || peAtWall

	// GS actor-move camera anims (GetActorCameraMovingAnim, ActorUtils.pas:3290): the vanilla
	// sprint/strafe/move selector extended with lookouts, crouch down/up, jump, fall, landing/landing2 --
	// each with an _aim variant when zoomed and a per-weapon override `cam_<name>` in the hud section;
	// per-category effector types so e.g. a landing shake can start over a running strafe effect.
	// Special (non-move) events use full factor and are NOT gated on cam_eff_factor (a straight-down
	// landing with no movement keys must still shake).
	if(IsGameTypeSingle())
	{
		LPCSTR state_anm			= NULL;
		ECamEffectorType eff_id		= eCEActorMoving;
		float factor				= cam_eff_factor/70.f;
		bool gated					= true;		// requires cam_eff_factor>EPS (vanilla movement gate)

		if(!(mstate_real&mcRLookout) || !(mstate_real&mcLLookout))
		{
			if(!(mstate_real&mcRLookout) && (mstate_wishful&mcRLookout))
				{ state_anm = "lookout_right_start";eff_id = eCEActorRLookoutStart;	factor = 1.f; gated = false; }
			else if((mstate_real&mcRLookout) && !(mstate_wishful&mcRLookout))
				{ state_anm = "lookout_right_end";	eff_id = eCEActorRLookoutEnd;	factor = 1.f; gated = false; }
			else if(!(mstate_real&mcLLookout) && (mstate_wishful&mcLLookout))
				{ state_anm = "lookout_left_start";	eff_id = eCEActorLLookoutStart;	factor = 1.f; gated = false; }
			else if((mstate_real&mcLLookout) && !(mstate_wishful&mcLLookout))
				{ state_anm = "lookout_left_end";	eff_id = eCEActorLLookoutEnd;	factor = 1.f; gated = false; }
		}
		if(!state_anm)
		{
			if(mstate_real&mcLStrafe && !(mstate_old&mcLStrafe))
				{ state_anm = "strafe_left";	eff_id = eCEActorMovingLeft; }
			else if(mstate_real&mcRStrafe && !(mstate_old&mcRStrafe))
				{ state_anm = "strafe_right";	eff_id = eCEActorMovingRight; }
			else if(mstate_real&mcFwd && !(mstate_old&mcFwd))
				{ state_anm = "move_fwd";		eff_id = eCEActorMovingFwd; }
			else if(mstate_real&mcBack && !(mstate_old&mcBack))
				{ state_anm = "move_back";		eff_id = eCEActorMovingBack; }
			else if(mstate_real&mcCrouch && !(mstate_old&mcCrouch))
				{ state_anm = "crouch_down";	eff_id = eCEActorCrouchDown;	factor = 1.f; gated = false; }
			else if(mstate_real&mcCrouch && !(mstate_wishful&mcCrouch))
				{ state_anm = "crouch_up";		eff_id = eCEActorCrouchUp;		factor = 1.f; gated = false; }
			else if(mstate_real&mcJump && !(mstate_old&mcJump))
				{ state_anm = "jump";			eff_id = eCEActorJump;			factor = 1.f; gated = false; }
			else if(mstate_real&mcFall && !(mstate_old&mcFall))
				{ state_anm = "fall";			eff_id = eCEActorFallCam;		factor = 1.f; gated = false; }
			else if(mstate_real&mcLanding2)
				{ state_anm = "landing2";		eff_id = eCEActorLanding;		factor = 1.f; gated = false; }
			else if(mstate_real&mcLanding)
				{ state_anm = "landing";		eff_id = eCEActorLanding;		factor = 1.f; gated = false; }
			else if(mstate_real&mcSprint)
				{ state_anm = "sprint";			eff_id = eCEActorMovingSprint; }
		}

		if(state_anm && (!gated || cam_eff_factor>EPS))
		{
			// _aim variant + per-weapon cam_<name> override from the active item's hud section
			string128 base_name;
			xr_strcpy(base_name, state_anm);
			CHudItem* itm = smart_cast<CHudItem*>(inventory().ActiveItem());
			if(itm)
			{
				CWeapon* wpn = smart_cast<CWeapon*>(itm);
				if(wpn && wpn->IsZoomed())
					xr_strcat(base_name, "_aim");
				string128 key;
				xr_sprintf(key, "cam_%s", base_name);
				LPCSTR hs = itm->HudSection().c_str();
				if(pSettings->line_exist(hs, key))
					xr_strcpy(base_name, pSettings->r_string(hs, key));
			}

			CActor*	control_entity		= static_cast_checked<CActor*>(Level().CurrentControlEntity());
			R_ASSERT2					(control_entity, "current control entity is NULL");
			CEffectorCam* ec			= control_entity->Cameras().GetCamEffector(eff_id);
			if(NULL==ec)
			{
				string_path			eff_name;
				xr_sprintf			(eff_name, sizeof(eff_name), "%s.anm", base_name);
				string_path			ce_path;
				string_path			anm_name;
				strconcat			(sizeof(anm_name), anm_name, "camera_effects\\actor_move\\", eff_name);
				// _aim variant falls back to the base anm when the file is absent
				if (!FS.exist(ce_path, "$game_anims$", anm_name) && state_anm)
				{
					xr_sprintf		(eff_name, sizeof(eff_name), "%s.anm", state_anm);
					strconcat		(sizeof(anm_name), anm_name, "camera_effects\\actor_move\\", eff_name);
				}
				if (FS.exist( ce_path, "$game_anims$", anm_name))
				{
					CAnimatorCamLerpEffectorConst* e		= xr_new<CAnimatorCamLerpEffectorConst>();
					e->SetFactor				(factor);
					e->SetType					(eff_id);
					e->SetHudAffect				(false);
					e->SetCyclic				(false);
					e->Start					(anm_name);
					control_entity->Cameras().AddCamEffector(e);
				}
			}
		}
	}
	//transform local dir to world dir
	Fmatrix				mOrient;
	mOrient.rotateY		(-r_model_yaw);
	mOrient.transform_dir(vControlAccel);
}

#define ACTOR_ANIM_SECT "actor_animation"

#define ACTOR_LLOOKOUT_ANGLE	PI_DIV_4
#define ACTOR_RLOOKOUT_ANGLE	PI_DIV_4

void CActor::g_Orientate	(u32 mstate_rl, float dt)
{
	static float fwd_l_strafe_yaw	= deg2rad(pSettings->r_float(ACTOR_ANIM_SECT,	"fwd_l_strafe_yaw"));
	static float back_l_strafe_yaw	= deg2rad(pSettings->r_float(ACTOR_ANIM_SECT,	"back_l_strafe_yaw"));
	static float fwd_r_strafe_yaw	= deg2rad(pSettings->r_float(ACTOR_ANIM_SECT,	"fwd_r_strafe_yaw"));
	static float back_r_strafe_yaw	= deg2rad(pSettings->r_float(ACTOR_ANIM_SECT,	"back_r_strafe_yaw"));
	static float l_strafe_yaw		= deg2rad(pSettings->r_float(ACTOR_ANIM_SECT,	"l_strafe_yaw"));
	static float r_strafe_yaw		= deg2rad(pSettings->r_float(ACTOR_ANIM_SECT,	"r_strafe_yaw"));

	if(!g_Alive())return;
	// visual effect of "fwd+strafe" like motion
	float calc_yaw = 0;
	if(mstate_real&mcClimb)
	{
		if(g_LadderOrient()) return;
	}
	switch(mstate_rl&mcAnyMove)
	{
	case mcFwd+mcLStrafe:
		calc_yaw = +fwd_l_strafe_yaw;//+PI_DIV_4; 
		break;
	case mcBack+mcRStrafe:
		calc_yaw = +back_r_strafe_yaw;//+PI_DIV_4; 
		break;
	case mcFwd+mcRStrafe:
		calc_yaw = -fwd_r_strafe_yaw;//-PI_DIV_4; 
		break;
	case mcBack+mcLStrafe: 
		calc_yaw = -back_l_strafe_yaw;//-PI_DIV_4; 
		break;
	case mcLStrafe:
		calc_yaw = +l_strafe_yaw;//+PI_DIV_3-EPS_L; 
		break;
	case mcRStrafe:
		calc_yaw = -r_strafe_yaw;//-PI_DIV_4+EPS_L; 
		break;
	}

	// lerp angle for "effect" and capture torso data from camera
	angle_lerp		(r_model_yaw_delta,calc_yaw,PI_MUL_4,dt);

	// build matrix
	Fmatrix mXFORM;
	mXFORM.rotateY	(-(r_model_yaw + r_model_yaw_delta));
	mXFORM.c.set	(Position());
	XFORM().set		(mXFORM);
	VERIFY(_valid(XFORM()));

	//-------------------------------------------------

	float tgt_roll		=	0.f;
	if (mstate_rl&mcLookout)
	{
		tgt_roll		=	(mstate_rl&mcLLookout)?-ACTOR_LLOOKOUT_ANGLE:ACTOR_RLOOKOUT_ANGLE;
		
		if( (mstate_rl&mcLLookout) && (mstate_rl&mcRLookout) )
			tgt_roll	= 0.0f;
	}
	// GS LookoutFunctionReplace (WeaponInertion.pas:828): amplified lean with a nonlinear approach --
	// tgt_roll is scaled by lookout_ampl_k (GS 1.5 -> deeper lean) and approached at
	// |dx|^dx_pow * dt * speed (GS speed 6, pow 0.6: fast start, smooth settle), replacing the vanilla
	// fsimilar-guarded linear angle_lerp (GS nops that guard for smoothness). Per-weapon multipliers
	// lookout_speed_koef / lookout_ampl_k come from the active item's hud section.
	{
		LPCSTR GB	= "gunslinger_base";
		float speed	= READ_IF_EXISTS(pSettings, r_float, GB, "lookout_speed", 1.0f);
		float ampl	= READ_IF_EXISTS(pSettings, r_float, GB, "lookout_ampl_k", 1.0f);
		float dxpow	= READ_IF_EXISTS(pSettings, r_float, GB, "lookout_ampl_dx_pow", 1.0f);
		if (CHudItem* itm = smart_cast<CHudItem*>(inventory().ActiveItem()))
		{
			LPCSTR hs	= itm->HudSection().c_str();
			speed		*= READ_IF_EXISTS(pSettings, r_float, hs, "lookout_speed_koef", 1.0f);
			ampl		*= READ_IF_EXISTS(pSettings, r_float, hs, "lookout_ampl_k", 1.0f);
		}
		tgt_roll		*= ampl;
		float dx		= tgt_roll - r_torso_tgt_roll;
		float delta		= _abs(powf(_abs(dx), dxpow) * dt * speed);
		if (dx < 0.f)				delta = -delta;
		if (_abs(delta) > _abs(dx))	delta = dx;
		r_torso_tgt_roll += delta;
		r_torso_tgt_roll = angle_normalize_signed(r_torso_tgt_roll);
	}
}
bool CActor::g_LadderOrient()
{
	Fvector leader_norm;
	character_physics_support()->movement()->GroundNormal(leader_norm);
	if(_abs(leader_norm.y)>M_SQRT1_2) return false;
	//leader_norm.y=0.f;
	float mag=leader_norm.magnitude();
	if(mag<EPS_L) return false;
	leader_norm.div(mag);
	leader_norm.invert();
	Fmatrix M;M.set(Fidentity);
	M.k.set(leader_norm);
	M.j.set(0.f,1.f,0.f);
	generate_orthonormal_basis1(M.k,M.j,M.i);
	M.i.invert();
	//M.j.invert();


	//Fquaternion q1,q2,q3;
	//q1.set(XFORM());
	//q2.set(M);
	//q3.slerp(q1,q2,dt);
	//Fvector angles1,angles2,angles3;
	//XFORM().getHPB(angles1.x,angles1.y,angles1.z);
	//M.getHPB(angles2.x,angles2.y,angles2.z);
	////angle_lerp(angles3.x,angles1.x,angles2.x,dt);
	////angle_lerp(angles3.y,angles1.y,angles2.y,dt);
	////angle_lerp(angles3.z,angles1.z,angles2.z,dt);

	//angles3.lerp(angles1,angles2,dt);
	////angle_lerp(angles3.y,angles1.y,angles2.y,dt);
	////angle_lerp(angles3.z,angles1.z,angles2.z,dt);
	//angle_lerp(angles3.x,angles1.x,angles2.x,dt);
	//XFORM().setHPB(angles3.x,angles3.y,angles3.z);
	Fvector position;
	position.set(Position());
	//XFORM().rotation(q3);
	VERIFY2(_valid(M),"Invalide matrix in g_LadderOrient");
	XFORM().set(M);
	VERIFY2(_valid(position),"Invalide position in g_LadderOrient");
	Position().set(position);
	VERIFY(_valid(XFORM()));
	return true;
}
// ****************************** Update actor orientation according to camera orientation
void CActor::g_cl_Orientate	(u32 mstate_rl, float dt)
{
	// capture camera into torso (only for FirstEye & LookAt cameras)
	if (eacFreeLook!=cam_active)
	{
		r_torso.yaw		=	cam_Active()->GetWorldYaw	();
		r_torso.pitch	=	cam_Active()->GetWorldPitch	();
	}
	else
	{
		r_torso.yaw		=	cam_FirstEye()->GetWorldYaw	();
		r_torso.pitch	=	cam_FirstEye()->GetWorldPitch	();
	}

	unaffected_r_torso.yaw		= r_torso.yaw;
	unaffected_r_torso.pitch	= r_torso.pitch;
	unaffected_r_torso.roll		= r_torso.roll;

	CWeaponMagazined *pWM = smart_cast<CWeaponMagazined*>(inventory().GetActiveSlot() != NO_ACTIVE_SLOT ? 
		inventory().ItemFromSlot(inventory().GetActiveSlot())/*inventory().m_slots[inventory().GetActiveSlot()].m_pIItem*/ : NULL);
	if (pWM && pWM->GetCurrentFireMode() == 1 && eacFirstEye != cam_active)
	{
		Fvector dangle = weapon_recoil_last_delta();
		r_torso.yaw		=	unaffected_r_torso.yaw + dangle.y;
		r_torso.pitch	=	unaffected_r_torso.pitch + dangle.x;
	}
	
	// ���� ���� �������� - ��������� ������ �� ������
	if (mstate_rl&mcAnyMove)	{
		r_model_yaw		= angle_normalize(r_torso.yaw);
		mstate_real		&=~mcTurn;
	} else {
		// if camera rotated more than 45 degrees - align model with it
		float ty = angle_normalize(r_torso.yaw);
		if (_abs(r_model_yaw-ty)>PI_DIV_4)	{
			r_model_yaw_dest = ty;
			// 
			mstate_real	|= mcTurn;
		}
		if (_abs(r_model_yaw-r_model_yaw_dest)<EPS_L){
			mstate_real	&=~mcTurn;
		}
		if (mstate_rl&mcTurn){
			angle_lerp	(r_model_yaw,r_model_yaw_dest,PI_MUL_2,dt);
		}
	}
}

void CActor::g_sv_Orientate(u32 /**mstate_rl/**/, float /**dt/**/)
{
	r_model_yaw		= NET_Last.o_model;

	r_torso.yaw		=	unaffected_r_torso.yaw;
	r_torso.pitch	=	unaffected_r_torso.pitch;
	r_torso.roll	=	unaffected_r_torso.roll;

	CWeaponMagazined *pWM = smart_cast<CWeaponMagazined*>(inventory().GetActiveSlot() != NO_ACTIVE_SLOT ? 
		inventory().ItemFromSlot(inventory().GetActiveSlot())/*inventory().m_slots[inventory().GetActiveSlot()].m_pIItem*/ : NULL);
	if (pWM && pWM->GetCurrentFireMode() == 1/* && eacFirstEye != cam_active*/)
	{
		Fvector dangle = weapon_recoil_last_delta();
		r_torso.yaw		+=	dangle.y;
		r_torso.pitch	+=	dangle.x;
		r_torso.roll	+=	dangle.z;
	}
}

bool isActorAccelerated(u32 mstate, bool ZoomMode) 
{
	bool res = false;
	if (mstate&mcAccel)
		res = psActorFlags.test(AF_ALWAYSRUN)?false:true;
	else
		res = psActorFlags.test(AF_ALWAYSRUN)?true :false;
	if (mstate&(mcCrouch|mcClimb|mcJump|mcLanding|mcLanding2))
		return res;
	if (mstate & mcLookout || ZoomMode)
		return false;
	return res;
}

bool CActor::CanAccelerate()
{
	bool can_accel = !conditions().IsLimping() &&
		!character_physics_support()->movement()->PHCapture() && 
		(m_time_lock_accel < Device.dwTimeGlobal)
	;		

	return can_accel;
}

bool CActor::CanRun()
{
	bool can_run		= !IsZoomAimingMode() && !(mstate_real&mcLookout);
	return can_run;
}

bool CActor::CanSprint()
{
	bool can_Sprint = CanAccelerate() && !conditions().IsCantSprint() &&
						Game().PlayerCanSprint(this)
						&& CanRun()
						&& !(mstate_real&mcLStrafe || mstate_real&mcRStrafe)
						&& InventoryAllowSprint()
						;

	return can_Sprint && (m_block_sprint_counter<=0);
}

bool CActor::CanJump()
{
	bool can_Jump = 
		!character_physics_support()->movement()->PHCapture() &&((mstate_real&mcJump)==0) && (m_fJumpTime<=0.f) 
		&& !m_bJumpKeyPressed &&!IsZoomAimingMode();

	return can_Jump;
}

bool CActor::CanMove()
{
	if( conditions().IsCantWalk() )
	{
		if(mstate_wishful&mcAnyMove)
		{
			HUD().GetUI()->AddInfoMessage("cant_walk");
		}
		return false;
	}else
	if( conditions().IsCantWalkWeight() )
	{
		if(mstate_wishful&mcAnyMove)
		{
			HUD().GetUI()->AddInfoMessage("cant_walk_weight");
		}
		return false;
	
	}

	if(IsTalking())
		return false;
	else
		return true;
}

void CActor::StopAnyMove()
{
	mstate_wishful	&=		~mcAnyMove;
	mstate_real		&=		~mcAnyMove;
}


bool CActor::is_jump()
{
	return ((mstate_real & (mcJump|mcFall|mcLanding|mcLanding2)) != 0);
}

//������������ ���������� ���
#include "CustomOutfit.h"
float CActor::MaxCarryWeight () const
{
	float res = inventory().GetMaxWeight();
	res      += get_additional_weight();
	return res;
}

float CActor::MaxWalkWeight() const
{
	float max_w = CActor::conditions().MaxWalkWeight();
	max_w      += get_additional_weight();
	return max_w;
}

float CActor::get_additional_weight() const
{
	float res = 0.0f ;
	CCustomOutfit* outfit	= GetOutfit();
	if ( outfit )
	{
		res				+= outfit->m_additional_weight;
	}

	if ( !m_ArtefactsOnBelt.empty() )
	{
		xr_vector<const CArtefact*>::const_iterator it		= m_ArtefactsOnBelt.begin();
		xr_vector<const CArtefact*>::const_iterator it_e	= m_ArtefactsOnBelt.end();
		for ( ; it != it_e ; ++it )
		{
			res			+= (*it)->AdditionalInventoryWeight();
		}
	}
	return res;
}
