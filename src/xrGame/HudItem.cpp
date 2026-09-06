#include "stdafx.h"
#include "HudItem.h"
#include "Weapon.h"
#include "physic_item.h"
#include "actor.h"
#include "actoreffector.h"
#include "Missile.h"
#include "xrmessages.h"
#include "level.h"
#include "inventory.h"
#include "../xrEngine/CameraBase.h"
#include "player_hud.h"
#include "../xrEngine/SkeletonMotions.h"
#include "../xrEngine/motion.h"			// ESMFlags (esmStopAtEnd) - cyclic vs one-shot sprint loop
#include "GamePersistent.h"				// GS action DOF (SetEffectorDOF + the [gunslinger_base] defaults)

extern bool gwr_pda_need_fastzoom();		// ui\UIPdaWnd.cpp

// ---- ppe played over a slice of a hud motion (Gunslinger's NV blackout) ----
// Keyed per ALIAS, exactly as GS does it: use_ppe_effector_<alias> / ppe_effector_<alias> /
// ppe_start_<alias> / ppe_end_<alias>. That genericity is the whole point -- the goggles gesture
// plays on whatever HUD is out (the empty-hands animator's anm_show, a weapon's anm_nv_on, and its
// _empty/_jammed twins), so keying it to one hard-coded alias only ever blacked out one of them.
// Called from PlayHUDMotion, where the alias is already resolved and the motion's length is known.
void CHudItem::ArmPPE(const shared_str& alias)
{
	m_show_ppe_at = 0;
	if (!alias.size())	return;

	string256 key;
	strconcat(sizeof(key), key, "use_ppe_effector_", alias.c_str());
	if (!READ_IF_EXISTS(pSettings, r_bool, HudSection(), key, FALSE))	return;

	strconcat(sizeof(key), key, "ppe_effector_", alias.c_str());
	LPCSTR sect = READ_IF_EXISTS(pSettings, r_string, HudSection(), key, "");
	if (!sect || !sect[0])	return;

	u32 now = Device.dwTimeGlobal;
	u32 end = MotionEndTm();
	if (end <= now)			return;				// no motion running -> nothing to key off

	strconcat(sizeof(key), key, "ppe_start_", alias.c_str());
	float f0 = READ_IF_EXISTS(pSettings, r_float, HudSection(), key, 0.0f);
	strconcat(sizeof(key), key, "ppe_end_", alias.c_str());
	float f1 = READ_IF_EXISTS(pSettings, r_float, HudSection(), key, 1.0f);

	float len			= float(end - now);
	m_show_ppe_sect		= sect;
	m_show_ppe_at		= now + u32(len * f0);
	m_show_ppe_off_at	= now + u32(len * f1);
	m_show_ppe_on		= false;
}

void CHudItem::UpdateShowPPE()
{
	if (!m_show_ppe_at)					return;
	CActor* pA = smart_cast<CActor*>(object().H_Parent());
	if (!pA)							return;
	u32 now = Device.dwTimeGlobal;

	if (!m_show_ppe_on && now >= m_show_ppe_at)
	{
		AddEffector		(pA, effActionAnimPPE, m_show_ppe_sect);
		m_show_ppe_on	= true;
	}
	if (m_show_ppe_on && now >= m_show_ppe_off_at)
	{
		// Drop it, don't Stop() it: Stop(sp) fades m_factor out linearly over 1/sp seconds, so the
		// black lingered a whole second past the gesture. Gunslinger cuts both ways -- the effector
		// starts at m_factor 1.0 (hard in), and this makes the way out just as hard.
		RemoveEffector	(pA, effActionAnimPPE);
		m_show_ppe_on	= false;
		m_show_ppe_at	= 0;					// done for this gesture
	}
}

ENGINE_API extern float psHUD_FOV_def;

CHudItem::CHudItem()
{
	RenderHud					(TRUE);
//	m_hud_item_shared_data		= NULL;
	m_bStopAtEndAnimIsRunning = false;
	m_current_motion_def		= NULL;
	m_bAttachedNoMotion			= false;
	m_bActionDofActive			= false;
	m_pending_dof_alias			= NULL;
	m_pending_dof_until			= 0;
	m_fActionDofOutSpeed		= 1.f;
	m_iActionDofTimeOffsetMs	= -500;
	m_started_rnd_anim_idx		= u8(-1);
	m_fHudFov					= 0.f;
	m_fHudFovAim				= 0.f;
	m_fHudFovAimScope			= 0.f;
	m_bStepSlow					= false;
	m_bStepCrouch				= false;
	m_bSprintStarted			= false;
	m_bSprintStartRunning		= false;
	m_bPrevSprint				= false;
	m_sprint_loop_motion		= NULL;
	m_bSprintLoopCyclic			= false;
	m_fNextBlendAccrue			= 0.f;
	m_dwSprintExitEndTm			= 0;
	m_dwShootLockTm				= 0;
	m_bIdleTransitionLock		= false;
	m_bIdleMoving				= false;
	m_bSuppressCompanion		= false;
	m_bPdaCursorAnims			= false;
	m_bBlowoutPlayed			= false;
	m_dwBlowoutUntil			= 0;
	m_show_ppe_at				= 0;
	m_show_ppe_off_at			= 0;
	m_show_ppe_on				= false;
}

// 3D PDA cursor direction, set by CUIPdaWnd::Update from the accumulated mouse movement.
// 0 = centred (plain idle), 1..8 = the eight directions, 9 = click. Global because only one PDA
// can be open at a time, and the hud item has to read it from deep inside its idle selection.
int g_pda_cursor_dir = 0;
// suffix per direction, indexed by g_pda_cursor_dir (must match the enum order above)
LPCSTR g_pda_dir_suffix[] = { "", "_up", "_up_right", "_right", "_down_right", "_down", "_down_left", "_left", "_up_left", "_click" };

bool CHudItem::HasMovementIdleVariant()
{
	// base HUD items (detectors, etc.) vary by walk speed and/or crouch posture
	return isHUDAnimationExist("anm_idle_moving_slow")
		|| isHUDAnimationExist("anm_idle_moving_crouch");
}

DLL_Pure *CHudItem::_construct	()
{
	m_object			= smart_cast<CPhysicItem*>(this);
	VERIFY				(m_object);

	m_item				= smart_cast<CInventoryItem*>(this);
	VERIFY				(m_item);

	return				(m_object);
}

CHudItem::~CHudItem()
{
}

