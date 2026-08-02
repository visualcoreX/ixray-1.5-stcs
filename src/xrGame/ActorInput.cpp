#include "stdafx.h"
#include <dinput.h>
#include "Actor.h"
#include "Torch.h"
#include "WeaponMagazined.h"
#include "Missile.h"
#include "CustomOutfit.h"
#include "CustomDetector.h"
#include "player_hud.h"
#include <luabind/functor.hpp>
#include "ai_space.h"
#include "script_engine.h"
#include "trade.h"
#include "../xrEngine/CameraBase.h"

#ifdef DEBUG
#	include "PHDebug.h"
#endif

#include "hit.h"
#include "PHDestroyable.h"
#include "Car.h"
#include "HudManager.h"
#include "UIGameSP.h"
#include "inventory.h"
#include "level.h"
#include "game_cl_base.h"
#include "xr_level_controller.h"
#include "ActorEffector.h"				// CActorCameraManager (Cameras().Position()/Direction() for the kick aim)
#include "WeaponKnife.h"					// knife-in-hand quick kick -> normal attack
#include "WeaponAmmo.h"					// CCartridge (quick-kick melee)
#include "level_bullet_manager.h"		// Level().BulletManager() (quick-kick melee)
#include "../xrEngine/gamemtllib.h"		// GMLib (quick-kick melee material)
#include "UsableScriptObject.h"
#include "actorcondition.h"
#include "actor_input_handler.h"
#include "string_table.h"
#include "UI/UIStatic.h"
#include "CharacterPhysicsSupport.h"
#include "InventoryBox.h"
#include "player_hud.h"
#include "../xrEngine/xr_input.h"
#include "flare.h"
#include "CustomDetector.h"
#include "clsid_game.h"
#include "ShootingObject.h"
#include "HUDManager.h"
#include "UIGameCustom.h"
#include "UI.h"

static bool gwr_actor_hud_busy(CActor* actor);	// defined below; true while a weapon/eat/torch animation runs
static bool gwr_try_burn_use(CActor* actor);	// defined below; USE beats out a fire instead of using the world

bool g_bAutoClearCrouch = true;
extern u32 hud_adj_mode;

// Block weapon/slot switching while an item-use animation plays (toggled from Lua via console var).
int g_block_wpn_switch = 0;

// != 0 while an emission/surge is in progress. Set from Lua (xr_surge_hide) via the g_surge_active
// console var; drives the GS-style "electronics problems" device failure in UpdateElectronicsProblems.
int g_surge_active = 0;

// GS CurrentElectronicsProblemsCnt(): the ramped electronics-problems level (0 = normal). Updated in
// CActor::UpdateElectronicsProblems; read by other devices' per-frame render (e.g. CWeapon::UpdateLaserDot
// suppresses the laser dot while this is high, exactly like GS ProcessLaserdot).
float g_electronics_problems = 0.f;

// Length of THIS surge's hide window, in the CS surge's `time` units (xr_surge_hide.time_before_surge), set
// from Lua when a surge starts. The real hide-window duration (phase-3 hide task -> sky-red climax) is
// time*6 - 40 seconds (task_objects: surge_time = wait_time/10 - 40, wait_time = time*60). Lets the engine
// compute true surge progress so the PDA screen cycle spans the whole surge and blacks out AT the climax.
float g_surge_time = 30.f;

