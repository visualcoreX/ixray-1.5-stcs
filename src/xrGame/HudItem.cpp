#include "stdafx.h"
#include "HudItem.h"
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

ENGINE_API extern float psHUD_FOV_def;

CHudItem::CHudItem()
{
	RenderHud					(TRUE);
//	m_hud_item_shared_data		= NULL;
	m_bStopAtEndAnimIsRunning = false;
	m_current_motion_def		= NULL;
	m_started_rnd_anim_idx		= u8(-1);
	m_fHudFov					= 0.f;
	m_fHudFovAim				= 0.f;
	m_bStepSlow					= false;
	m_bStepCrouch				= false;
	m_bIdleTransitionLock		= false;
}

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

	m_fHudFov = READ_IF_EXISTS(pSettings, r_float, hud_sect, "hud_fov", 0.f);
	m_fHudFovAim = READ_IF_EXISTS(pSettings, r_float, hud_sect, "hud_fov_aim", 0.f);

	m_current_inertion.PitchOffsetR = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_pitch_offset_r", PITCH_OFFSET_R);
	m_current_inertion.PitchOffsetD = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_pitch_offset_d", PITCH_OFFSET_D);
	m_current_inertion.PitchOffsetN = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_pitch_offset_n", PITCH_OFFSET_N);

	m_current_inertion.OriginOffset = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_origin_offset", ORIGIN_OFFSET);
	m_current_inertion.TendtoSpeed = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_tendto_speed", TENDTO_SPEED);

	m_sounds.LoadSound(section, "snd_bore", "sndBore", true);
}


void CHudItem::PlaySound(LPCSTR alias, const Fvector& position)
{
	m_sounds.PlaySound	(alias, position, object().H_Root(), !!GetHUDmode());
}

void CHudItem::renderable_Render()
{
	UpdateXForm					();
	BOOL _hud_render			= ::Render->get_HUD() && GetHUDmode();
	
	if(_hud_render  && !IsHidden())
	{ 
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
				OnAnimationEnd						(m_startedMotionState);
			}
		}
	}
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
	if(m_current_motion_def)
	{
		PlayHUDMotion_noCB(m_current_motion, FALSE);
#ifdef DEBUG
		Msg("continue playing [%s][%d]",m_current_motion.c_str(), Device.dwFrame);
#endif // #ifdef DEBUG
	}
}

void CHudItem::MakeJammedName(LPCSTR name, string_path& out)
{
	// "_jammed" goes before a trailing GL suffix, else at the very end. NOTE: no numeric _0.._3
	// suffixes here — they collide with firemode tokens like anm_firemode_a_to_1 (the "_1").
	static const LPCSTR sfx[] = { "_gl_off", "_gl_on", "_w_gl", "_g" };
	int len = (int)xr_strlen(name);
	for (u32 i=0; i<sizeof(sfx)/sizeof(sfx[0]); ++i)
	{
		int sl = (int)xr_strlen(sfx[i]);
		if (len>sl && 0==xr_strcmp(name+len-sl, sfx[i]))
		{
			xr_strcpy	(out, name);
			out[len-sl]	= 0;
			xr_strcat	(out, "_jammed");
			xr_strcat	(out, sfx[i]);
			return;
		}
	}
	xr_strcpy	(out, name);
	xr_strcat	(out, "_jammed");
}

u32 CHudItem::PlayHUDMotion(const shared_str& M, BOOL bMixIn, CHudItem*  W, u32 state)
{
	shared_str playM = M;
	if (NeedJammedAnim())
	{
		string_path jam;
		MakeJammedName	(M.c_str(), jam);
		if (isHUDAnimationExist(jam))
			playM = jam;
	}
	u32 anim_time					= PlayHUDMotion_noCB(playM, bMixIn);
	if (anim_time>0)
	{
		m_bStopAtEndAnimIsRunning = true;
		m_dwMotionStartTm			= Device.dwTimeGlobal;
		m_dwMotionCurrTm			= m_dwMotionStartTm;
		m_dwMotionEndTm				= m_dwMotionStartTm + anim_time;
		m_startedMotionState		= state;
	} else {
		m_bStopAtEndAnimIsRunning = false;
	}
	return anim_time;
}


u32 CHudItem::PlayHUDMotion_noCB(const shared_str& motion_name, BOOL bMixIn)
{
	m_current_motion					= motion_name;

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
	if (TryPlayAnimIdle()) return;

	PlayHUDMotion("anm_idle", TRUE, NULL, GetState());
}

bool CHudItem::TryPlayAnimIdle()
{
	if(MovingAnimAllowedNow())
	{
		CActor* pActor = smart_cast<CActor*>(object().H_Parent());
		if(pActor)
		{
			CEntity::SEntityState st;
			pActor->g_State(st);
			if(st.bSprint)
			{
				PlayAnimIdleSprint();
				return true;
			}else
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
				PlayAnimIdleMoving();
				m_bStepSlow   = false;
				m_bStepCrouch = false;
				return true;
			}
		}
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
	PlayHUDMotion(SelectMovingAnim("anm_idle_moving"), TRUE, NULL, GetState());
}

void CHudItem::PlayAnimIdleSprint()
{
	PlayHUDMotion("anm_idle_sprint", TRUE, NULL,GetState());
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

float CHudItem::GetHudFov()
{
	auto base = m_fHudFov ? m_fHudFov : psHUD_FOV_def;
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