void CHudItem::Load(LPCSTR section)
{
	hud_sect				= pSettings->r_string		(section,"hud");
	m_animation_slot		= pSettings->r_u32			(section,"animation_slot");
	// xrMPE per-weapon actor (third-person) animations: names a torso set in the actor animation pack
	// instead of one of the 13 stock numbered slots -- `norm_torso_<group>_aim_1` and so on. Optional;
	// without it the weapon keeps using animation_slot exactly as before.
	m_actor_anim_group		= READ_IF_EXISTS(pSettings, r_string, section, "actor_anim_group", "");
	// A ONE-SHOT torso motion for the actor while this item is in hand, addressed by full motion
	// name (the pack has no family for these -- just norm_torso_item_medkit and friends). Meant for
	// the item-use gesture phantoms, whose section IS the hud section gwr_eatable spawns.
	m_actor_torso_anim		= READ_IF_EXISTS(pSettings, r_string, section, "actor_torso_anim", "");

	m_fHudFov = READ_IF_EXISTS(pSettings, r_float, hud_sect, "hud_fov", 0.f);
	m_fHudFovAim = READ_IF_EXISTS(pSettings, r_float, hud_sect, "hud_fov_aim", 0.f);
	m_fHudFovAimScope = READ_IF_EXISTS(pSettings, r_float, hud_sect, "scope_hud_fov_aim", 0.f);

	// GS inertion model (WeaponInertion.pas UpdateInertion): defaults from [gunslinger_base]
	// (GS gunslinger_params.ltx: hip -0.017/-0.012/-0.02, origin 0.09, speed 5; aim 0/0/0, 0.04, 7 --
	// note the flipped signs vs vanilla: GS substitutes these into the same engine constant slots),
	// per-weapon overrides via inertion_* / inertion_aim_* in the hud section.
	LPCSTR GB = "gunslinger_base";
	float d_r  = READ_IF_EXISTS(pSettings, r_float, GB, "inertion_default_pitch_offset_r", PITCH_OFFSET_R);
	float d_n  = READ_IF_EXISTS(pSettings, r_float, GB, "inertion_default_pitch_offset_n", PITCH_OFFSET_N);
	float d_d  = READ_IF_EXISTS(pSettings, r_float, GB, "inertion_default_pitch_offset_d", PITCH_OFFSET_D);
	float d_o  = READ_IF_EXISTS(pSettings, r_float, GB, "inertion_default_origin_offset", ORIGIN_OFFSET);
	float d_s  = READ_IF_EXISTS(pSettings, r_float, GB, "inertion_default_speed", TENDTO_SPEED);
	m_current_inertion.PitchOffsetR = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_pitch_offset_r", d_r);
	m_current_inertion.PitchOffsetN = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_pitch_offset_n", d_n);
	m_current_inertion.PitchOffsetD = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_pitch_offset_d", d_d);
	m_current_inertion.OriginOffset = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_origin_offset", d_o);
	m_current_inertion.TendtoSpeed  = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_speed",
									  READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_tendto_speed", d_s));

	float a_r  = READ_IF_EXISTS(pSettings, r_float, GB, "inertion_aim_default_pitch_offset_r", m_current_inertion.PitchOffsetR);
	float a_n  = READ_IF_EXISTS(pSettings, r_float, GB, "inertion_aim_default_pitch_offset_n", m_current_inertion.PitchOffsetN);
	float a_d  = READ_IF_EXISTS(pSettings, r_float, GB, "inertion_aim_default_pitch_offset_d", m_current_inertion.PitchOffsetD);
	float a_o  = READ_IF_EXISTS(pSettings, r_float, GB, "inertion_aim_default_origin_offset", m_current_inertion.OriginOffset);
	float a_s  = READ_IF_EXISTS(pSettings, r_float, GB, "inertion_aim_default_speed", m_current_inertion.TendtoSpeed);
	m_aim_inertion.PitchOffsetR = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_aim_pitch_offset_r", a_r);
	m_aim_inertion.PitchOffsetN = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_aim_pitch_offset_n", a_n);
	m_aim_inertion.PitchOffsetD = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_aim_pitch_offset_d", a_d);
	m_aim_inertion.OriginOffset = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_aim_origin_offset", a_o);
	m_aim_inertion.TendtoSpeed  = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_aim_speed", a_s);

	// grenade-launcher mode: inertion_gl_* keys, defaulting to the resolved aim set (GS
	// ApplyInertionParamsWithDef(aim_inert, wpn_inert.gl, aim_inert))
	m_gl_inertion.PitchOffsetR = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_gl_pitch_offset_r", m_aim_inertion.PitchOffsetR);
	m_gl_inertion.PitchOffsetN = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_gl_pitch_offset_n", m_aim_inertion.PitchOffsetN);
	m_gl_inertion.PitchOffsetD = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_gl_pitch_offset_d", m_aim_inertion.PitchOffsetD);
	m_gl_inertion.OriginOffset = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_gl_origin_offset", m_aim_inertion.OriginOffset);
	m_gl_inertion.TendtoSpeed  = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_gl_speed", m_aim_inertion.TendtoSpeed);

	m_bHudInertion  = !!READ_IF_EXISTS(pSettings, r_bool, hud_sect, "hud_inertion", TRUE);
	m_bZoomInertion = !!READ_IF_EXISTS(pSettings, r_bool, hud_sect, "zoom_inertion", FALSE);
	m_bDisableBore  = !!READ_IF_EXISTS(pSettings, r_bool, hud_sect, "disable_bore", TRUE);	// GS default: no idle bore anim

	m_bPdaCursorAnims = !!READ_IF_EXISTS(pSettings, r_bool, hud_sect, "pda_cursor_anims", FALSE);

	m_sounds.LoadSound(section, "snd_bore", "sndBore", true);
	m_sounds.LoadSound(section, "snd_blowout", "sndBlowout", true);	// GS emission glitch sound (PDA pda_vibros); optional
}

// GS UpdateInertion: hip params blended toward the aim set by the zoom factor; hud_inertion=false or
// (zoom_inertion && zoomed) turns the sway off (GS AllowWeaponInertion on OnZoomIn/OnZoomOut).
InertionData& CHudItem::CurrentInertionData()
{
	if (!m_bHudInertion || (m_bZoomInertion && InertionZoomedNow()))
	{
		// zero offsets = no visual sway. TendtoSpeed must make speed*dt == EXACTLY 1 (one-frame snap of
		// the internal last-dir tracker): the tracker is st_last_dir.mad(diff, speed*dt), so speed*dt > 1
		// OVERSHOOTS and diverges to NaN (was 1000 -> weapons went invisible + fps drop).
		static InertionData s_off;
		s_off.PitchOffsetR = s_off.PitchOffsetN = s_off.PitchOffsetD = 0.f;
		s_off.OriginOffset = 0.f;
		s_off.TendtoSpeed  = (Device.fTimeDelta > EPS) ? (1.f / Device.fTimeDelta) : 0.f;
		return s_off;
	}
	float f = GetInertionAimFactor();
	if (f <= 0.f)	return m_current_inertion;
	m_blend_inertion.lerp(m_current_inertion, InertionGrenadeModeNow() ? m_gl_inertion : m_aim_inertion, f);
	return m_blend_inertion;
}


void CHudItem::PlaySound(LPCSTR alias, const Fvector& position, bool b_force_unlock)
{
	m_sounds.PlaySound	(alias, position, object().H_Root(), !!GetHUDmode(), false, u8(-1), b_force_unlock);
}

void CHudItem::renderable_Render()
{
	UpdateXForm					();
	BOOL _hud_render			= ::Render->get_HUD() && GetHUDmode();
	
	if(_hud_render  && !IsHidden())
	{
		// ...but only for the person LOOKING through that HUD. In third person the owner's own body is
		// on screen and the item has to be in its hand -- this empty branch is why the world model of
		// everything held (weapons, the item-use phantoms, the PDA) was invisible there. CActor sets
		// setVisible(!HUDview()) on itself, so the owner's visual is the exact test.
		CObject* p = object().H_Parent();
		if (p && p->getVisible())
			on_renderable_Render	();
	}
	else 
	{
		if (!object().H_Parent() || (!_hud_render && !IsHidden()))
		{
			on_renderable_Render		();
			debug_draw_firedeps			();
		}else
		if (object().H_Parent()) 
		{
			CInventoryOwner	*owner = smart_cast<CInventoryOwner*>(object().H_Parent());
			VERIFY			(owner);
			CInventoryItem	*self = smart_cast<CInventoryItem*>(this);
			if (owner->attached(self))
				on_renderable_Render();
		}
	}
}

void CHudItem::SwitchState(u32 S)
{
	if (OnClient())
		return;

	// GS IsActionProcessing (WeaponAdditionalBuffer.pas:591) reports the weapon busy for the whole
	// controller-suicide scene, so nothing can put an idle over the gesture. Ours had no such rule and
	// the idle came back the moment the gesture's motion ended -- on a launcher-equipped rifle through
	// CWeaponMagazinedWGrenade's own PlayAnimIdle, which bypassed the guard in the base class. The
	// block belongs on the TRANSITION, where no subclass can route around it: the scene owns the pose
	// until it resolves, and the shot leaves from the victim's own head rather than from the hip.
	if (S == eIdle)
	{
		CWeapon* w = smart_cast<CWeapon*>(this);
		if (w && w->SuicideHoldsPose())	return;
	}

	SetNextState( S );

	if (object().Local() && !object().getDestroy())	
	{
		// !!! Just single entry for given state !!!
		NET_Packet				P;
		object().u_EventGen		(P,GE_WPN_STATE_CHANGE,object().ID());
		P.w_u8					(u8(S));
		object().u_EventSend	(P);
	}
}