void CActor::IR_OnKeyboardPress(int cmd)
{
	if(hud_adj_mode && pInput->iGetAsyncKeyState(DIK_LSHIFT))	return;

	if (Remote())		return;

	if (IsTalking())	return;
	if (m_input_external_handler && !m_input_external_handler->authorized(cmd))	return;

	if (g_block_wpn_switch &&
		(cmd==kWPN_1 || cmd==kWPN_2 || cmd==kWPN_3 || cmd==kWPN_4 ||
		 cmd==kWPN_5 || cmd==kWPN_6 || cmd==kARTEFACT || cmd==kWPN_NEXT ||
		 cmd==kNEXT_SLOT || cmd==kPREV_SLOT))
		return;

	switch (cmd)
	{
	case kWPN_FIRE:
		{
			if( (mstate_wishful & mcLookout) && !IsGameTypeSingle() ) return;

			u32 slot = inventory().GetActiveSlot();
			if(inventory().ActiveItem() && (slot==RIFLE_SLOT || slot==PISTOL_SLOT) )
				mstate_wishful &=~mcSprint;
			//-----------------------------
			if (OnServer())
			{
				NET_Packet P;
				P.w_begin(M_PLAYER_FIRE);
				P.w_u16(ID());
				u_EventSend(P);
			}
		}break;
	case kWPN_ZOOM:
		{
			// aiming leaves sprint too (like fire): the weapon plays the sprint-out anim, then aims
			u32 slot = inventory().GetActiveSlot();
			if(inventory().ActiveItem() && (slot==RIFLE_SLOT || slot==PISTOL_SLOT) )
				mstate_wishful &=~mcSprint;
		}break;
	default:
		{
		}break;
	}

	if (!g_Alive()) return;

	// GS scope reticle/NV illumination up/down (scope_brightness_plus/minus) -- only while aiming a scope.
	if (cmd==kSCOPE_ILLUM_INC || cmd==kSCOPE_ILLUM_DEC)
	{
		CWeapon* w = smart_cast<CWeapon*>(inventory().ActiveItem());
		if (w && w->IsZoomed() && w->IsScopeAttached())
			w->ChangeScopeIllum(cmd==kSCOPE_ILLUM_INC ? +1 : -1);
		return;
	}

	// GS alter zoom (wpn_alter_zoom, GS binds the middle mouse button): toggle the active scope's SECOND aim
	// pose -- only meaningful while already aiming through a scope that declares alter_zoom_allowed.
	if (cmd==kWPN_ALTER_ZOOM)
	{
		CWeapon* w = smart_cast<CWeapon*>(inventory().ActiveItem());
		if (w && w->IsZoomed() && w->IsAlterZoomAllowed())
			w->ToggleAlterZoom();
		return;
	}

	if(m_holder && kUSE != cmd)
	{
		m_holder->OnKeyboardPress			(cmd);
		if(m_holder->allowWeapon() && inventory().Action(cmd, CMD_START))		return;
		return;
	}else
		if(inventory().Action(cmd, CMD_START))					return;

	switch(cmd)
	{
	case kJUMP:		
		{
			mstate_wishful |= mcJump;
		}break;
	case kCROUCH_TOGGLE:
		{
			g_bAutoClearCrouch = !g_bAutoClearCrouch;
			if (!g_bAutoClearCrouch)
				mstate_wishful |= mcCrouch;

		}break;
	case kSPRINT_TOGGLE:	
		{
			if (mstate_wishful & mcSprint)
				mstate_wishful &=~mcSprint;
			else
				mstate_wishful |= mcSprint;					
		}break;
	case kCAM_1:	cam_Set			(eacFirstEye);				break;
	case kCAM_2:	cam_Set			(eacLookAt);				break;
	case kCAM_3:	cam_Set			(eacFreeLook);				break;
	case kNIGHT_VISION:
		{
			SwitchNightVision();
			break;
		}
	case kTORCH:
		{
			SwitchTorch();
			break;
		}
	case kWPN_LASER:
		{
			SwitchWeaponLaser();
			break;
		}
	case kWPN_FLASHLIGHT:
		{
			SwitchWeaponFlashlight();
			break;
		}
	case kWPN_KICK:
		{
			// GS quick knife kick (быстрая атака ножом). If a KNIFE is already the active weapon, GS just
			// runs its normal attack (OnActorKick: virtual_Action FIRE) -- no holster/phantom, the knife in
			// hand simply stabs. Otherwise spawn the full-view knife-stab phantom (Lua): holster -> stab ->
			// redraw, with the binder firing the melee hit (quick_kick_hit) at the stab mark.
			CInventoryItem* akItem = inventory().ActiveItem();
			CWeaponMagazined* wmk = smart_cast<CWeaponMagazined*>(akItem);
			if (smart_cast<CWeaponKnife*>(akItem))
			{
				inventory().Action(kWPN_FIRE, CMD_START);
				inventory().Action(kWPN_FIRE, CMD_STOP);	// single stab (press+release)
			}
			else if (wmk && wmk->IsBayonetActive() && wmk->PlayHudActionAnim("anm_kick"))
			{
				// bayonet-equipped weapon (ak74), barrel clear of a silencer/GL: stab with ITS OWN anm_kick
				// (=ak74_bayonet), no knife phantom. If a silencer/GL is on, IsBayonetActive()==false -> falls
				// through to the generic knife-phantom kick below (and the blade bone is hidden in gwr_UpdateBones).
				// Schedule the melee hit at the stab mark (weapon is already out, so a fixed delay from now).
				u32 ht = READ_IF_EXISTS(pSettings, r_u32, wmk->cNameSect().c_str(), "bayonet_hit_time", 250);
				m_dwBayonetHitTm = Device.dwTimeGlobal + ht;
				wmk->PlayKickSound();	// GS snd_kick (its own snd_<anim> lookup for anm_kick)
			}
			else
			{
				luabind::functor<void>	fn;
				if (ai().script_engine().functor("gwr_eatable.on_quick_kick", fn))	fn();
			}
			break;
		}

	case kDETECTOR:
		{
			// don't draw/holster the detector mid jam-inspect (the weapon gesture owns the hands)
			{
				CWeaponMagazined* wmj = smart_cast<CWeaponMagazined*>(inventory().ActiveItem());
				if (wmj && wmj->IsJamInspectPlaying())	break;
			}
			PIItem det_active					= inventory().ItemFromSlot(DETECTOR_SLOT);
			if(det_active)
			{
				CCustomDetector* det			= smart_cast<CCustomDetector*>(det_active);
				det->ToggleDetector				(g_player_hud->attached_item(0)!=NULL);
				return;
			}
		}break;
/*
	case kFLARE:{
			PIItem fl_active = inventory().ItemFromSlot(FLARE_SLOT);
			if(fl_active)
			{
				CFlare* fl			= smart_cast<CFlare*>(fl_active);
				fl->DropFlare		();
				return				;
			}

			PIItem fli = inventory().Get(CLSID_DEVICE_FLARE, true);
			if(!fli)			return;

			CFlare* fl			= smart_cast<CFlare*>(fli);
			
			if(inventory().Slot(fl))
				fl->ActivateFlare	();
		}break;
*/
	case kUSE:
		// On fire? USE beats the flames out instead of poking at the world -- Gunslinger takes the
		// key over the same way. Only while actually burning, so normal use is untouched.
		if (gwr_try_burn_use(this))	break;
		ActorUse();
		break;
	case kDROP:
		b_DropActivated			= TRUE;
		f_DropPower				= 0;
		break;
	case kNEXT_SLOT:
		{
			OnNextWeaponSlot();
		}break;
	case kPREV_SLOT:
		{
			OnPrevWeaponSlot();
		}break;

	case kUSE_BANDAGE:
	case kUSE_MEDKIT:
		{
			if(IsGameTypeSingle())
			{
				PIItem itm = inventory().item((cmd==kUSE_BANDAGE)?  CLSID_IITEM_BANDAGE:CLSID_IITEM_MEDKIT );
				// don't quick-use (and don't print "used: ...") while a weapon/eat animation is playing --
				// the item wouldn't actually be applied (the gwr script hands it back)
				if(itm && !gwr_actor_hud_busy(this))
				{
					inventory().Eat				(itm);
					SDrawStaticStruct* _s		= HUD().GetUI()->UIGame()->AddCustomStatic("item_used", true);
					_s->m_endTime				= Device.fTimeGlobal+3.0f;
					string1024					str;
					strconcat					(sizeof(str),str,*CStringTable().translate("st_item_used"),": ", itm->NameItem());
					_s->wnd()->SetText			(str);
				}
			}
		}break;
	}
}

void CActor::IR_OnMouseWheel(int direction)
{
	if(hud_adj_mode)
	{
		g_player_hud->tune	(Ivector().set(0,0,direction));
		return;
	}

	if(inventory().Action( (direction>0)? kWPN_ZOOM_DEC:kWPN_ZOOM_INC , CMD_START)) return;

	if (g_block_wpn_switch)	return;

	if (direction>0)
		OnNextWeaponSlot				();
	else
		OnPrevWeaponSlot				();
}

void CActor::IR_OnKeyboardRelease(int cmd)
{
	if(hud_adj_mode && pInput->iGetAsyncKeyState(DIK_LSHIFT))	return;

	if (Remote())	return;

	if (m_input_external_handler && !m_input_external_handler->authorized(cmd))	return;

	if (g_Alive())	
	{
		if (cmd == kUSE) 
			PickupModeOff();

		if(m_holder)
		{
			m_holder->OnKeyboardRelease(cmd);
			
			if(m_holder->allowWeapon() && inventory().Action(cmd, CMD_STOP))		return;
			return;
		}else
			if(inventory().Action(cmd, CMD_STOP))		return;



		switch(cmd)
		{
		case kJUMP:		mstate_wishful &=~mcJump;		break;
		case kDROP:		if(GAME_PHASE_INPROGRESS == Game().Phase()) g_PerformDrop();				break;
		case kCROUCH:	g_bAutoClearCrouch = true;
		}
	}
}

void CActor::IR_OnKeyboardHold(int cmd)
{
	if(hud_adj_mode && pInput->iGetAsyncKeyState(DIK_LSHIFT))	return;

	if (Remote() || !g_Alive())					return;
	if (m_input_external_handler && !m_input_external_handler->authorized(cmd))	return;
	if (IsTalking())							return;

	if(m_holder)
	{
		m_holder->OnKeyboardHold(cmd);
		return;
	}

	float LookFactor = GetLookFactor();
	switch(cmd)
	{
	case kUP:
	case kDOWN: 
		cam_Active()->Move( (cmd==kUP) ? kDOWN : kUP, 0, LookFactor);									break;
	case kCAM_ZOOM_IN: 
	case kCAM_ZOOM_OUT: 
		cam_Active()->Move(cmd);												break;
	case kLEFT:
	case kRIGHT:
		if (eacFreeLook!=cam_active) cam_Active()->Move(cmd, 0, LookFactor);	break;

	case kACCEL:	mstate_wishful |= mcAccel;									break;
	case kL_STRAFE:	mstate_wishful |= mcLStrafe;								break;
	case kR_STRAFE:	mstate_wishful |= mcRStrafe;								break;
	case kL_LOOKOUT:mstate_wishful |= mcLLookout;								break;
	case kR_LOOKOUT:mstate_wishful |= mcRLookout;								break;
	case kFWD:		mstate_wishful |= mcFwd;									break;
	case kBACK:		mstate_wishful |= mcBack;									break;
	case kCROUCH:	mstate_wishful |= mcCrouch;									break;


	}
}

void CActor::IR_OnMouseMove(int dx, int dy)
{

	if(hud_adj_mode)
	{
		g_player_hud->tune	(Ivector().set(dx,dy,0));
		return;
	}

	PIItem iitem = inventory().ActiveItem();
	if(iitem && iitem->cast_hud_item())
		iitem->cast_hud_item()->ResetSubStateTime();

	if (Remote())		return;

	if(m_holder) 
	{
		m_holder->OnMouseMove(dx,dy);
		return;
	}

	float LookFactor = GetLookFactor();

	CCameraBase* C	= cameras	[cam_active];
	float scale		= (C->f_fov/g_fov)*psMouseSens * psMouseSensScale/50.f  / LookFactor;

	// Gunslinger zoom_mouse_sense_koef: slow the look while aiming a PiP lensed scope. Its world FOV stays wide
	// (all magnification is in the lens), so the f_fov/g_fov term above is ~1 and gives no slowdown -- the koef
	// supplies it, matching the strong lens zoom (lower koef for higher magnification).
	{
		CWeapon* pWpn = smart_cast<CWeapon*>(inventory().ActiveItem());
		if (pWpn && pWpn->IsZoomed() && pWpn->IsLensedScope())
			scale *= pWpn->ZoomMouseSenseKoef();
	}

	if (dx){
		float d = float(dx)*scale;
		cam_Active()->Move((d<0)?kLEFT:kRIGHT, _abs(d));
	}
	if (dy){
		float d = ((psMouseInvert.test(1))?-1:1)*float(dy)*scale*3.f/4.f;
		cam_Active()->Move((d>0)?kUP:kDOWN, _abs(d));
	}
}
#include "HudItem.h"
bool CActor::use_Holder				(CHolderCustom* holder)
{

	if(m_holder){
		bool b = false;
		CGameObject* holderGO			= smart_cast<CGameObject*>(m_holder);
		
		if(smart_cast<CCar*>(holderGO))
			b = use_Vehicle(0);
		else
			if (holderGO->CLS_ID==CLSID_OBJECT_W_STATMGUN)
				b = use_MountedWeapon(0);

		if(inventory().ActiveItem()){
			CHudItem* hi = smart_cast<CHudItem*>(inventory().ActiveItem());
			if(hi) hi->OnAnimationEnd(hi->GetState());
		}

		return b;
	}else{
		bool b = false;
		CGameObject* holderGO			= smart_cast<CGameObject*>(holder);
		if(smart_cast<CCar*>(holder))
			b = use_Vehicle(holder);

		if (holderGO->CLS_ID==CLSID_OBJECT_W_STATMGUN)
			b = use_MountedWeapon(holder);
		
		if(b){//used succesfully
			// switch off torch...
			CAttachableItem *I = CAttachmentOwner::attachedItem(CLSID_DEVICE_TORCH);
			if (I){
				CTorch* torch = smart_cast<CTorch*>(I);
				if (torch) torch->Switch(false);
			}
		}

		if(inventory().ActiveItem()){
			CHudItem* hi = smart_cast<CHudItem*>(inventory().ActiveItem());
			if(hi) hi->OnAnimationEnd(hi->GetState());
		}

		return b;
	}
}

void CActor::ActorUse()
{
	//mstate_real = 0;
	PickupModeOn();

		
	if (m_holder)
	{
		CGameObject*	GO			= smart_cast<CGameObject*>(m_holder);
		NET_Packet		P;
		CGameObject::u_EventGen		(P, GEG_PLAYER_DETACH_HOLDER, ID());
		P.w_u16						(GO->ID());
		CGameObject::u_EventSend	(P);
		return;
	}
				
	if(character_physics_support()->movement()->PHCapture())
		character_physics_support()->movement()->PHReleaseObject();

	

	if(m_pUsableObject)m_pUsableObject->use(this);
	
	if(m_pInvBoxWeLookingAt && m_pInvBoxWeLookingAt->nonscript_usable())
	{
		CUIGameSP* pGameSP = smart_cast<CUIGameSP*>(HUD().GetUI()->UIGame());
		if(pGameSP) pGameSP->StartCarBody(this, m_pInvBoxWeLookingAt );
		return;
	}

	if(!m_pUsableObject||m_pUsableObject->nonscript_usable())
	{
		if(m_pPersonWeLookingAt)
		{
			CEntityAlive* pEntityAliveWeLookingAt = 
				smart_cast<CEntityAlive*>(m_pPersonWeLookingAt);

			VERIFY(pEntityAliveWeLookingAt);

			if (IsGameTypeSingle())
			{			

				if(pEntityAliveWeLookingAt->g_Alive())
				{
					TryToTalk();
				}else

				//����� �����
				if(!Level().IR_GetKeyState(DIK_LSHIFT))
				{
					//������ ���� ��������� � ������ single
					CUIGameSP* pGameSP = smart_cast<CUIGameSP*>(HUD().GetUI()->UIGame());
					if(pGameSP)
						pGameSP->StartCarBody(this, m_pPersonWeLookingAt );
				}
			}
		}

		collide::rq_result& RQ = HUD().GetCurrentRayQuery();
		CPhysicsShellHolder* object = smart_cast<CPhysicsShellHolder*>(RQ.O);
		u16 element = BI_NONE;
		if(object) 
			element = (u16)RQ.element;

		if(object && Level().IR_GetKeyState(DIK_LSHIFT))
		{
			bool b_allow = !!pSettings->line_exist("ph_capture_visuals",object->cNameVisual());
			if(b_allow && !character_physics_support()->movement()->PHCapture())
			{
				character_physics_support()->movement()->PHCaptureObject( object, element );

			}

		}
		else
		{
			if (object && smart_cast<CHolderCustom*>(object))
			{
					NET_Packet		P;
					CGameObject::u_EventGen		(P, GEG_PLAYER_ATTACH_HOLDER, ID());
					P.w_u16						(object->ID());
					CGameObject::u_EventSend	(P);
					return;
			}

		}
	}
}

BOOL CActor::HUDview				( )const 
{ 
	return IsFocused() && (cam_active==eacFirstEye)&&
		((!m_holder) || (m_holder && m_holder->allowWeapon() && m_holder->HUDView() ) ); 
}

static	u32 SlotsToCheck [] = {
		KNIFE_SLOT		,		// 0
		PISTOL_SLOT		,		// 1
		RIFLE_SLOT		,		// 2
		GRENADE_SLOT	,		// 3
		APPARATUS_SLOT	,		// 4
		ARTEFACT_SLOT	,		// 10
};