void CHudItem::OnEvent(NET_Packet& P, u16 type)
{
	switch (type)
	{
	case GE_WPN_STATE_CHANGE:
		{
			u8				S;
			P.r_u8			(S);
			OnStateSwitch	(u32(S));
		}
		break;
	}
}

void CHudItem::OnStateSwitch(u32 S)
{
	SetState			(S);

	// putting the item away ends whatever action was running on it, so the action DOF goes with it.
	// Matters most when the item is holstered by something OTHER than the animation finishing --
	// a quick grenade cutting an item-use gesture, for one.
	if(S==eHiding || S==eHidden)
		StopActionDof	();

	if(object().Remote())
		SetNextState	(S);

	switch (S)
	{
	case eBore:
		SetPending		(FALSE);

		PlayAnimBore	();
		if(HudItemData())
		{
			Fvector P		= HudItemData()->m_item_transform.c;
			m_sounds.PlaySound("sndBore", P, object().H_Root(), !!GetHUDmode(), false, m_started_rnd_anim_idx);
		}

		break;
	}
}

void CHudItem::OnAnimationEnd(u32 state)
{
	// any motion that was holding the transition lock (aim in/out) has now ended
	m_bIdleTransitionLock = false;

	switch(state)
	{
	case eBore:
		{
			SwitchState	(eIdle);
		} break;
	}
}

void CHudItem::PlayAnimBore()
{
	PlayHUDMotion	("anm_bore", TRUE, this, GetState());
}

bool CHudItem::ActivateItem() 
{
	OnActiveItem	();
	return			true;
}

void CHudItem::DeactivateItem() 
{
	OnHiddenItem	();
}
void CHudItem::OnMoveToRuck(EItemPlace prev)
{
	SwitchState(eHidden);
}

void CHudItem::SendDeactivateItem	()
{
	SendHiddenItem	();
}
void CHudItem::SendHiddenItem()
{
	if (!object().getDestroy())
	{
		NET_Packet				P;
		object().u_EventGen		(P,GE_WPN_STATE_CHANGE,object().ID());
		P.w_u8					(u8(eHiding));
		object().u_EventSend	(P, net_flags(TRUE, TRUE, FALSE, TRUE));

		/*NET_Packet		P;
		CHudItem::object().u_EventGen		(P,GE_WPN_STATE_CHANGE,CHudItem::object().ID());
		P.w_u8			(u8(eHidden));
		P.w_u8			(u8(m_sub_state));
		P.w_u8			(u8(m_ammoType& 0xff));
		P.w_u8			(u8(iAmmoElapsed & 0xff));
		P.w_u8			(u8(m_set_next_ammoType_on_reload & 0xff));
		CHudItem::object().u_EventSend		(P, net_flags(TRUE, TRUE, FALSE, TRUE));*/
	}
}


void CHudItem::UpdateHudAdditonal		(Fmatrix& hud_trans)
{
}

void CHudItem::UpdateCL()
{
	UpdateShowPPE();

	// The moving<->standing idle swap is a SINGLE edge: CActor::g_SetAnimation sees mcAnyMove
	// change and calls player_hud::OnMovementChanged once. OnMovementChanged is allowed to refuse
	// it (not eIdle, an aim transition, a one-shot gesture in the idle slot) -- and then the
	// notification is gone for good, because nothing re-evaluates the idle while the actor stands
	// still. That is the walk animation that keeps running after a dialogue opens (CanMove() goes
	// false on the very frame the talk window takes the input) and after anything else stops him
	// from the outside. Re-check the one direction that gets stuck.
	// Only from a moving idle to a standing one: the PDA cursor/aim idles never take the moving
	// branch, so they leave the flag false and this can never fire on them. The flag is cleared
	// BEFORE the re-play so a PlayAnimIdle override that returns without reaching TryPlayAnimIdle
	// (the detector mirroring its companion) cannot turn this into a per-frame loop.
	if(m_bIdleMoving && GetState()==eIdle && !IsPending() &&
	   !m_bIdleTransitionLock && !m_bStopAtEndAnimIsRunning)
	{
		CActor* pIdleActor = smart_cast<CActor*>(object().H_Parent());
		if(pIdleActor && pIdleActor==Level().CurrentViewEntity())
		{
			CEntity::SEntityState st;
			pIdleActor->g_State(st);
			if(!pIdleActor->AnyMove() && !st.bSprint)
			{
				m_bIdleMoving = false;
				PlayAnimIdle();
			}
		}
	}

	if (m_pending_dof_alias.size())
	{
		shared_str	a		= m_pending_dof_alias;
		const bool	expired	= (Device.dwTimeGlobal > m_pending_dof_until);
		if (expired || HudItemData())
		{
			m_pending_dof_alias	= NULL;
			if (!expired)	StartActionDof(a.c_str());
		}
	}

	// GS WeaponUpdate.pas:928 -- the action DOF is released relative to the ANIMATION, not on a timer
	// of its own: dof_time_offset_<alias> is negative by default (-0.5 s), meaning "start easing out
	// half a second before this animation ends", so the focus is already back by the time the hands
	// settle. A positive value counts from the start instead. Skipped while aiming, because there the
	// aim DOF is the one in charge.
	// This sits at the TOP level on purpose. It used to live inside the motion bookkeeping below, and
	// an animation that never reaches its end took the DOF with it: StopCurrentAnimWithoutCallback
	// (a hud item going independent, an item-use phantom released mid-gesture) zeroes the timings and
	// m_current_motion_def, after which that block never runs again. Hence a zero end time counts as
	// due -- the animation that owned the focus is gone, so give it back. The stuck blur was obvious
	// only with empty hands, where no later weapon animation happens to re-arm and release it.
	if(m_bActionDofActive && !DofHeldByAim() && GamePersistent().DofChanged())
	{
		const u32 now = Device.dwTimeGlobal;
		const int off = m_iActionDofTimeOffsetMs;
		bool due = (0 == m_dwMotionEndTm);
		if(!due && off < 0)
			due = (m_dwMotionEndTm <= now) || ((m_dwMotionEndTm - now) < u32(-off));
		else if(!due && off > 0)
			due = (now - m_dwMotionStartTm) > u32(off);
		if(due)		StopActionDof();
	}

	if(m_current_motion_def)
	{
		if(m_bStopAtEndAnimIsRunning)
		{
			const xr_vector<motion_marks>&	marks = m_current_motion_def->marks;
			if(!marks.empty())
			{
				float motion_prev_time = ((float)m_dwMotionCurrTm - (float)m_dwMotionStartTm)/1000.0f;
				float motion_curr_time = ((float)Device.dwTimeGlobal - (float)m_dwMotionStartTm)/1000.0f;
				
				xr_vector<motion_marks>::const_iterator it = marks.begin();
				xr_vector<motion_marks>::const_iterator it_e = marks.end();
				for(;it!=it_e;++it)
				{
					const motion_marks&	M = (*it);
					if(M.is_empty())
						continue;
	
					const motion_marks::interval* Iprev = M.pick_mark(motion_prev_time);
					const motion_marks::interval* Icurr = M.pick_mark(motion_curr_time);
					if(Iprev==NULL && Icurr!=NULL /* || M.is_mark_between(motion_prev_time, motion_curr_time)*/)
					{
						OnMotionMark				(m_startedMotionState, M);
					}
				}
			
			}

			m_dwMotionCurrTm					= Device.dwTimeGlobal;
			if(m_dwMotionCurrTm > m_dwMotionEndTm)
			{
				m_current_motion_def				= NULL;
				m_dwMotionStartTm					= 0;
				m_dwMotionEndTm						= 0;
				m_dwMotionCurrTm					= 0;
				m_bStopAtEndAnimIsRunning = false;
				StopActionDof						();	// nothing released it early -> release it now
				OnAnimationEnd						(m_startedMotionState);
			}
		}
	}
}