void	CActor::OnNextWeaponSlot()
{
	u32 ActiveSlot = inventory().GetActiveSlot();
	if (ActiveSlot == NO_ACTIVE_SLOT) 
		ActiveSlot = inventory().GetPrevActiveSlot();

	if (ActiveSlot == NO_ACTIVE_SLOT) 
		ActiveSlot = KNIFE_SLOT;
	
	u32 NumSlotsToCheck = sizeof(SlotsToCheck)/sizeof(u32);	
	u32 CurSlot = 0;
	for (; CurSlot<NumSlotsToCheck; CurSlot++)
	{
		if (SlotsToCheck[CurSlot] == ActiveSlot) break;
	};
	if (CurSlot >= NumSlotsToCheck) return;
	for (u32 i=CurSlot+1; i<NumSlotsToCheck; i++)
	{
		if (inventory().ItemFromSlot(SlotsToCheck[i]))
		{
			if (SlotsToCheck[i] == ARTEFACT_SLOT) 
			{
				IR_OnKeyboardPress(kARTEFACT);
			}
			else
				IR_OnKeyboardPress(kWPN_1+(i-KNIFE_SLOT));
			return;
		}
	}
};

void	CActor::OnPrevWeaponSlot()
{
	u32 ActiveSlot = inventory().GetActiveSlot();
	if (ActiveSlot == NO_ACTIVE_SLOT) 
		ActiveSlot = inventory().GetPrevActiveSlot();

	if (ActiveSlot == NO_ACTIVE_SLOT) 
		ActiveSlot = KNIFE_SLOT;

	u32 NumSlotsToCheck = sizeof(SlotsToCheck)/sizeof(u32);	
	u32 CurSlot = 0;
	for (; CurSlot<NumSlotsToCheck; CurSlot++)
	{
		if (SlotsToCheck[CurSlot] == ActiveSlot) break;
	};
	if (CurSlot >= NumSlotsToCheck) return;
	for (s32 i=s32(CurSlot-1); i>=0; i--)
	{
		if (inventory().ItemFromSlot(SlotsToCheck[i]))
		{
			if (SlotsToCheck[i] == ARTEFACT_SLOT) 
			{
				IR_OnKeyboardPress(kARTEFACT);
			}
			else
				IR_OnKeyboardPress(kWPN_1+(i-KNIFE_SLOT));
			return;
		}
	}
};

float	CActor::GetLookFactor()
{
	if (m_input_external_handler) 
		return m_input_external_handler->mouse_scale_factor();

	
	float factor	= 1.f;

	PIItem pItem	= inventory().ActiveItem();

	if (pItem)
		factor *= pItem->GetControlInertionFactor();

	VERIFY(!fis_zero(factor));

	return factor;
}

void CActor::set_input_external_handler(CActorInputHandler *handler) 
{
	// clear state
	if (handler) 
		mstate_wishful			= 0;

	// release fire button
	if (handler)
		IR_OnKeyboardRelease	(kWPN_FIRE);

	// set handler
	m_input_external_handler	= handler;
}

// Notify a Lua handler for the headlamp/NV toggle. `spawn_left_hand` = play the generic left-hand
// headflash animator (ONLY when hands are empty); otherwise the script just plays the toggle sound.
static void gwr_call_action_animator(LPCSTR fn_name, bool on, bool spawn_left_hand)
{
	luabind::functor<void>	fn;
	if (ai().script_engine().functor(fn_name, fn))
		fn(on, spawn_left_hand);
}

// Delay (ms) between pressing the torch/NV key (starts the animation) and the light/NV actually
// toggling, so the effect syncs with the hand reaching the head. Tunable in console.
int g_torch_switch_delay = 350;
// Anti-spam + slot-block window (ms) covering the whole toggle gesture. Tune to your animation length.
int g_torch_action_time  = 1200;

// Pending deferred toggles + the shared anti-spam / slot-block window (single local actor).
static u32  s_torch_switch_at		= 0;
static u32  s_nv_switch_at			= 0;
static u32  s_action_busy_until		= 0;	// ignore new torch/NV presses while Device.dwTimeGlobal < this
static bool s_block_set_by_action	= false;// we raised g_block_wpn_switch and must lower it again

// Night-vision screen blackout (Gunslinger's goggles-over-the-eyes effect). Driven ENGINE-SIDE from
// the actor, NOT from the gesture animation, so it fires for EVERY weapon (a weapon playing its own
// anm_nv_on used to skip the left-hand phantom that carried the ppe -> no darken with e.g. the ak74)
// and always for a fixed short window instead of black.ppe's full ~3s (the phantom was destroyed on
// its timer before UpdateShowPPE could remove the effector). Timed around the (deferred) NV switch.
static u32  s_nv_black_on_at		= 0;
static u32  s_nv_black_off_at		= 0;
static bool s_nv_black_added		= false;
static const int s_nv_black_pre		= 150;	// ms the black starts BEFORE the nv actually switches
static const int s_nv_black_post	= 150;	// ms it lingers AFTER (GS spacing ~= 0.35..0.6 of the anim)

static CTorch* gwr_find_actor_torch(CActor* actor)
{
	xr_vector<CAttachableItem*> const& all = actor->attached_objects();
	xr_vector<CAttachableItem*>::const_iterator it = all.begin();
	xr_vector<CAttachableItem*>::const_iterator it_e = all.end();
	for ( ; it != it_e; ++it )
	{
		CTorch* torch = smart_cast<CTorch*>(*it);
		if (torch)	return torch;
	}
	return NULL;
}

// Open the anti-spam window and block slot switching for the duration of the gesture.
static void gwr_begin_torch_action()
{
	s_action_busy_until = Device.dwTimeGlobal + (u32)g_torch_action_time;
	if (g_block_wpn_switch == 0)		// don't stomp an item-use (eat) animation's own block
	{
		g_block_wpn_switch		= 1;
		s_block_set_by_action	= true;
	}
}

// True if ANY HUD animation is running, so a torch/NV toggle must be ignored entirely:
//  - g_block_wpn_switch != 0  -> an item-use (eat/medkit) animation OR a torch/NV gesture is playing
//  - active weapon not idle   -> reload / fire / draw / holster / another action
// The detector currently shown in the left hand (idx 1), or NULL.
static CCustomDetector* gwr_active_detector()
{
	if (!g_player_hud)	return NULL;
	attachable_hud_item* a = g_player_hud->attached_item(1);
	return a ? smart_cast<CCustomDetector*>(a->m_parent_hud_item) : NULL;
}

static bool gwr_actor_hud_busy(CActor* actor)
{
	if (g_block_wpn_switch != 0)	return true;
	CWeapon* w = smart_cast<CWeapon*>(actor->inventory().ActiveItem());
	if (w && (w->GetState() != CHUDState::eIdle || w->IsPending()))	return true;
	// the jam-inspect gesture plays in eIdle without pending -> catch it explicitly
	CWeaponMagazined* wm = smart_cast<CWeaponMagazined*>(w);
	if (wm && wm->IsJamInspectPlaying())	return true;
	// a detector in the left hand mid-gesture (or showing/hiding) is busy too, even though it's
	// not the "active item" -- otherwise a spammed toggle would fire with no animation
	CCustomDetector* det = gwr_active_detector();
	if (det && (det->GetState() != CHUDState::eIdle || det->IsPending()))	return true;
	return false;
}