// GS SetShootLockTime: block firing for `ms` (0 clears). Deadline-based like our other fire timers, so it
// self-expires -- no per-frame tick needed. The base HUD item has only this plain lock; CWeaponMagazined
// folds its aim/sprint fire-lock deadlines into IsShootLocked() on top.
void CHudItem::SetShootLock(u32 ms)
{
	m_dwShootLockTm = ms ? (Device.dwTimeGlobal + ms) : 0;
}

bool CHudItem::IsShootLocked() const
{
	return m_dwShootLockTm && Device.dwTimeGlobal < m_dwShootLockTm;
}

void CHudItem::OnH_A_Chield		()
{}

void CHudItem::OnH_B_Chield		()
{
	StopCurrentAnimWithoutCallback();
}

void CHudItem::OnH_B_Independent	(bool just_before_destroy)
{
	m_sounds.StopAllSounds	();
	UpdateXForm				();
	
	// next code was commented 
	/*
	if(HudItemData() && !just_before_destroy)
	{
		object().XFORM().set( HudItemData()->m_item_transform );
	}
	
	if (HudItemData())
	{
		g_player_hud->detach_item(this);
		Msg("---Detaching hud item [%s][%d]", this->HudSection().c_str(), this->object().ID());
	}*/
	//SetHudItemData			(NULL);
}

void CHudItem::OnH_A_Independent	()
{
	if(HudItemData())
		g_player_hud->detach_item(this);
	StopCurrentAnimWithoutCallback();
}

void CHudItem::on_b_hud_detach()
{
	m_sounds.StopAllSounds	();
}

void CHudItem::on_a_hud_attach()
{
	m_bSprintStarted = false;	// fresh draw: no sprint transition owed (avoids a stray sprint_end on redraw)
	m_bSprintStartRunning = false;
	if(m_current_motion_def)
	{
		PlayHUDMotion_noCB(m_current_motion, FALSE);
		// This motion was started BEFORE we were attached (a draw: switch2_Showing runs off the state
		// machine, the attach happens later in CActor::UpdateCL), so PlayHUDMotion's companion hook bailed
		// out back then - attached_item(0) wasn't us yet. It only becomes visible here, so mirror it now,
		// else a weapon drawn with a detector out never plays the detector's anm_wpn_show (the holster
		// works because the weapon is long attached by then and goes through PlayHUDMotion normally).
		TryPlayDetectorCompanion(m_current_motion);
#ifdef DEBUG
		Msg("continue playing [%s][%d]",m_current_motion.c_str(), Device.dwFrame);
#endif // #ifdef DEBUG
	}
	else
	{
		// Nothing was playing: the previous motion cleared itself when it ended (CHudItem::UpdateCL
		// nulls m_current_motion_def at the end of a motion), which is exactly the state a weapon is
		// in after it has finished holstering. There is nothing sensible to show until the draw
		// starts -- the bind pose puts the gun in the middle of the screen, and an idle is a pose the
		// draw does not begin from, so either way there is a visible seam. Skip drawing it instead;
		// the very next motion (normally anm_show, a frame later) clears this.
		m_bAttachedNoMotion = true;
	}
}

void CHudItem::MakeJammedName(LPCSTR name, string_path& out)
{
	MakeStateName	(name, "_jammed", out);
}

// Build a weapon-state alias variant ("_jammed" / "_empty"): the infix goes before a trailing GL
// suffix, else at the very end. NOTE: no numeric _0.._3 suffixes here — they collide with firemode
// tokens like anm_firemode_a_to_1 (the "_1").
void CHudItem::MakeStateName(LPCSTR name, LPCSTR infix, string_path& out)
{
	static const LPCSTR sfx[] = { "_gl_off", "_gl_on", "_w_gl", "_g" };
	int len = (int)xr_strlen(name);
	for (u32 i=0; i<sizeof(sfx)/sizeof(sfx[0]); ++i)
	{
		int sl = (int)xr_strlen(sfx[i]);
		if (len>sl && 0==xr_strcmp(name+len-sl, sfx[i]))
		{
			xr_strcpy	(out, name);
			out[len-sl]	= 0;
			xr_strcat	(out, infix);
			xr_strcat	(out, sfx[i]);
			return;
		}
	}
	xr_strcpy	(out, name);
	xr_strcat	(out, infix);
}

// Split an alias into "<stem><tail>", where tail is the run of trailing state/variant tokens the
// firemode mark has to be inserted IN FRONT of: anm_reload_empty_w_gl -> ("anm_reload", "_empty_w_gl"),
// anm_shoot_aim_last -> ("anm_shoot_aim", "_last"). The tail keeps the config's own token order, so
// re-joining it around the mark reproduces GS's naming exactly. Tokens that belong to the BASE
// (_aim, _scope, _moving_<dir>, ...) are deliberately not in the table. Returns false when there is
// no tail (nothing to move the mark past).
bool CHudItem::SplitStateSuffix(LPCSTR name, string_path& stem, string_path& tail)
{
	static const LPCSTR tok[] = {
		"_gl_off", "_gl_on", "_w_gl", "_g", "_detector",	// GL / companion suffixes (outermost)
		"_sil", "_ammochange", "_preloaded", "_suicide",	// variant tokens
		"_empty", "_jammed", "_first", "_last"				// weapon-state tokens
	};

	xr_strcpy(stem, name);
	tail[0] = 0;
	int len = (int)xr_strlen(stem);

	for (;;)
	{
		bool hit = false;
		for (u32 i = 0; i < sizeof(tok) / sizeof(tok[0]); ++i)
		{
			int sl = (int)xr_strlen(tok[i]);
			if (len > sl && 0 == xr_strcmp(stem + len - sl, tok[i]))
			{
				// "_detector" is movable only where GS puts it LAST, as the "while the detector is
				// out" variant of a weapon action (anm_reload_auto_empty_detector). In the detector
				// GESTURE aliases it is part of the NAME and the state follows it instead
				// (anm_draw_detector_empty, anm_holster_detector_auto_empty) -- peeling it there built
				// anm_draw_auto_empty_detector, which nothing authors, so the gesture silently fell
				// back to the bare alias and ignored empty/jammed and the fire-mode mark.
				if (0 == xr_strcmp(tok[i], "_detector"))
				{
					static const LPCSTR gesture[] = { "_draw", "_prepare", "_holster", "_finish" };
					bool is_gesture = false;
					for (u32 g = 0; g < sizeof(gesture) / sizeof(gesture[0]); ++g)
					{
						int gl = (int)xr_strlen(gesture[g]);
						// PREFIX compare: what follows is the "_detector" we are testing, so a full
						// string compare (xr_strcmp) can never match here.
						if (len - sl > gl && 0 == strncmp(stem + len - sl - gl, gesture[g], gl))
							{ is_gesture = true; break; }
					}
					if (is_gesture)	continue;
				}
				string_path t;
				strconcat(sizeof(t), t, tok[i], tail);		// prepend: keeps the original order
				xr_strcpy(tail, t);
				stem[len - sl] = 0;
				len -= sl;
				hit = true;
				break;
			}
		}
		if (!hit)	break;
	}
	return (0 != tail[0]);
}

// See the header: mirrors PlayHUDMotion's candidate order (base + mark + token + tail, then without
// the mark), so a caller can ask "does this weapon author a _jammed variant of that motion?".
bool CHudItem::HasStateVariant(LPCSTR alias, LPCSTR st_tok)
{
	if (!alias || !alias[0] || !st_tok || !st_tok[0])	return false;

	string_path stem, tail, cand;
	SplitStateSuffix(alias, stem, tail);
	if (strstr(tail, st_tok))	return false;			// the alias already carries the token

	LPCSTR mark = GetFireModeMark(alias);
	if (mark[0])
	{
		strconcat(sizeof(cand), cand, stem, mark, st_tok, tail);
		if (isHUDAnimationExist(cand))	return true;
	}
	strconcat(sizeof(cand), cand, stem, st_tok, tail);
	return !!isHUDAnimationExist(cand);
}

u32 CHudItem::PlayHUDMotion(const shared_str& M, BOOL bMixIn, CHudItem*  W, u32 state)
{
	// An empty/absent alias (M == "" or null) would flow through MakeFireModeName -> a NULL shared_str
	// (the container docks "" to null) and crash in anim_play/motion_length on the null name. Bail early.
	if (!M.c_str() || !M.c_str()[0])
	{
		Msg("! [CHudItem::PlayHUDMotion] empty animation name on [%s] -- skipped", HudSection().c_str());
		return 0;
	}

	// GS name order (ModifierStd): base + firemode mark + weapon-state token, e.g.
	// anm_idle -> anm_idle_auto -> anm_idle_auto_jammed, anm_fakeshoot -> anm_fakeshoot_auto_jammed.
	// Resolve BOTH in one pass over a candidate list. Doing it in two existence-gated steps (mark,
	// then state) silently lost the mark whenever the INTERMEDIATE name isn't authored: e.g. the mp5
	// has anm_fakeshoot_auto_jammed but no anm_fakeshoot_auto, so the jammed dry-fire fell back to the
	// unmarked anm_fakeshoot_jammed -- the single-fire motion, which snaps the model's selector to "1".
	// Precedence of the state token is GS's: jammed > empty > first.
	LPCSTR st_tok = nullptr;
	if		(NeedJammedAnim())	st_tok = "_jammed";
	else if	(NeedEmptyAnim())	st_tok = "_empty";
	else if	(NeedFirstAnim())	st_tok = "_first";

	string_path stem, tail;
	SplitStateSuffix	(M.c_str(), stem, tail);			// the caller may already have passed tokens
	LPCSTR mark			= GetFireModeMark(M.c_str());		// "" when the weapon/mode has no mark
	// don't duplicate a state token the alias already carries (anm_reload_empty + "_empty")
	if (st_tok && strstr(tail, st_tok))	st_tok = nullptr;

	string_path cand;
	shared_str playM	= M;								// fallback: exactly what the caller asked for
	bool got			= false;
	if (mark[0] && st_tok)									// base + mark + state + tail
	{
		strconcat(sizeof(cand), cand, stem, mark, st_tok, tail);
		if (isHUDAnimationExist(cand))	{ playM = cand; got = true; }
	}
	if (!got && mark[0])									// base + mark + tail
	{
		strconcat(sizeof(cand), cand, stem, mark, tail);
		if (isHUDAnimationExist(cand))	{ playM = cand; got = true; }
	}
	if (!got && st_tok)										// base + state + tail (no firemode variants)
	{
		strconcat(sizeof(cand), cand, stem, st_tok, tail);
		if (isHUDAnimationExist(cand))	{ playM = cand; got = true; }
	}
	// ...and finally GS's outermost "_noscope" token, applied to whatever the above settled on: with no
	// scope mounted, <alias>_noscope wins when the config authors it (see NeedNoScopeAnim). Existence-
	// gated, so a weapon with no _noscope variants is untouched.
	if (NeedNoScopeAnim())
	{
		strconcat(sizeof(cand), cand, playM.c_str(), "_noscope");
		if (isHUDAnimationExist(cand))	playM = cand;
	}
	// GS PlaySoundByAnimName: a hud section may give ANY animation its own sound under the key
	// `snd_<alias>` (e.g. snd_anm_changefiremode_from_1_to_a = the gauss MUI powering up). The sounds are
	// pre-loaded by name in CWeaponMagazined::Load, so this is just a lookup; weapons without such keys
	// are untouched. The shotgun's per-reload-phase sounds use the same spelling.
	{
		string_path skey;	strconcat(sizeof(skey), skey, "snd_", playM.c_str());
		if (m_sounds.FindSoundItem(skey, false))
			PlaySound(skey, object().Position());
	}

	u32 anim_time					= PlayHUDMotion_noCB(playM, bMixIn);
	if (anim_time>0)
	{
		m_bStopAtEndAnimIsRunning = true;
		m_dwMotionStartTm			= Device.dwTimeGlobal;
		m_dwMotionCurrTm			= m_dwMotionStartTm;
		m_dwMotionEndTm				= m_dwMotionStartTm + anim_time;
		// GS lock_time (WeaponAdditionalBuffer.pas MakeLockByConfigParam): a config `lock_time_<anim>`
		// in the hud section ends the action state on that timer instead of the full motion length, so
		// the dead tail is skipped and the next anim blends in early (like GS's PlayCustomAnim). Opt-in
		// per anim -- absent key = unchanged (full length). Tries the resolved name (jammed/empty may
		// differ) then the base alias. Shorten-only, so a stray value can't stall a state longer than
		// the motion. This works for ANY action animation the config author keys, EXCEPT the aim/sprint
		// transitions which own dedicated timers (m_dwAimTransitionEndTm / m_dwSprintExitEndTm read the
		// same lock_time keys) -- shortening their state end here would race those handoffs.
		float lt = -1.f;
		if (0 != strncmp(M.c_str(), "anm_idle_aim", 12) && 0 != strncmp(M.c_str(), "anm_idle_sprint", 15))
		{
			string128 key;
			xr_sprintf(key, "lock_time_%s", playM.c_str());
			lt = READ_IF_EXISTS(pSettings, r_float, HudSection(), key, -1.f);
			if (lt < 0.f && playM != M)
			{
				xr_sprintf(key, "lock_time_%s", M.c_str());
				lt = READ_IF_EXISTS(pSettings, r_float, HudSection(), key, -1.f);
			}
		}
		if (lt >= 0.f)
		{
			u32 lock_end = m_dwMotionStartTm + u32(lt * 1000.f);
			if (lock_end < m_dwMotionEndTm)	m_dwMotionEndTm = lock_end;
		}
		m_startedMotionState		= state;
		StartActionDof			(playM.c_str());	// needs the timings above, so it goes last
	} else {
		m_bStopAtEndAnimIsRunning = false;
	}
	ArmPPE					(playM);	// ppe keyed to THIS alias, if the hud section asks for one
	TryPlayDetectorCompanion(playM);	// mirror this action on an out companion detector
	return anim_time;
}

// GS ReadActionDOFVector / ReadActionDOFSpeed_In. The animation that just started pulls the focus
// onto the hands. GS turns this on BY DEFAULT only for the aliases its reload-family selectors
// produce -- anm_reload, anm_reload_g, anm_open, anm_close, anm_add_cartridge (the `def=true` call
// sites in WeaponAnims.pas). Draw, holster, idle, shooting and the knife pass `def=false`, i.e. they
// blur only if the config explicitly asks with use_dof_<alias>. Values come from
// [gunslinger_base] default_action_dof_* and may be overridden per alias.
void CHudItem::StartActionDof(LPCSTR alias)
{
	// Hand the focus back before taking it again. Dropping the flag here instead (what this used to do)
	// leaked the effector: the very next motion after a DOF one is usually an idle with no use_dof_ key,
	// so it cleared the flag and returned -- and with the flag down, neither the release below nor
	// net_Destroy would ever restore. StopActionDof is a no-op unless a DOF is actually held, and
	// re-arming right after restoring interpolates from the same current value, so nothing flickers.
	StopActionDof			();
	if (!alias || !alias[0])			return;
	// Only the item the player is actually looking at -- HudItemData() is the direct question, and
	// it also keeps an NPC's reload from blurring the player's screen. But the item-use phantoms
	// (slot 10, gwr_eatable) start anm_show a beat BEFORE their model is attached -- the log showed
	// huddata=0 there and 1 only by the next motion -- so a plain refusal dropped the DOF for every
	// bandage and medkit. Park the request and let UpdateCL re-issue it on the frame the model
	// appears; the motion timings are already stamped, so the release still lands as configured.
	if (!HudItemData())
	{
		m_pending_dof_alias	= alias;
		m_pending_dof_until	= Device.dwTimeGlobal + 1000;	// a stale request must not fire later
		return;
	}

	static const char* dof_on_by_default[] = { "anm_reload", "anm_open", "anm_close", "anm_add_cartridge" };
	bool def = false;
	for (const char* p : dof_on_by_default)
		if (0 == strncmp(alias, p, xr_strlen(p)))	{ def = true; break; }

	LPCSTR hs = HudSection().c_str();
	if (!hs || !pSettings->section_exist(hs))	return;
	string_path key;
	strconcat(sizeof(key), key, "use_dof_", alias);
	if (!READ_IF_EXISTS(pSettings, r_bool, hs, key, def ? TRUE : FALSE))		return;

	const CGamePersistent::SDofDefaults& DD = CGamePersistent::DofDefaults();
	Fvector v = DD.action;
	strconcat(sizeof(key), key, "dof_", alias, "_near");	v.x = READ_IF_EXISTS(pSettings,r_float,hs,key,v.x);
	strconcat(sizeof(key), key, "dof_", alias, "_focus");	v.y = READ_IF_EXISTS(pSettings,r_float,hs,key,v.y);
	strconcat(sizeof(key), key, "dof_", alias, "_far");		v.z = READ_IF_EXISTS(pSettings,r_float,hs,key,v.z);

	strconcat(sizeof(key), key, "dof_speed_in_", alias);
	const float in_speed	= READ_IF_EXISTS(pSettings,r_float,hs,key,DD.speed_in);
	strconcat(sizeof(key), key, "dof_speed_out_", alias);
	m_fActionDofOutSpeed	= READ_IF_EXISTS(pSettings,r_float,hs,key,DD.speed_out);
	strconcat(sizeof(key), key, "dof_time_offset_", alias);
	m_iActionDofTimeOffsetMs = iFloor(READ_IF_EXISTS(pSettings,r_float,hs,key,DD.time_offset) * 1000.f);

	GamePersistent().SetEffectorDOF	(v, in_speed);
	m_bActionDofActive = true;
}