// ---- burning actor (Gunslinger's burn_animator) ----
// "Burning" isn't a flag: it's a wound of hit type burn, exactly as GS reads it. Clear Sky already
// tracks those per type (CWound::TypeSize) and already sets the body alight via
// [entity_fire_particles], so this only adds the first-person half -- the hands, and a way to put
// yourself out faster than standing still.
static const char* GWR_BURN_SECT = "gwr_burn_animator";

bool gwr_actor_burning(CActor* actor)
{
	return actor && actor->conditions().BleedingSpeedByType(ALife::eHitTypeBurn) > 0.0f;
}

// One press = one full swing = out, the way it reads in Gunslinger. Not a hold, not a spam: USE
// brings the hands up once, fire_on_the_hand plays through, and at the end of that one cycle the
// burn is cleared and the hands come down. A second press while they're already up does nothing.
// true = we consumed the key.
static bool gwr_try_burn_use(CActor* actor)
{
	if (!gwr_actor_burning(actor))						return false;
	if (!pSettings->section_exist(GWR_BURN_SECT))		return false;

	PIItem it = actor->inventory().ActiveItem();
	if (it && 0 == xr_strcmp(it->object().cNameSect().c_str(), GWR_BURN_SECT))
		return true;								// already beating -> ignore the extra press
	if (gwr_actor_hud_busy(actor))						return true;	// mid reload/eat/gesture

	luabind::functor<void>	fn;
	if (ai().script_engine().functor("gwr_eatable.on_burn_use", fn))
		fn();
	return true;
}

// "You're on fire" prompt: up while burning with your hands down, gone the moment you bring them up
// or the fire dies. Its disappearance IS the "you stopped burning" feedback -- Gunslinger shows the
// same string, added/removed the same way (a custom static).
static void gwr_burn_msg(bool on)
{
	if (!HUD().GetUI() || !HUD().GetUI()->UIGame())	return;
	CUIGameCustom* g = HUD().GetUI()->UIGame();
	if (on)
	{
		if (!g->GetCustomStatic("gwr_burn_msg"))
			g->AddCustomStatic("gwr_burn_msg", true);
	}
	else
		g->RemoveCustomStatic("gwr_burn_msg");
}

// Called every frame from CActor::UpdateCL. Two jobs: the slow natural burn-down while your hands are
// down, and driving the one-shot beat-out gesture while the phantom is up.
void gwr_update_burning(CActor* actor)
{
	static bool	s_show_seen		= false;	// have we watched THIS phantom's draw start yet
	static bool	s_fx_started	= false;	// its flame particles have been spawned (once)

	PIItem it		= actor->inventory().ActiveItem();
	bool   phantom	= (it && 0 == xr_strcmp(it->object().cNameSect().c_str(), GWR_BURN_SECT));

	if (phantom)
	{
		gwr_burn_msg(false);				// hands are up; no prompt
		CHudItem* hi = it->cast_hud_item();

		// Fire on the hands: start it ONCE. burn_actor is a one-shot, so calling StartFlameParticles
		// every frame (as this used to) stacked a fresh copy each frame -- a wall of overlapping
		// particles that only thinned as they aged. That was the "too bright, fine by the end" look.
		if (!s_fx_started)
		{
			CShootingObject* so = smart_cast<CShootingObject*>(it);
			if (so) { so->StartFlameParticles(); s_fx_started = true; }
		}

		// Wait for the one draw cycle to finish (eShowing/pending seen, then back to a settled eIdle),
		// then put the fire out and drop the hands. That's the single, complete swing GS plays.
		if (hi)
		{
			if (hi->GetState() == CHUDState::eShowing || hi->IsPending())
				s_show_seen = true;

			if (s_show_seen && hi->GetState() == CHUDState::eIdle && !hi->IsPending())
			{
				actor->conditions().ChangeBleedingByType(1000.0f, ALife::eHitTypeBurn);	// fully out
				luabind::functor<void>	fn;
				if (ai().script_engine().functor("gwr_eatable.finish_burn", fn))	fn();
				s_show_seen = false;
				s_fx_started = false;
			}
		}
		return;
	}

	// no phantom in hand -> reset the per-gesture flags for next time
	s_show_seen = false;
	s_fx_started = false;

	if (!gwr_actor_burning(actor))	{ gwr_burn_msg(false); return; }

	// burning, hands down: nudge you to act, and let the fire die down slowly on its own
	gwr_burn_msg(true);
	float speed = READ_IF_EXISTS(pSettings, r_float, "gwr_burning", "actor_burn_restore_speed", 0.000002f);
	actor->conditions().ChangeBleedingByType(speed * float(Device.dwTimeDelta), ALife::eHitTypeBurn);
}

// Same test, for callers outside this file (the PDA key in CUIGameSP) -- resolves the actor itself.
bool gwr_actor_hud_busy_now()
{
	if (!g_pGameLevel || !g_pGameLevel->bReady)	return false;
	CActor* a = Actor();
	return a ? gwr_actor_hud_busy(a) : false;
}

// PDA in hand (the WP_BINOC phantom): play its own headlamp/NV toggle gesture (GS pda_headflash), the _aim
// variant when it's held to the face. Returns the PDA hud item if it handled it, so the caller skips the
// generic left-hand headflash phantom -- which would otherwise spawn into the PDA's OWN slot and fight it.
static CHudItem* gwr_pda_device_gesture(PIItem ai, LPCSTR base)
{
	// the PDA phantom is WP_BINOC = CWeaponBinoculars : CWeaponCustomPistol : CWeaponMagazined, so it carries
	// PlayHudActionAnim; UsesPdaCursorAnims() is what marks it as the PDA specifically.
	CWeaponMagazined* wm = smart_cast<CWeaponMagazined*>(ai);
	if (!wm || !wm->UsesPdaCursorAnims())	return nullptr;
	LPCSTR use = base;
	string64 aimnm;	xr_sprintf(aimnm, "%s_aim", base);
	if (wm->IsZoomed() && wm->isHUDAnimationExist(aimnm))	use = aimnm;			// at the face -> _aim clip
	if (wm->isHUDAnimationExist(use))	wm->PlayHudActionAnim(use);
	return wm;
}