void CHudItem::StopActionDof()
{
	if (!m_bActionDofActive)		return;
	m_bActionDofActive = false;
	if (DofHeldByAim())				return;		// the aim owns the effector; leaving aim restores it
	GamePersistent().RestoreEffectorDOF	(m_fActionDofOutSpeed);
}

// If THIS is the active weapon (hud idx 0) and a detector is out (idx 1), play the detector's
// matching companion anim (anm_wpn_<action>). action = the played motion minus the "anm_" prefix.
void CHudItem::TryPlayDetectorCompanion(const shared_str& weaponMotion)
{
	if (m_bSuppressCompanion)		return;		// this item's motions must not drive the companion
	if (!g_player_hud)	return;
	attachable_hud_item* w = g_player_hud->attached_item(0);
	if (!w || w->m_parent_hud_item != this)	return;	// only the right-hand weapon drives the companion
	attachable_hud_item* d = g_player_hud->attached_item(1);
	if (!d || !d->m_parent_hud_item)		return;
	const char* wm = weaponMotion.c_str();
	if (0 != strncmp(wm, "anm_", 4))		return;
	// TRUE: this hook only runs when the weapon actually (re)started a motion, so the companion must
	// re-sync with it -- otherwise a repeated one-shot (dry-fire spam) plays on the weapon but not here.
	d->m_parent_hud_item->PlayCompanionAction(wm + 4, true);	// no-op unless it's a detector with anm_wpn_<action>
}

u32 CHudItem::PlayHUDMotion_noCB(const shared_str& motion_name, BOOL bMixIn)
{
	m_current_motion					= motion_name;
	m_bAttachedNoMotion					= false;	// something is playing again -- safe to draw

	if(bDebug && item().m_pInventory)
	{
		Msg("-[%s] as[%d] [%d]anim_play [%s][%d]",
			HudItemData()?"HUD":"Simulating", 
			item().m_pInventory->GetActiveSlot(), 
			item().object_id(),
			motion_name.c_str(), 
			Device.dwFrame);
	}
	if( HudItemData() )
	{
		return HudItemData()->anim_play		(motion_name, bMixIn, m_current_motion_def, m_started_rnd_anim_idx);
	}else
	{
		m_started_rnd_anim_idx				= 0;
		return g_player_hud->motion_length	(motion_name, HudSection(), m_current_motion_def );
	}
}

void CHudItem::StopCurrentAnimWithoutCallback()
{
	m_dwMotionStartTm			= 0;
	m_dwMotionEndTm				= 0;
	m_dwMotionCurrTm			= 0;
	m_bStopAtEndAnimIsRunning = false;
	m_current_motion_def		= NULL;
}

BOOL CHudItem::GetHUDmode()
{
	if(object().H_Parent())
	{
		CActor* A = smart_cast<CActor*>(object().H_Parent());
		return ( A && A->HUDview() && HudItemData() && HudItemData());
	}else
		return FALSE;
}

void CHudItem::PlayAnimIdle()
{
	// GS IsActionProcessing: no idle may start while a controller-suicide scene owns the pose. The
	// guard has to sit in EVERY PlayAnimIdle -- walking changes the movement anim through this call
	// directly, without a state switch, so the SwitchState(eIdle) block never sees it. That is why the
	// shot survived a standing victim and vanished for a walking one.
	{
		CWeapon* sw = smart_cast<CWeapon*>(this);
		if (sw && sw->SuicideHoldsPose())	return;
	}

	if (TryPlayAnimIdle()) return;

	PlayHUDMotion("anm_idle", TRUE, NULL, GetState());
}

// GS blowout (ActorUtils.pas ~2339, play_blowout_anim): when an emission's electronics-problems level passes
// this item's threshold, play the glitch animation `anm_blowout` (the PDA's pda_vibros = "выброс") ONCE. The
// threshold is tied to the SCREEN blackout (pda_black_frac * max_level) so the hand anim and the black always
// coincide. Called both from TryPlayAnimIdle (movement/cursor path) AND per-frame from CUIPdaWnd::Update, so it
// fires even while standing perfectly still (TryPlayAnimIdle alone isn't re-entered without a state change).
// Returns true if it started the anim this call. Requires eIdle && !pending so it never cuts a draw/gesture.
bool CHudItem::TryPlayBlowoutAnim()
{
	if (m_bBlowoutPlayed)	return false;
	if (GetState() != eIdle || IsPending())	return false;
	if (!READ_IF_EXISTS(pSettings, r_bool, HudSection(), "play_blowout_anim", FALSE) || !isHUDAnimationExist("anm_blowout"))	return false;

	extern float g_electronics_problems;
	float lvl = READ_IF_EXISTS(pSettings, r_float, HudSection(), "blowout_anim_level", 1000.f);
	const float bf = READ_IF_EXISTS(pSettings, r_float, "gwr_blowout", "pda_black_frac", -1.f);
	if (bf > 0.f)	lvl = bf * READ_IF_EXISTS(pSettings, r_float, "gwr_blowout", "max_level", 20.f);
	if (g_electronics_problems < lvl)	return false;

	// GS plays it exactly ONCE per PDA-open session; our phantom is recreated on every open, so the
	// constructor's m_bBlowoutPlayed=false is the re-arm (reopen the PDA to see it again).
	m_bBlowoutPlayed = true;
	if (m_sounds.FindSoundItem("sndBlowout", false))		// GS plays sndBlowout with the glitch anim
		PlaySound("sndBlowout", object().Position());
	PlayHUDMotion("anm_blowout", TRUE, this, GetState());
	m_dwBlowoutUntil = MotionEndTm();		// hold the cursor loop off until the glitch finishes
	return true;
}

bool CHudItem::TryPlayAnimIdle()
{
	// re-decided below; only the walk/sprint branches raise it again (see m_bIdleMoving)
	m_bIdleMoving = false;

	if (TryPlayBlowoutAnim())	return true;

	// 3D PDA: the cursor drives the idle. While it's off-centre we play anm_idle[_aim]<dir> and ignore
	// the movement/sprint variants entirely -- Gunslinger does the same (its anm_idle_up_moving,
	// anm_idle_up_crouch etc. all point at the very same pda_idle_up motion anyway).
	if(m_bPdaCursorAnims)
	{
		bool zoomed = IsHudItemZoomed();
		// Between the two halves of the split draw: hold the pose anm_show_fastzoom ended on
		// (pda_aim_draw_idle) rather than dropping to the lowered idle, which would undo the lift
		// a frame before anm_idle_aim_start_fastzoom picks it back up.
		if(!zoomed && gwr_pda_need_fastzoom() && isHUDAnimationExist("anm_idle_fastzoom"))
		{
			PlayHUDMotion("anm_idle_fastzoom", TRUE, this, GetState());
			return true;
		}
		if(g_pda_cursor_dir)
		{
			string_path nm;
			strconcat(sizeof(nm), nm, zoomed ? "anm_idle_aim" : "anm_idle", g_pda_dir_suffix[g_pda_cursor_dir]);
			if(isHUDAnimationExist(nm))
			{
				PlayHUDMotion(nm, TRUE, this, GetState());
				return true;
			}
		}
		// held to the face: the aim idle, never the walk one (and it's also the fallback when this
		// direction has no aim variant). Centred + not zoomed falls through to the normal
		// moving/idle selection below, so anm_idle_moving (pda_walk) still works.
		if(zoomed && isHUDAnimationExist("anm_idle_aim"))
		{
			PlayHUDMotion("anm_idle_aim", TRUE, this, GetState());
			return true;
		}
	}

	if(MovingAnimAllowedNow())
	{
		CActor* pActor = smart_cast<CActor*>(object().H_Parent());
		if(pActor)
		{
			CEntity::SEntityState st;
			pActor->g_State(st);
			// A companion hand holds off until the hand it follows starts its sprint (see
			// SprintAnimAllowedNow). Falling through leaves the ordinary moving idle playing and
			// clears m_bPrevSprint below, so the wait ends with a proper false->true edge and the
			// enter transition plays -- both hands from the same event, in phase, nothing to correct.
			if(st.bSprint && SprintAnimAllowedNow())
			{
				// false->true edge: sprint just began -> force the enter transition to play even if
				// m_bSprintStarted was left set by the previous state (e.g. aiming out into a sprint).
				if(!m_bPrevSprint)
					m_bSprintStarted = false;
				// ...and the same when the sprint never stopped but the idle slot was taken over in
				// between: throw a bolt while running and the detector mirrors the throw, so it is not
				// CONTINUING its sprint loop here, it is COMING BACK to it -- the enter is owed. The
				// right hand gets this for free (CMissile::OnStateSwitch drops the flag on the throw
				// states); without it the left hand jumped straight into the loop.
				// Same predicate as the sprint-EXIT gate below: every sprint motion is built on
				// SprintLoopBase() == "anm_idle_sprint" plus a class suffix, so the name carries
				// "sprint" for the loop and for the enter transition alike.
				// Do NOT do this when the companion STARTS instead: a throw is a SEQUENCE of mirrored
				// motions, and clearing it there replayed the enter in the MIDDLE of the throw. Between
				// two of them we never reach this line -- CCustomDetector::PlayAnimIdle mirrors the
				// weapon first and returns; we only get here once the weapon is back on its sprint.
				if(m_current_motion.size() && NULL == strstr(m_current_motion.c_str(), "sprint"))
					m_bSprintStarted = false;
				m_bPrevSprint = true;
				m_bIdleMoving = true;
				PlayAnimIdleSprint();
				return true;
			}
			m_bPrevSprint = false;
			// just stopped sprinting -> play the one-shot exit transition once (its OnAnimationEnd
			// routes back here, now with the flag cleared, to the normal moving/idle)
			// ...but ONLY while we are actually LEAVING a sprint motion. The idle slot can have been
			// taken over in between -- the detector mirrors the bolt's throw as a companion -- and the
			// exit is then owed to a sprint whose end nobody saw: it played on the LEFT hand alone,
			// after the actor had long since stopped, while the right hand went straight to its idle.
			// Clearing the debt without playing it is what the right hand does anyway
			// (CMissile::OnStateSwitch drops the flag when a throw starts). Do NOT clear it when the
			// companion STARTS instead: a throw is a sequence of mirrored motions and the detector
			// passes back through here between them, so a cleared flag replayed the sprint ENTER in
			// the middle of the throw. Still sprinting? The st.bSprint branch above wins first.
			// Every sprint motion is built on SprintLoopBase() == "anm_idle_sprint" plus a class
			// suffix, so both the loop and the enter transition carry "sprint" in the name.
			const bool owed_sprint_end	= m_bSprintStarted && m_current_motion.size() &&
											  (NULL != strstr(m_current_motion.c_str(), "sprint"));
			m_bSprintStarted = false;
			if(owed_sprint_end)
			{
				string_path endnm;
				MakeSprintVariant(SprintLoopBase(), "end", endnm);	// suffix-correct exit (GL / bm16 shell)
				if(endnm[0] && isHUDAnimationExist(endnm))
				{
					PlayHUDMotion(endnm, TRUE, this, GetState());
					// block fire/aim until this exit anim is almost done, then FireStart/OnZoomIn resume.
					// Config `lock_time_anm_idle_sprint_end` (seconds from the anim start, Gunslinger-style)
					// tunes how many end frames are cut for responsiveness; default = full length - 130ms.
					u32 now = Device.dwTimeGlobal;
					// GS reads `lock_time_<FULLY RESOLVED anim name>` (WeaponAdditionalBuffer.pas:1059
					// builds anm_name through ModifierAlterSprint/ModifierStd first), so a weapon whose
					// sprint anims are numbered variants keys them per variant: the bm16 and the toz34
					// have lock_time_anm_idle_sprint_end_0/_1/_2 and NO base key at all. Asking for the
					// base name only found nothing there and dropped us into the fallback below.
					string128 lk;
					xr_sprintf(lk, "lock_time_%s", endnm);
					float lt = READ_IF_EXISTS(pSettings, r_float, HudSection(), lk, -1.f);
					if (lt < 0.f)
						lt = READ_IF_EXISTS(pSettings, r_float, HudSection(), "lock_time_anm_idle_sprint_end", -1.f);
					// No key at all = no lock, which is what GS does (MakeLockByConfigParam only acts
					// `if game_ini_line_exist`). The old fallback held fire for the whole motion minus
					// 130 ms -- on the bm16 that was 637 ms of a dead trigger and read as "the lock
					// system doesn't work here". Still set the deadline (to now) rather than 0: the
					// UpdateCL handoff needs a non-zero value to release a shot deferred mid-sprint.
					m_dwSprintExitEndTm = (lt >= 0.f) ? (now + (u32)(lt * 1000.f)) : now;
					return true;
				}
			}
			if(pActor->AnyMove())
			{
				// not accelerated (walk) -> slow variant; standing: walk<->walk_slow,
				// crouch: crouch<->crouch_slow (creep). Crouch branch only when the
				// weapon actually has crouch-move anims, else fall through to idle.
				bool accel = isActorAccelerated(pActor->MovingState(), pActor->IsZoomAimingMode());
				if(st.bCrouch)
				{
					if(!isHUDAnimationExist("anm_idle_moving_crouch"))
						return false;
					m_bStepCrouch = true;
				}
				m_bStepSlow   = !accel;
				m_bIdleMoving = true;
				PlayAnimIdleMoving();
				m_bStepSlow   = false;
				m_bStepCrouch = false;
				return true;
			}
		}
	}
	else
	{
		m_bSprintStarted = false;	// aiming (no moving anims): drop the sprint state so no stale exit later
		m_bSprintStartRunning = false;
		m_bPrevSprint    = false;	// so exiting aim into a sprint reads as a fresh edge -> plays the start
	}
	return false;
}