void CActor::SwitchNightVision()
{
	{	CWeapon* aw = smart_cast<CWeapon*>(inventory().ActiveItem());
		CHudItem* ah = aw ? aw->cast_hud_item() : nullptr;
		if (ah && ah->UsesPdaCursorAnims())
		{
			// PDA out: it sets g_block_wpn_switch (blocks weapon SWITCH, not a device toggle) which
			// gwr_actor_hud_busy checks, so that guard would wrongly veto this. Just require the phantom
			// itself to be idle; allow the toggle even at the face (it has _aim gestures).
			if (aw->GetState() != CHUDState::eIdle || aw->IsPending())	return;
		}
		else
		{
			if (gwr_actor_hud_busy(this))	return;		// don't toggle while any animation (reload/eat/gesture) plays
			if (aw && aw->IsZoomed())		return;		// no device toggle while aiming a weapon (lower it first)
		}
	}
	CTorch* torch = gwr_find_actor_torch(this);
	if (!torch)	return;

	// night-vision must actually be available (outfit provides it, incl. via installed upgrade);
	// otherwise ignore the key entirely -- no toggle, no animation, no sound
	CCustomOutfit* outfit = GetOutfit();
	if (!outfit || outfit->m_NightVisionSect.size() == 0)	return;

	bool desired = !torch->GetNightVisionStatus();				// what it WILL become after the (deferred) toggle
	LPCSTR base = desired ? "anm_nv_on" : "anm_nv_off";
	PIItem ai = inventory().ActiveItem();
	CCustomDetector* det = gwr_active_detector();
	CWeaponMagazined* wpn = smart_cast<CWeaponMagazined*>(ai);
	CMissile* msl = smart_cast<CMissile*>(ai);
	CHudItem* pda = gwr_pda_device_gesture(ai, base);				// PDA phantom plays its own gesture (skips the phantom)
	if (pda)		{}
	else if (det)	det->PlayHudActionAnim(base);					// detector in the left hand
	else if (wpn)	wpn->PlayHudActionAnim(base);					// weapon's own left hand
	else if (msl)	msl->PlayHudActionAnim(base);					// bolt/grenade in the RIGHT hand (like GS); the left-hand headflash phantom below still plays too
	// generic left-hand headflash for empty hands OR a non-weapon item (knife/grenade/bolt/binoc);
	// never over a real (magazined) weapon, an out detector, or the PDA (it plays its own)
	gwr_call_action_animator("gwr_eatable.on_nv_switch", desired, det == NULL && wpn == NULL && pda == NULL);

	if (g_torch_switch_delay > 0)	s_nv_switch_at = Device.dwTimeGlobal + (u32)g_torch_switch_delay;
	else							torch->SwitchNightVision();

	// Schedule the goggles blackout around the switch moment (see s_nv_black_* notes). Clear any
	// prior one first so a rapid re-toggle can't leave a stale effector on.
	if (s_nv_black_added)	{ RemoveEffector(this, effActionAnimPPE); s_nv_black_added = false; }
	{
		u32 sw			= Device.dwTimeGlobal + (u32)((g_torch_switch_delay > 0) ? g_torch_switch_delay : 0);
		u32 on			= (sw > (u32)s_nv_black_pre) ? (sw - s_nv_black_pre) : Device.dwTimeGlobal;
		s_nv_black_on_at	= on;
		s_nv_black_off_at	= sw + s_nv_black_post;
	}
	gwr_begin_torch_action();
}

void CActor::SwitchTorch()
{
	{	CWeapon* aw = smart_cast<CWeapon*>(inventory().ActiveItem());
		CHudItem* ah = aw ? aw->cast_hud_item() : nullptr;
		if (ah && ah->UsesPdaCursorAnims())
		{
			// PDA out: it sets g_block_wpn_switch (blocks weapon SWITCH, not a device toggle) which
			// gwr_actor_hud_busy checks, so that guard would wrongly veto this. Just require the phantom
			// itself to be idle; allow the toggle even at the face (it has _aim gestures).
			if (aw->GetState() != CHUDState::eIdle || aw->IsPending())	return;
		}
		else
		{
			if (gwr_actor_hud_busy(this))	return;		// don't toggle while any animation (reload/eat/gesture) plays
			if (aw && aw->IsZoomed())		return;		// no device toggle while aiming a weapon (lower it first)
		}
	}
	CTorch* torch = gwr_find_actor_torch(this);
	if (!torch)	return;											// headlamp is always present, but guard anyway

	bool desired = !torch->torch_active();
	LPCSTR base = desired ? "anm_headlamp_on" : "anm_headlamp_off";
	PIItem ai = inventory().ActiveItem();
	CCustomDetector* det = gwr_active_detector();
	CWeaponMagazined* wpn = smart_cast<CWeaponMagazined*>(ai);
	CMissile* msl = smart_cast<CMissile*>(ai);
	CHudItem* pda = gwr_pda_device_gesture(ai, base);				// PDA phantom plays its own gesture (skips the phantom)
	if (pda)		{}
	else if (det)	det->PlayHudActionAnim(base);					// detector in the left hand
	else if (wpn)	wpn->PlayHudActionAnim(base);					// weapon's own left hand
	else if (msl)	msl->PlayHudActionAnim(base);					// bolt/grenade in the RIGHT hand (like GS); the left-hand headflash phantom below still plays too
	// generic left-hand headflash for empty hands OR a non-weapon item (knife/grenade/bolt/binoc);
	// never over a real (magazined) weapon, an out detector, or the PDA (it plays its own)
	gwr_call_action_animator("gwr_eatable.on_headlamp_switch", desired, det == NULL && wpn == NULL && pda == NULL);

	if (g_torch_switch_delay > 0)	s_torch_switch_at = Device.dwTimeGlobal + (u32)g_torch_switch_delay;
	else							torch->Switch();
	gwr_begin_torch_action();
}

// GS laser designator toggle (kWPN_LASER): plays the anm_laser_on/off gesture on the active weapon
// (SelectActionAnim adds the _empty/_jammed variants) and flips the beam at lock_time_start_anm_laser_*
// (GS winchester: 0.45s into the gesture). Weapons without the gesture toggle instantly.
void CActor::SwitchWeaponLaser()
{
	if (gwr_actor_hud_busy(this))	return;			// not during reload/eat/other gestures
	CWeapon* wpn = smart_cast<CWeapon*>(inventory().ActiveItem());
	if (!wpn || !wpn->IsLaserInstalled())	return;
	if (wpn->IsZoomed())	return;					// like torch/NV: lower the weapon first

	bool desired	= !wpn->IsLaserEnabled();
	LPCSTR base		= desired ? "anm_laser_on" : "anm_laser_off";
	u32 delay		= 0;
	CWeaponMagazined* wm = smart_cast<CWeaponMagazined*>(wpn);
	if (wm && wm->isHUDAnimationExist(base))
	{
		string128 lk;
		xr_sprintf(lk, "lock_time_start_%s", base);
		delay = u32(1000.f * READ_IF_EXISTS(pSettings, r_float, wpn->HudSection(), lk, 0.f));
		wm->PlayHudActionAnim(base);
		gwr_begin_torch_action();					// same slot-switch block window as the torch/NV gestures
	}
	// GS snd_laser_on/off (optional): play the matching toggle click
	if (wm)	wm->PlaySound(desired ? "sndLaserOn" : "sndLaserOff", wpn->get_LastFP());
	wpn->ScheduleLaserToggle(desired, delay);
}