LPCSTR CHudItem::SelectMovingAnim(LPCSTR base)
{
	if(!m_bStepSlow && !m_bStepCrouch)
		return base;
	// base is "anm_idle_moving" + <weapon-state suffix> ("" / "_g" / "_w_gl" / "_empty"
	// / "_0"..). Compose posture(_crouch) + speed(_slow) + suffix, most-specific first
	// with graceful fallback: crouch_slow -> crouch -> slow -> base.
	static const size_t prefix_len = xr_strlen("anm_idle_moving");
	LPCSTR suffix = base + prefix_len;
	static string128 name;
	if(m_bStepCrouch && m_bStepSlow)
	{
		xr_sprintf(name, "anm_idle_moving_crouch_slow%s", suffix);
		if(isHUDAnimationExist(name)) return name;
		xr_sprintf(name, "anm_idle_moving_crouch%s", suffix);
		if(isHUDAnimationExist(name)) return name;
	}
	else if(m_bStepCrouch)
	{
		xr_sprintf(name, "anm_idle_moving_crouch%s", suffix);
		if(isHUDAnimationExist(name)) return name;
	}
	else // slow only
	{
		xr_sprintf(name, "anm_idle_moving_slow%s", suffix);
		if(isHUDAnimationExist(name)) return name;
	}
	return base;
}

void CHudItem::PlayAnimIdleMoving()
{
	{
		CWeapon* sw = smart_cast<CWeapon*>(this);
		if (sw && sw->SuicideHoldsPose())	return;
	}

	PlayHUDMotion(SelectMovingAnim("anm_idle_moving"), TRUE, NULL, GetState());
}

void CHudItem::MakeSprintVariant(LPCSTR loop_base, LPCSTR which, string_path& out)
{
	static const size_t plen = xr_strlen("anm_idle_sprint");
	if(loop_base && 0==strncmp(loop_base, "anm_idle_sprint", plen))
		xr_sprintf(out, "anm_idle_sprint_%s%s", which, loop_base + plen);	// insert _start/_end after the prefix
	else
		out[0] = 0;
}

bool CHudItem::HasSprintExitAnim()
{
	string_path e;
	MakeSprintVariant(SprintLoopBase(), "end", e);
	return e[0] && isHUDAnimationExist(e);
}

void CHudItem::PlayAnimIdleSprint()
{
	{
		CWeapon* sw = smart_cast<CWeapon*>(this);
		if (sw && sw->SuicideHoldsPose())	return;
	}

	LPCSTR loop = SprintLoopBase();	// class supplies the suffix (GL / bm16 shell); "" fallback = plain
	// The sprint-START one-shot is still on screen: do NOT replace it with the loop. This happens when a
	// second PlayAnimIdle fires the same frame right after the start began -- e.g. the aim-out transition's
	// timed handoff landing the tick the start played -- which would otherwise snap into the loop and cut
	// the start. Let the start finish; its OnAnimationEnd re-enters here (now past the end) and plays the loop.
	if(m_bSprintStartRunning && m_bStopAtEndAnimIsRunning && Device.dwTimeGlobal < m_dwMotionEndTm)
		return;
	// entering sprint: play the one-shot enter transition first (if it exists). Its OnAnimationEnd(eIdle)
	// routes back through PlayAnimIdle -> here with m_bSprintStarted set -> the loop.
	if(!m_bSprintStarted)
	{
		string_path start;
		MakeSprintVariant(loop, "start", start);
		if(start[0] && isHUDAnimationExist(start))
		{
			m_bSprintStarted     = true;
			m_bSprintStartRunning = true;
			PlayHUDMotion(start, TRUE, this, GetState());
			return;
		}
	}
	m_bSprintStarted     = true;	// no enter anim -> straight to the loop, but remember we're sprinting (for the exit)
	m_bSprintStartRunning = false;
	// The sprint loop is a CYCLIC motion: the animator keeps it running by itself, so re-playing it
	// only cross-fades the hands from wherever the cycle happens to be back to frame 0 -- read on a
	// run cycle (arms mid-swing) as a hard snap. We used to do that on EVERY speed/direction change,
	// because OnMovementChanged's HasMovementIdleVariant() fast path is guarded only by
	// m_bStopAtEndAnimIsRunning -- and that flag is ALWAYS false here, since player_hud::motion_length
	// returns 0 for a non-esmStopAtEnd motion, so no timer is ever armed. Gunslinger's engine keeps the
	// same guard but re-plays the idle only on mcSprint/mcAnyMove, so its loop simply runs (user
	// 2026-08-08, TT-33 -- identical omf and config on both sides, verified against GunsXRay-xd_dev).
	// Gated on the motion being cyclic: a StopAtEnd sprint loop still NEEDS the re-play, or the hands
	// would freeze on its last frame.
	if (m_bSprintLoopCyclic && m_sprint_loop_motion.size() && m_current_motion == m_sprint_loop_motion)
		return;
	PlayHUDMotion(loop, TRUE, this, GetState());
	// remember what it actually resolved to (may carry _empty / _jammed / a firemode mark)
	m_sprint_loop_motion = m_current_motion;
	m_bSprintLoopCyclic  = (m_current_motion_def && 0 == (m_current_motion_def->flags & esmStopAtEnd));
}

void CHudItem::OnMovementChanged(ACTOR_DEFS::EMoveCommand cmd)
{
	if(GetState()!=eIdle)
		return;

	// never cut an aim in/out transition short (it owns the eIdle slot until it ends)
	if(m_bIdleTransitionLock)
		return;

	// a one-shot gesture (dry-fire / jam inspect / bore) is playing in the idle slot: let it
	// finish — interrupting it with the moving idle broke the misfire inspect (its OnAnimationEnd
	// never fired, so the dry-fire flag stayed set and firing stuck). It refreshes to the right
	// (moving) idle itself when it ends.
	if(m_bStopAtEndAnimIsRunning)
		return;

	// Items with a movement-dependent idle (slow-walk, or directional aim-walk on
	// weapons) re-play IMMEDIATELY on any speed/direction change so the right variant
	// blends in (bMixIn) instead of only switching at the next cycle boundary.
	// PlayAnimIdle() routes to the aim idle when zoomed, the moving idle otherwise.
	if(HasMovementIdleVariant())
	{
		PlayAnimIdle						();
		ResetSubStateTime					();
		return;
	}

	// Plain items keep the original start/stop-at-cycle-boundary behaviour.
	if(!m_bStopAtEndAnimIsRunning)
	{
		if( (cmd == ACTOR_DEFS::mcSprint) || (cmd == ACTOR_DEFS::mcAnyMove)  )
		{
			PlayAnimIdle						();
			ResetSubStateTime					();
		}
	}
}

attachable_hud_item* CHudItem::HudItemData()
{
	attachable_hud_item* hi = NULL;
	if(!g_player_hud)		
		return				hi;

	hi = g_player_hud->attached_item(0);
	if (hi && hi->m_parent_hud_item == this)
		return hi;

	hi = g_player_hud->attached_item(1);
	if (hi && hi->m_parent_hud_item == this)
		return hi;

	return NULL;
}

// Live tuning knobs for the 3D PDA's "how close is it held" (console g_pda_hud_fov /
// g_pda_hud_fov_aim). 0 = use the item's config value. A SMALLER hud fov = narrower = the model
// looks BIGGER/closer; the engine default is psHUD_FOV_def (0.45).
float g_pda_hud_fov		= 0.f;
float g_pda_hud_fov_aim	= 0.f;

float CHudItem::GetHudFov()
{
	auto base = m_fHudFov ? m_fHudFov : psHUD_FOV_def;
	if(m_bPdaCursorAnims && g_pda_hud_fov > 0.f)
		base = g_pda_hud_fov;
	clamp(base, 0.1f, 1.0f);

	return base;
}

bool CHudItem::isHUDAnimationExist(LPCSTR anim_name)
{
	if (HudItemData())
	{
		string256 anim_name_r;
		sprintf(anim_name_r, "%s", anim_name);
		player_hud_motion* anm = HudItemData()->m_hand_motions.find_motion(anim_name_r);
		if (anm)
			return true;
	}
	else
	{
		if (g_player_hud->motion_length(anim_name, HudSection(), m_current_motion_def) > 100)
			return true;
	}

#ifdef DEBUG
	Msg("~ [WARNING] ------ Animation [%s] does not exist in [%s]", anim_name, HudSection().c_str());
#endif
	return false;
}