// GS weapon-mounted flashlight toggle (kWPN_FLASHLIGHT): plays anm_torch_on/off, the light flips at
// lock_time_start_anm_torch_*. Same guards/block window as the laser & torch gestures.
void CActor::SwitchWeaponFlashlight()
{
	if (gwr_actor_hud_busy(this))	return;
	CWeapon* wpn = smart_cast<CWeapon*>(inventory().ActiveItem());
	if (!wpn || !wpn->IsFlashlightInstalled())	return;
	if (wpn->IsZoomed())	return;

	bool desired	= !wpn->IsFlashlightEnabled();
	LPCSTR base		= desired ? "anm_torch_on" : "anm_torch_off";
	u32 delay		= 0;
	CWeaponMagazined* wm = smart_cast<CWeaponMagazined*>(wpn);
	if (wm && wm->isHUDAnimationExist(base))
	{
		string128 lk;
		xr_sprintf(lk, "lock_time_start_%s", base);
		delay = u32(1000.f * READ_IF_EXISTS(pSettings, r_float, wpn->HudSection(), lk, 0.f));
		wm->PlayHudActionAnim(base);
		gwr_begin_torch_action();
	}
	if (wm)	wm->PlaySound(desired ? "sndFlashOn" : "sndFlashOff", wpn->get_LastFP());
	wpn->ScheduleFlashlightToggle(desired, delay);
}

// Called every frame from CActor::UpdateCL: fire pending deferred toggles and end the block window.
void CActor::UpdateDelayedDeviceSwitch()
{
	if (s_block_set_by_action && Device.dwTimeGlobal >= s_action_busy_until)
	{
		g_block_wpn_switch		= 0;
		s_block_set_by_action	= false;
	}

	// NV goggles blackout: add at s_nv_black_on_at, hard-remove at s_nv_black_off_at. Runs BEFORE the
	// early-out below because it must outlive s_nv_switch_at (the black lingers past the switch).
	if (s_nv_black_off_at != 0)
	{
		u32 now = Device.dwTimeGlobal;
		if (!s_nv_black_added && now >= s_nv_black_on_at)
		{
			AddEffector		(this, effActionAnimPPE, "gwr_effector_black");
			s_nv_black_added = true;
		}
		if (now >= s_nv_black_off_at)
		{
			if (s_nv_black_added)	RemoveEffector(this, effActionAnimPPE);
			s_nv_black_off_at	= 0;
			s_nv_black_added	= false;
		}
	}

	if (s_torch_switch_at == 0 && s_nv_switch_at == 0)	return;

	CTorch* torch = gwr_find_actor_torch(this);
	if (!torch)											// torch gone (dropped/unequipped) -> drop the pending toggles
	{
		s_torch_switch_at	= 0;
		s_nv_switch_at		= 0;
		return;
	}

	if (s_torch_switch_at != 0 && Device.dwTimeGlobal >= s_torch_switch_at)
	{
		s_torch_switch_at = 0;
		torch->Switch();
	}
	if (s_nv_switch_at != 0 && Device.dwTimeGlobal >= s_nv_switch_at)
	{
		s_nv_switch_at = 0;
		torch->SwitchNightVision();
	}
}

// Clear all torch/NV gesture state. Called on actor spawn/load so a save made mid-gesture (or a
// desynced Lua block mirror) can't leave the slot-switch block stuck on after loading.
void CActor::ResetTorchActionState()
{
	g_block_wpn_switch		= 0;
	s_action_busy_until		= 0;
	s_block_set_by_action	= false;
	s_torch_switch_at		= 0;
	s_nv_switch_at			= 0;
	if (s_nv_black_added)	RemoveEffector(this, effActionAnimPPE);
	s_nv_black_on_at		= 0;
	s_nv_black_off_at		= 0;
	s_nv_black_added		= false;
}

// GS blowout "ElectronicsProblems": while a surge runs (g_surge_active, set from Lua) a level ramps up
// (~1 unit / 2s); once it crosses `disabling_level` the actor's electronics glitch. Matching GS exactly,
// the ONLY actor device the surge touches is night vision -- and it is NOT switched off: GS kills just the
// NV *effector* (the green-screen postprocess) while the device stays logically ON, then re-lights it, so
// the visual flickers/glitches (CTorch__StopNvEffector, ActorUtils.pas ~2366). GS deliberately leaves the
// headlamp torch, the weapon laser and the weapon flashlight alone during a surge. (The PDA `anm_blowout`
// glitch anim and the fullscreen electronics shader are separate systems, not done here.) The level ramps
// back down after the surge, at which point the NV effector is restored. All numbers are tunable via the
// optional [gwr_blowout] config section (absent = the code defaults below). Called each frame from UpdateCL.
void CActor::UpdateElectronicsProblems()
{
	const float dsec = float(Device.dwTimeDelta) / 1000.f;
	const float cap  = READ_IF_EXISTS(pSettings, r_float, "gwr_blowout", "max_level", 20.f);

	// Drive the level from the ACTUAL surge PROGRESS (0 when the hide task begins -> 1 at the sky-red climax),
	// not a blind fixed-rate ramp -- so the whole PDA cycle spans THIS surge and blacks out AT the climax no
	// matter how long it runs. g_surge_active (Lua xr_surge_hide.surge_activated) marks the hide window;
	// g_surge_time (Lua xr_surge_hide.time_before_surge) gives its length: hide_dur = time*6 - 40 real seconds.
	extern float g_surge_time;
	static float s_surge_on_at = -1.f;
	static int   s_prev_active = 0;
	const float now = float(Device.dwTimeGlobal) / 1000.f;
	if (g_surge_active && !s_prev_active)	s_surge_on_at = now;	// rising edge = hide window start
	s_prev_active = g_surge_active;

	if (g_surge_active && s_surge_on_at >= 0.f)
	{
		float hide_dur = g_surge_time * 6.f - 40.f;
		if (hide_dur < 10.f)	hide_dur = 10.f;
		float p = (now - s_surge_on_at) / hide_dur;					// 0 at the hide task -> 1.0 at the sky-red climax
		clamp(p, 0.f, 2.f);											// allow >1.0 into the post-climax wave, so the black can be pushed past the climax (pda_black_frac up to ~1.15)
		g_electronics_problems = p * cap;							// smooth by construction (tracks wall time); may exceed cap past the climax
	}
	else
	{
		// surge over: ramp down fast so the boot/power-on screen (the decreasing branch below) is brief.
		const float down = READ_IF_EXISTS(pSettings, r_float, "gwr_blowout", "recover_per_sec", 6.f);
		g_electronics_problems -= down * dsec;
		if (g_electronics_problems < 0.f)	g_electronics_problems = 0.f;
	}

	// GS binder_affects (r_constants.pas): feed the electronics level into the m_affects shader constant that
	// the device-screen shaders read (model_pda_screen.ps, model_compscreen/clockarrow/digiclock/exoscreen) ->
	// the PDA and every other electronic screen tear/noise/black out during a surge. The engine already
	// registers m_affects from g_pGamePersistent->hud_affects (cl_affects binder) but kept it 0 for lack of an
	// electronics system -- now we drive it. .x = level/div (the shader glitch thresholds live in ~0.09..0.45),
	// .y = per-frame random noise seed, .z = .x, .w = 0 (>0 flips model_pda_screen.ps to its loading branch).
	if (g_pGamePersistent)
	{
		// model_pda_screen.ps runs the whole PDA cycle off m_affects=(x, random, x, DECREASING). Map surge
		// progress p=level/cap to x with the emission's phase breakpoints: p < pda_noise_frac -> tearing+noise
		// over the LIVE screen (x 0.09..0.27); noise_frac..black_frac -> the ui_deadpda self-diagnostic
		// sequence (x 0.27..0.41); past black_frac (the sky-red climax) -> FULL BLACK (x>=0.41). .w = the
		// DECREASING flag: the moment the surge ends and the level falls, the shader switches to loading_main =
		// the (halved) ui_pda_loadscreen power-on sequence, shown until x clears, then the live PDA returns.
		static float s_prev_elec = 0.f;
		const bool decreasing = (g_electronics_problems < s_prev_elec - 1e-4f);
		s_prev_elec = g_electronics_problems;

		// noise-over-LIVE-screen holds for MOST of the surge; the ui_deadpda dark phase + full black are packed
		// into the last (1 - pda_noise_frac) so the blackout lands right at the hand anim. The surge can run
		// 80-200s, so this fraction must be TINY or the dark phase is still many seconds early -- black_frac
		// 0.975 = full black at level 19.5 = the anim (blowout_anim_level), noise_frac 0.97 = a ~1% deadpda
		// flash just before it. Tunable via a [gwr_blowout] config section (no rebuild needed).
		const float nf = READ_IF_EXISTS(pSettings, r_float, "gwr_blowout", "pda_noise_frac", 0.97f);
		const float bf = READ_IF_EXISTS(pSettings, r_float, "gwr_blowout", "pda_black_frac", 0.975f);
		float p = (cap > 0.f) ? (g_electronics_problems / cap) : 0.f;
		clamp(p, 0.f, 2.f);		// p can exceed 1.0 in the post-climax wave (see the ramp above) -> bf can be >1.0
		float x;
		if (p <= 0.f)		x = 0.f;											// no surge -> clean live screen
		else if (p < nf)	x = 0.09f + (p / nf) * (0.27f - 0.09f);				// noise over the live screen
		else if (p < bf)	x = 0.27f + ((p - nf) / (bf - nf)) * (0.41f - 0.27f);	// ui_deadpda self-diagnostic
		else				x = 0.5f;												// full black (shader blacks at x>0.41); bf up to 1.0 safe (no /0)
		// Smooth ONSET: the noise phase starts at x=0.09, so without this the glitch pops in and the screen
		// jumps a few tones brighter the instant the surge begins. Ramp the whole effect in over the first
		// pda_fade_in_sec seconds of the surge (rising edge captured in s_surge_on_at). Doesn't touch the
		// post-surge recovery (by then now-on >> fade_t, so fade=1).
		const float fade_t = READ_IF_EXISTS(pSettings, r_float, "gwr_blowout", "pda_fade_in_sec", 5.f);
		if (fade_t > 0.f && s_surge_on_at >= 0.f && g_surge_active)
		{
			float fade = (now - s_surge_on_at) / fade_t;
			clamp(fade, 0.f, 1.f);
			x *= fade;
		}
		g_pGamePersistent->hud_affects.set(x, (x > 0.f) ? ::Random.randF() : 0.f, x, decreasing ? 1.f : 0.f);
	}

	// Night vision is glitched here (it has no per-frame render hook of its own like the laser dot does).
	// The laser dot, in contrast, reads g_electronics_problems straight from CWeapon::UpdateLaserDot.
	CTorch* torch = gwr_find_actor_torch(this);
	if (!torch || !torch->GetNightVisionStatus())	return;		// NV off -> nothing to glitch

	const float lvl = READ_IF_EXISTS(pSettings, r_float, "gwr_blowout", "disabling_level", 10.f);
	const float spd = READ_IF_EXISTS(pSettings, r_float, "gwr_blowout", "nv_disabling_speed", 1.0f);
	if (g_electronics_problems >= lvl)
	{
		// GS does NOT flicker NV: once the electronics-problems level crosses the threshold the green
		// effector is stopped ONCE (the goggle mask/stripes stay -- see CTorch::StopNightVisionEffector)
		// and it stays off for the rest of the surge. It comes back only when the level drops below the
		// threshold again (surge over). No re-lighting while the surge runs -> no strobe.
		if (torch->IsNightVisionEffectorActive())	torch->StopNightVisionEffector(spd);
	}
	else if (!torch->IsNightVisionEffectorActive())
	{
		torch->StartNightVisionEffector();		// surge over / below threshold -> restore the green effector
	}
}

// GS quick knife kick melee: a camera-forward "bullet" with the kick's hit params (like CWeaponKnife::
// KnifeStrike, but from the actor and capped at kick_distance so it's a short-range stab). Fired from
// UpdateCL at the scheduled anim mark. No Lua raypick exists in this engine, hence engine-side.
void CActor::QuickKickHit()
{
	// GS MakeWeaponKick reads the kick_* params from the ACTIVE weapon's section (with upgrade modifiers):
	// during the knife-phantom kick that's `quick_kick_animator` (distance 2), during a BAYONET stab it's the
	// weapon itself (ak74 -> distance 3, power 1.5 from base_bayonet). So read from whatever is in hand now.
	LPCSTR sect = "quick_kick_animator";
	CInventoryItem* ai = inventory().ActiveItem();
	if (ai)	sect = ai->object().cNameSect().c_str();
	if (!pSettings->section_exist(sect))	return;
	float power   = READ_IF_EXISTS(pSettings, r_float, sect, "kick_hit_power",     0.5f);
	float impulse = READ_IF_EXISTS(pSettings, r_float, sect, "kick_hit_impulse",   10.0f);
	float dist    = READ_IF_EXISTS(pSettings, r_float, sect, "kick_distance",      2.0f);
	float wm      = READ_IF_EXISTS(pSettings, r_float, sect, "kick_wallmark_size", 0.05f);

	// Aim from the actor's RAW look (r_torso yaw/pitch), sampled live at the mark -> it follows the player's
	// current turn like a normal knife, but WITHOUT the knife_kick_quick.anm camera wobble that Cameras().
	// Direction() carries (that wobble drifted the hit left). setXYZ(-pitch,yaw).k = the forward aim (see
	// get_box_mat in ActorCameras.cpp).
	Fvector pos = Cameras().Position();
	Fmatrix xf;	xf.setXYZ(-r_torso.pitch, r_torso.yaw, 0.f);
	Fvector dir;	dir.set(xf.k);	dir.normalize_safe();

	CCartridge cartridge;
	cartridge.param_s.buckShot		= 1;
	cartridge.param_s.impair		= 1.0f;
	cartridge.param_s.kDisp			= 1.0f;
	cartridge.param_s.kHit			= 1.0f;
	cartridge.param_s.kCritical		= 1.0f;
	cartridge.param_s.kImpulse		= 1.0f;
	cartridge.param_s.kAP			= 1.0f;
	cartridge.m_flags.set			(CCartridge::cfTracer,   FALSE);
	cartridge.m_flags.set			(CCartridge::cfRicochet, FALSE);
	cartridge.param_s.fWallmarkSize	= wm;
	cartridge.bullet_material_idx	= GMLib.GetMaterialIdx("objects\\knife");	// knife cut marks/impact (NOT "knife" -> invalid idx -> crash)

	Level().BulletManager().AddBullet(
		pos, dir,
		1000.0f,				// high speed -> lands within `dist` this frame
		power, power,			// hit + critical
		impulse,
		ID(),					// parent (the actor)
		ID(),					// weapon (no real weapon in the kick)
		ALife::eHitTypeWound,
		dist,					// only reaches kick_distance
		cartridge,
		true);
}
