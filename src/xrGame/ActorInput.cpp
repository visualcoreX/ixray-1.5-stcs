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
#include "HudItem.h"
#include "actor_flags.h"
#include "level.h"
#include "game_cl_base.h"
#include "xr_level_controller.h"
#include "ActorEffector.h"				// CActorCameraManager (Cameras().Position()/Direction() for the kick aim)
#include "WeaponKnife.h"
#include "WeaponRG6.h"
#include "WeaponRPG7.h"
#include "Grenade.h"					// knife-in-hand quick kick -> normal attack
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
#include "ui/UIActorMenu.h"	// quick-use slots redraw after a key use
#include "UIGameCustom.h"
#include "UI.h"
#include "game_cl_single.h"			// g_SingleGameDifficulty (GS suicide visibility rule)

// defined below; true while a weapon/eat/torch animation runs. allow_weapon_action = a reload or a
// jam does not count (the caller is allowed to cut those short -- see gwr_weapon_action_interruptible)
static bool gwr_actor_hud_busy(CActor* actor, bool allow_weapon_action = false);
static u32  gwr_gesture_lock_start(CHudItem* owner, LPCSTR base, u32 def_ms);	// defined below; GS lock_time_start_<anim>
static bool gwr_try_burn_use(CActor* actor);	// defined below; USE beats out a fire instead of using the world
// torch/NV gesture bookkeeping. Defined up here (rather than beside gwr_begin_torch_action, where
// they used to live) because the quick grenade drops the block to interrupt the gesture, and that
// runs earlier in the file.
static u32  s_action_busy_until		= 0;	// ignore new torch/NV presses while Device.dwTimeGlobal < this
static bool s_block_set_by_action	= false;// we raised g_block_wpn_switch and must lower it again
// A quick grenade that had to cancel an item-use gesture first: retry the slot activation until this
// deadline, because the cancelled phantom is released asynchronously (see kQUICK_GRENADE).
static u32  s_quick_grenade_until	= 0;

bool g_bAutoClearCrouch = true;
extern u32 hud_adj_mode;

// Block weapon/slot switching while an item-use animation plays (toggled from Lua via console var).
int g_block_wpn_switch = 0;

// Controller-grab diagnostic (console: g_ctrl_dbg 1). Prints every decision of the GS suicide
// state machine -- which branch was taken, what the knife selector handed out, when the pulse
// fires -- so a report like "the prepare animation does not play" can be read off the log.
int g_ctrl_dbg = 0;
#define CDBG(...)	do { if (g_ctrl_dbg) Msg(__VA_ARGS__); } while (0)

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

	// ...and a quick grenade throw owns the hands for its whole length. It cannot use
	// g_block_wpn_switch for that -- the throw drives CInventory::Activate itself and Activate
	// refuses to cross that flag -- so it gets its own input-only lock. Without it a weapon-slot key
	// pressed mid-throw simply cancelled the grenade.
	if ((g_block_wpn_switch || CMissile::QuickThrowBusy()) &&
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

	// GS blocks these keys outright for the duration of a grab / suicide scene (ActorUtils.pas:2568
	// kJUMP, :2570 the quick-use slots, :2629 the quick grenade). The victim is not allowed to heal
	// his way out, nor to lob a grenade clear; weapon slots are handled by g_block_wpn_switch.
	if (IsActorControlled() || IsSuicideInProgress() || IsControllerPreparing())
	{
		switch (cmd)
		{
		case kJUMP:
		case kUSE_BANDAGE:
		case kUSE_MEDKIT:
		case kARTEFACT:
		case kQUICK_USE_1:
		case kQUICK_USE_2:
		case kQUICK_USE_3:
		case kQUICK_USE_4:
		case kQUICK_GRENADE:
			return;
		default: break;
		}
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
			// GS mode: the key is HELD, so a press only asks for the sprint and the hold below
			// keeps asking. Handled here too, so a tap is not a frame late.
			if (psActorFlags.test(AF_GS_SPRINT))
			{
				if (CanSprintNow())	mstate_wishful |= mcSprint;
			}
			else if (mstate_wishful & mcSprint)
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
			// A no-weapon zone (sr_no_weapon -> hide_weapon) blocks every weapon slot, but the quick
			// stab never asked: with a weapon in hand it plays the bayonet anim on the hud item, and
			// with empty hands it spawns the knife phantom in the ARTEFACT slot -- the one slot
			// SetSlotsBlocked deliberately leaves open so eat/heal anims still work in a safe zone.
			// So the player could still stab people at a base. A stab is a weapon action: gate it on
			// the knife slot being blocked, which is exactly what hide_weapon does.
			if (inventory().m_slots[KNIFE_SLOT].IsBlocked())	break;

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
				// GS KickCallback (ActorUtils.pas:939) fires it at `lock_time_start_anm_kick` -- the same
				// per-animation timer every other gesture uses -- so prefer that when the hud section keys it;
				// `bayonet_hit_time` on the weapon section stays for configs that do not.
				u32 ht = READ_IF_EXISTS(pSettings, r_u32, wmk->cNameSect().c_str(), "bayonet_hit_time", 250);
				ht = gwr_gesture_lock_start(wmk, "anm_kick", ht);
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

	case kQUICK_GRENADE:
		{
			// GS quick grenade throw (ActorUtils.pas kQUICK_GRENADE): from any weapon, arm the throw and
			// switch to the grenade slot -- the grenade is then pulled and lobbed in a single animation
			// (anm_throw_quick, force_const) and the weapon we came from is drawn back afterwards.
			// Opt-in per grenade: `supports_quick_throw`.
			if (!IsGameTypeSingle())							break;
			if (inventory().GetActiveSlot() == (u32)GRENADE_SLOT)	break;	// it is already in hand
			if (!g_Alive())										break;

			// Same gate the quick stab got: a no-weapon zone (sr_no_weapon -> hide_weapon) blocks
			// every slot but the artefact one, so Activate(GRENADE_SLOT) below would refuse and no
			// grenade would ever come out. That refusal happens LAST, though -- by then this branch
			// has already cancelled a running item-use gesture, so at a base the key did nothing
			// except interrupt the medkit. Ask first.
			if (inventory().m_slots[GRENADE_SLOT].IsBlocked())	break;

			PIItem gr = inventory().ItemFromSlot(GRENADE_SLOT);
			if (!gr)
			{
				// nothing slotted (the last one was thrown and the slot was emptied) -- put one in first,
				// exactly like CInventory::Activate does for the grenade slot
				gr = inventory().SameSlot(GRENADE_SLOT, NULL, true);
				if (gr && !inventory().Slot(gr))		gr = NULL;
			}
			if (!smart_cast<CMissile*>(gr))						break;
			if (!READ_IF_EXISTS(pSettings, r_bool, gr->object().cNameSect().c_str(), "supports_quick_throw", FALSE))
				break;

			// GS lets the quick grenade INTERRUPT anything. ActorUtils.pas:2629 checks only the
			// grenade slot, the grab and the suicide -- unlike the weapon-slot keys right above it,
			// which do bail out on an item-use animator. So no hud-busy test here.
			// A reload or a draw is cut by the holster on its own; an item-use gesture is not,
			// because it holds g_block_wpn_switch, which CInventory::Activate refuses to cross --
			// so cancel it (the script drops the phantom, hands the item back and lowers the block),
			// and clear the shorter torch/NV gesture block, which is engine-side and separate.
			bool cancelled = false;
			{
				luabind::functor<bool> fn;
				if (ai().script_engine().functor("gwr_eatable.abandon_now", fn))	cancelled = fn();
			}
			if (s_block_set_by_action)
			{
				g_block_wpn_switch		= 0;
				s_block_set_by_action	= false;
				s_action_busy_until		= 0;
				cancelled				= true;
			}

			// The slot to come back to. While a gesture phantom is out THAT is the active slot, and
			// returning to it would be meaningless -- the script's own saved slot is the one the
			// gesture interrupted, so ask for it.
			u32 ret_slot = inventory().GetActiveSlot();
			if (ret_slot == (u32)ARTEFACT_SLOT)
			{
				luabind::functor<int> fs;
				ret_slot = NO_ACTIVE_SLOT;
				if (ai().script_engine().functor("gwr_eatable.interrupted_slot", fs))
				{
					const int s = fs();
					if (s >= 0)	ret_slot = (u32)s;
				}
			}

			// Remember whether the detector was out: the grenade is incompatible with it, so it gets
			// holstered on the way in and PutNextToSlot has to ask for it back afterwards.
			// NeedActivation() counts as "out" -- interrupting a RELOAD is the common case here, and a
			// reload has already put the detector away with a pending re-show, so IsWorking() alone
			// would lose it.
			// That pending re-show also has to be CANCELLED, or it fires in the one frame between the
			// old weapon detaching and the grenade attaching (nothing in hand -> the deferred draw
			// thinks it may go) and ToggleDetector then switches the actor to a detector-compatible
			// slot -- the bolt. On screen: the grenade appears, is yanked away, and you end up holding
			// the bolt and the detector.
			CCustomDetector* qdet = smart_cast<CCustomDetector*>(inventory().ItemFromSlot(DETECTOR_SLOT));
			const bool det_out = qdet && (qdet->IsWorking() || qdet->NeedActivation());
			if (qdet)	qdet->CancelRestore();
			CMissile::ArmQuickThrow	(ret_slot, det_out);

			// A cancelled gesture releases its phantom through alife, which lands over the next few
			// frames -- activating the grenade slot in the same frame raced that teardown and the
			// grenade never came out (the gesture just stopped). Defer instead: s_quick_grenade_until
			// makes CActor::UpdateCL retry until the hands are genuinely free.
			if (cancelled)
				s_quick_grenade_until = Device.dwTimeGlobal + 1500;
			else
				inventory().Activate	(GRENADE_SLOT);
		}break;

	case kQUICK_USE_1:
	case kQUICK_USE_2:
	case kQUICK_USE_3:
	case kQUICK_USE_4:
	case kUSE_BANDAGE:
	case kUSE_MEDKIT:
		{
			if(IsGameTypeSingle())
			{
				// CoP quick-use slots: the key names a SECTION the player parked in that slot, and any
				// item of it in the inventory is used. The two legacy keys keep their "first medkit /
				// first bandage by class" behaviour so an existing binding still works.
				PIItem itm = NULL;
				if (cmd >= kQUICK_USE_1 && cmd <= kQUICK_USE_4)
				{
					LPCSTR sect = ACTOR_DEFS::g_quick_use_slots[cmd - kQUICK_USE_1];
					if (sect && sect[0])	itm = ACTOR_DEFS::quick_use_resolve(inventory(), sect);
				}
				else
					itm = inventory().item((cmd==kUSE_BANDAGE)?  CLSID_IITEM_BANDAGE:CLSID_IITEM_MEDKIT );
				// don't quick-use (and don't print "used: ...") while a weapon/eat animation is playing --
				// the item wouldn't actually be applied (the gwr script hands it back). And never while
				// dead or dying: the use spawns a hud phantom whose binder then keeps working on a
				// corpse the engine is tearing down -- an access violation with no log. Found by
				// spamming the medkit key through a controller's tube.
				// `true` = a reload or a jam does NOT hold the key off: the item comes out and the
				// holster cuts the animation, like the quick grenade does.
				if(itm && g_Alive() && !gwr_actor_hud_busy(this, true))
				{
					inventory().Eat				(itm);
					SDrawStaticStruct* _s		= HUD().GetUI()->UIGame()->AddCustomStatic("item_used", true);
					_s->m_endTime				= Device.fTimeGlobal+3.0f;
					string1024					str;
					strconcat					(sizeof(str),str,*CStringTable().translate("st_item_used"),": ", itm->NameItem());
					_s->wnd()->SetText			(str);
					// the slot may have just run dry -- let the inventory redraw its reference, but only
					// if it is actually open (see CUIActorMenu::ReloadQuickSlots)
					if (HUD().GetUI() && HUD().GetUI()->UIGame() && HUD().GetUI()->UIGame()->ActorMenu().IsShown())
						HUD().GetUI()->UIGame()->ActorMenu().ReloadQuickSlots();
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
	if (CMissile::QuickThrowBusy())	return;		// same lock as the slot keys above

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
		case kCROUCH:	g_bAutoClearCrouch = true;		break;
		// GS mode only: letting go of the key ends the sprint. In the stock toggle mode the
		// release must do nothing, or a toggle would last exactly as long as the press.
		case kSPRINT_TOGGLE:
			if (psActorFlags.test(AF_GS_SPRINT))	mstate_wishful &=~mcSprint;
			break;
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

	// GS sprint, ActorUtils.pas:1557: while the key is down the sprint is re-asserted every
	// frame, and dropped on any frame the hands refuse it. That is what lets firing or aiming
	// out of a sprint work with the key still held -- the sprint gives way to the action and
	// comes back by itself once the hands are idle, instead of staying lost until the key is
	// pressed anew.
	case kSPRINT_TOGGLE:
		// While the key is down the sprint is (re)started on any frame the hands allow it. It is NOT
		// cleared here: stopping a sprint belongs to whoever had a reason to -- the fire/aim keys,
		// the action hook behind "Перезарядка во время спринта", the actor's own limits. A poll that
		// cleared it too made the hold option answer for all of that as well.
		if (psActorFlags.test(AF_GS_SPRINT) && !(mstate_wishful & mcSprint) && CanSprintNow())
			mstate_wishful |= mcSprint;
		break;


	}
}

// GS asks CanSprintNow(wpn) about the item in the hands (ActorUtils.pas:1558), so with empty
// hands there is nothing to refuse and a sprint always starts.
bool CActor::CanSprintNow()
{
	CHudItem* hi = smart_cast<CHudItem*>(inventory().ActiveItem());
	return (!hi) || hi->CanSprintNow();
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

	ApplyControlledMouse(dx, dy);	// GS: a controller twists the victim's own aim

	float LookFactor = GetLookFactor();

	CCameraBase* C	= cameras	[cam_active];
	float scale		= (C->f_fov/g_fov)*psMouseSens * psMouseSensScale/50.f  / LookFactor;

	// Gunslinger zoom_mouse_sense_koef: slow the look while aiming a PiP lensed scope. Its world FOV stays wide
	// (all magnification is in the lens), so the f_fov/g_fov term above is ~1 and gives no slowdown -- the koef
	// supplies it, matching the strong lens zoom (lower koef for higher magnification).
	{
		// IsLensedScopeCfg, not IsLensedScope: with the 3D lens switched off the magnification arrives as a
		// late FOV override in CCameraManager::ApplyDevice, which `C->f_fov` above never sees (currentFOV
		// returns the base fov for this optic in BOTH modes) -- so the f_fov/g_fov term is still 1 and the
		// koef is the ONLY thing slowing the look. Gating it on the option left 2D scopes at full speed.
		CWeapon* pWpn = smart_cast<CWeapon*>(inventory().ActiveItem());
		if (pWpn && pWpn->IsZoomed() && pWpn->IsLensedScopeCfg())
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
// GS monster kick (ActorUtils.pas _planned_kick_animator): when a boar knocks the item out of the
// hands, the flinch animation is a HANDS motion (boar_hit_front / _front1 / _back, they live in
// wpn_hand_animation.omf) and it can only be shown by a hud phantom -- which in turn needs EMPTY
// hands. The drop itself lands a frame later, so the request waits here for the hands to clear,
// exactly like GS pumps _planned_kick_animator from its actor update.
static bool s_kick_planned		= false;
static bool s_kick_from_back	= false;
static u32  s_kick_expire_at	= 0;

void gwr_plan_monster_kick(bool from_back)
{
	if (s_kick_planned)	return;

	s_kick_planned		= true;
	s_kick_from_back	= from_back;
	s_kick_expire_at	= Device.dwTimeGlobal + 2500;
}

static void gwr_call_action_animator(LPCSTR fn_name, bool on, bool spawn_left_hand)
{
	luabind::functor<void>	fn;
	if (ai().script_engine().functor(fn_name, fn))
		fn(on, spawn_left_hand);
}

// Delay (ms) between pressing the torch/NV key (starts the animation) and the light/NV actually
// toggling, so the effect syncs with the hand reaching the head. Tunable in console.
int g_torch_switch_delay = 500;	// was 350
// Anti-spam + slot-block window (ms) covering the whole toggle gesture. Tune to your animation length.
int g_torch_action_time  = 1200;

// Pending deferred toggles + the shared anti-spam / slot-block window (single local actor).
static u32  s_torch_switch_at		= 0;
static u32  s_nv_switch_at			= 0;

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

// GS arms EVERY gesture off a per-animation lock (MakeLockByConfigParam): the work happens at
// `lock_time_start_<anim>` -- the frame the hand actually reaches the switch/target -- and only then does
// it lock again for `lock_time_end_<anim>`. HeadlampCallback / NVCallback / KickCallback (ActorUtils.pas
// :904/:922/:939) are exactly that. Read that value from the HUD section of whatever item plays the
// gesture; `def_ms` is what we used before (a single global constant) and stays as the fallback for items
// or animations that do not key it. The `_empty`/`_jammed`/`_auto`/`_w_gl` variants all carry the SAME
// value in every shipped config, so the plain alias is enough -- which is just as well, since
// PlayHudActionAnim is deferred and the resolved variant is not known yet at the call site.
static u32 gwr_gesture_lock_start(CHudItem* owner, LPCSTR base, u32 def_ms)
{
	if (!owner || !base || !base[0])	return def_ms;
	string128 key;
	xr_sprintf	(key, "lock_time_start_%s", base);
	float v = READ_IF_EXISTS(pSettings, r_float, owner->HudSection(), key, -1.f);
	return (v >= 0.f) ? u32(v * 1000.f) : def_ms;
}

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

// A RELOAD and a JAM may be cut short -- using an item is allowed to interrupt them, the way the
// quick grenade throw interrupts everything (GS ActorUtils.pas:2629): the holster that follows the
// item's activation kills whatever animation was playing. A draw, a holster, a shot cycle or another
// gesture is NOT interruptible -- those own the hands for a reason. Shared by the quick-use key here
// and by the script binding the eat script asks (CScriptGameObject::active_item_uninterruptible).
bool gwr_weapon_action_interruptible(CInventoryItem* itm)
{
	CWeaponMagazined* wm = smart_cast<CWeaponMagazined*>(itm);
	if (!wm)	return false;
	const u32 st = wm->GetState();
	// clearing a jam IS a reload, so eReload covers both; eMisfire is the jam itself
	if (st == CWeapon::eReload || st == CWeapon::eMisfire)			return true;
	// a jammed weapon finishing anm_shoot_jammed, or playing the jam-inspect gesture, sits in eIdle
	if (st == CHUDState::eIdle && (wm->IsMisfire() || wm->IsJamInspectPlaying()))	return true;
	return false;
}

static bool gwr_actor_hud_busy(CActor* actor, bool allow_weapon_action)
{
	if (g_block_wpn_switch != 0)	return true;
	// ANY hud item in hand that is not sitting idle owns the hands, not just a weapon. This used to
	// cast to CWeapon, which let a grenade through: a CMissile is not a CWeapon, so a throw looked
	// like empty hands, and a quick-use key mid-throw printed "item used" for an item the eat script
	// then handed straight back.
	PIItem ai_ = actor->inventory().ActiveItem();
	CHudItem* h = ai_ ? ai_->cast_hud_item() : NULL;
	const bool cuttable = allow_weapon_action && gwr_weapon_action_interruptible(ai_);
	if (!cuttable)
	{
		if (h && (h->GetState() != CHUDState::eIdle || h->IsPending()))	return true;
		// the jam-inspect gesture plays in eIdle without pending -> catch it explicitly
		CWeaponMagazined* wm = smart_cast<CWeaponMagazined*>(ai_);
		if (wm && wm->IsJamInspectPlaying())	return true;
	}
	// a detector in the left hand mid-gesture (or showing/hiding) is busy too, even though it's
	// not the "active item" -- otherwise a spammed toggle would fire with no animation
	CCustomDetector* det = gwr_active_detector();
	if (det && (det->GetState() != CHUDState::eIdle || det->IsPending()))	return true;

	// An item-use / PDA gesture that has already dropped the block but whose phantom is still in the
	// inventory: the Lua binder releases the object a frame or two before it actually goes away, and
	// starting a second use in that window put two phantoms in slot 10 and clobbered the binder's
	// (module-wide) saved slot bookkeeping. Crash with no log, reproducible by using a quick slot
	// right after closing the PDA.
	// ...but only while it is FRESH. That window is a frame or two; a phantom still sitting there
	// SECONDS later is a leak -- an effect carrier (<item>_eatable, same fake visual) whose eat never
	// happened, say -- and it must not jam every gesture for the rest of the save. After the grace
	// period it stops counting, so the game heals itself instead of going permanently dead-handed.
	// (A real gesture is covered by g_block_wpn_switch above anyway.)
	{
		static u32 s_phantom_seen_at = 0;
		bool phantom = false;
		const TIItemContainer& all = actor->inventory().m_all;
		for (TIItemContainer::const_iterator it = all.begin(); it != all.end(); ++it)
			if (CActor::IsGesturePhantom(*it))	{ phantom = true; break; }

		if (!phantom)					s_phantom_seen_at = 0;
		else
		{
			if (!s_phantom_seen_at)		s_phantom_seen_at = Device.dwTimeGlobal;
			if (Device.dwTimeGlobal - s_phantom_seen_at < 3000)	return true;
		}
	}
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

// Same test, for callers outside this file -- resolves the actor itself. allow_weapon_action = a
// reload or a jam is fair game to cut short (CInventory::Eat passes it: using an item interrupts
// them). The PDA key in CUIGameSP does not -- drawing the PDA over a reload is not wanted.
bool gwr_actor_hud_busy_now(bool allow_weapon_action)
{
	if (!g_pGameLevel || !g_pGameLevel->bReady)	return false;
	CActor* a = Actor();
	return a ? gwr_actor_hud_busy(a, allow_weapon_action) : false;
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
	// GS OnActorSwithesSmth (ActorUtils.pas:871): the gesture is played on the ITEM IN HAND, and the
	// detector only MIRRORS it through the companion table (its anm_lefthand_<det>_wpn_nv_on = our
	// anm_wpn_nv_on). Handing it to the detector INSTEAD left the right hand idle whenever a detector
	// was out. The mirror costs nothing here -- PlayHUDMotion's companion hook does it.
	CHudItem* gesture = pda;									// whoever ends up playing it owns the timing
	bool played = (pda != NULL);
	if (!played && wpn)	{ played = wpn->PlayHudActionAnim(base);	if (played) gesture = wpn; }	// weapon in the right hand
	if (!played && msl)	{ played = msl->PlayHudActionAnim(base);	if (played) gesture = msl; }	// bolt/grenade in the right hand
	if (!played && det)	{ det->PlayHudActionAnim(base);				gesture = det; }			// detector alone (or the item has no gesture)
	// generic left-hand headflash for empty hands OR a non-weapon item (knife/grenade/bolt/binoc);
	// never over a real (magazined) weapon, an out detector, or the PDA (it plays its own)
	gwr_call_action_animator("gwr_eatable.on_nv_switch", desired, det == NULL && wpn == NULL && pda == NULL);

	// GS NVCallback: the goggles flip at lock_time_start_<gesture>, not on the keypress
	const u32 nv_delay = gwr_gesture_lock_start(gesture, base, (g_torch_switch_delay > 0) ? (u32)g_torch_switch_delay : 0);
	if (nv_delay > 0)	s_nv_switch_at = Device.dwTimeGlobal + nv_delay;
	else				torch->SwitchNightVision();

	// Schedule the goggles blackout around the switch moment (see s_nv_black_* notes). Clear any
	// prior one first so a rapid re-toggle can't leave a stale effector on.
	if (s_nv_black_added)	{ RemoveEffector(this, effActionAnimPPE); s_nv_black_added = false; }
	{
		u32 sw			= Device.dwTimeGlobal + nv_delay;	// follows the gesture's own lock_time_start
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
	if (!torch)	return;											// the light-carrying device itself must exist

	// There is no headlamp of one's own any more: the lamp comes with the SUIT and only while it is
	// worn (`torch_enabled`, granted by the suit's flashlight upgrade). Without one the key does
	// nothing at all -- no toggle, no gesture, no sound -- exactly like night vision above.
	{
		CCustomOutfit* outfit = GetOutfit();
		if (!outfit || !outfit->m_bTorch)	return;
	}

	bool desired = !torch->torch_active();
	LPCSTR base = desired ? "anm_headlamp_on" : "anm_headlamp_off";
	PIItem ai = inventory().ActiveItem();
	CCustomDetector* det = gwr_active_detector();
	CWeaponMagazined* wpn = smart_cast<CWeaponMagazined*>(ai);
	CMissile* msl = smart_cast<CMissile*>(ai);
	CHudItem* pda = gwr_pda_device_gesture(ai, base);				// PDA phantom plays its own gesture (skips the phantom)
	// Same as the NV toggle above (GS OnActorSwithesSmth): the WEAPON plays anm_headlamp_on/off and the
	// detector mirrors it as a companion, instead of the detector taking the gesture and the right hand
	// standing still (user 2026-08-05: "при включении налобного фонаря правая рука не играет анимацию").
	CHudItem* gesture = pda;									// whoever ends up playing it owns the timing
	bool played = (pda != NULL);
	if (!played && wpn)	{ played = wpn->PlayHudActionAnim(base);	if (played) gesture = wpn; }	// weapon in the right hand
	if (!played && msl)	{ played = msl->PlayHudActionAnim(base);	if (played) gesture = msl; }	// bolt/grenade in the right hand
	if (!played && det)	{ det->PlayHudActionAnim(base);				gesture = det; }			// detector alone (or the item has no gesture)
	// generic left-hand headflash for empty hands OR a non-weapon item (knife/grenade/bolt/binoc);
	// never over a real (magazined) weapon, an out detector, or the PDA (it plays its own)
	gwr_call_action_animator("gwr_eatable.on_headlamp_switch", desired, det == NULL && wpn == NULL && pda == NULL);

	// GS HeadlampCallback: the lamp flips at lock_time_start_<gesture> (the ak74 keys 0.58 s, well past
	// our old flat 350 ms), so the light comes on as the hand reaches the switch
	const u32 tr_delay = gwr_gesture_lock_start(gesture, base, (g_torch_switch_delay > 0) ? (u32)g_torch_switch_delay : 0);
	if (tr_delay > 0)	s_torch_switch_at = Device.dwTimeGlobal + tr_delay;
	else				torch->Switch();
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
	// A quick grenade that interrupted an item-use gesture: the cancelled phantom is released through
	// alife, so it is still the active item for a frame or two and activating the grenade slot right
	// away simply lost the request. Retry until the phantom is gone and the block is down.
	if (s_quick_grenade_until)
	{
		const bool timed_out = Device.dwTimeGlobal >= s_quick_grenade_until;
		bool phantom_out = false;
		const TIItemContainer& all = inventory().m_all;
		for (TIItemContainer::const_iterator it = all.begin(); it != all.end(); ++it)
			if (CActor::IsGesturePhantom(*it))	{ phantom_out = true; break; }

		if (!phantom_out && g_block_wpn_switch == 0)
		{
			s_quick_grenade_until = 0;
			inventory().Activate(GRENADE_SLOT);
		}
		else if (timed_out)
		{
			s_quick_grenade_until = 0;
			CMissile::ResetQuickThrow();	// never fired -- don't leave the arming for the next draw
		}
	}

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
	// net_Spawn calls us BEFORE m_pActorEffector is created, so the camera manager can be NULL
	// here; Cameras() only VERIFYs it (compiled out in Release) and would deref null. Nothing to
	// remove in that case anyway -- the effectors die with the old camera manager.
	if (s_nv_black_added && m_pActorEffector)	RemoveEffector(this, effActionAnimPPE);
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

//////////////////////////////////////////////////////////////////////////
// GS controller suicide (wpnpatch ControllerMonster.pas)
//
// The controller's psi grab (CControllerPsyHit) hands over to this: the actor raises his own weapon
// (anm_suicide), and when that animation ends he pulls the trigger (anm_shoot_suicide) and dies
// `suicide_delay` seconds later. Breaking the grab before the shot lowers the weapon (anm_stop_suicide)
// and the actor lives -- GS's IsSuicideInreversible() is our eSuicideShot.
//
// Ported: the firearm branch, which is the one every weapon config here is set up for
// (`suicide_by_animation`, `prohibit_suicide`, `anm_suicide` / `anm_shoot_suicide` / `anm_stop_suicide`,
// `snd_suicide`, `suicide_delay`). NOT ported: the knife self-kill, the grenade suicide throw and the
// GL/RPG branches -- those need their own animations and none of our configs carry them.
//////////////////////////////////////////////////////////////////////////
bool CActor::StartControllerSuicide()
{
	// GS re-runs PsiEffects on every psi pulse, so the branch follows the situation: step closer to
	// the controller with an RPG and it gets dropped, a GL gets switched off, and so on. Only a scene
	// that is already PLAYING is left alone.
	// GS re-runs PsiEffects on every pulse. eSuicideNoAnim is re-entered too (nothing is playing, the
	// hands are just travelling), which is what lets an RPG or a launcher be dropped when the victim
	// walks INTO controller_shoot_expl_min_dist mid-scene. Only a running ANIMATION is left alone.
	if (m_eSuicideState != eSuicideNone && m_eSuicideState != eSuicidePlanning &&
		m_eSuicideState != eSuicideNoAnim && m_eSuicideState != eSuicideAnim)	return true;
	if (!g_Alive())							return false;

	// GS PsiEffects picks the branch by what is in hand. Returning TRUE means "the grab took over" --
	// the vanilla psi attack is then skipped, exactly like GS's PsiStart result.
	CInventoryItem*   it = inventory().ActiveItem();
	CWeaponKnife*     kn = smart_cast<CWeaponKnife*>(it);
	CWeaponMagazined* wm = smart_cast<CWeaponMagazined*>(it);

	{
		CWeapon* dbg_w = smart_cast<CWeapon*>(it);
		CDBG("~ctrl PICK: sect=%s state=%d dist=%.1f can=%d byanim=%d hudoff=%d glmode=%d ammo=%d rpg=%d rg6=%d",
			 dbg_w ? dbg_w->cNameSect().c_str() : (it ? "<non-weapon>" : "<none>"),
			 (int)m_eSuicideState, m_fCtrlDist,
			 wm ? (wm->CanSuicide() ? 1 : 0) : -1,
			 wm ? (wm->SuicideByAnimation() ? 1 : 0) : -1,
			 wm ? (wm->HasSuicideHudOffset() ? 1 : 0) : -1,
			 dbg_w ? (dbg_w->IsGrenadeMode() ? 1 : 0) : -1,
			 wm ? wm->GetAmmoElapsed() : -1,
			 smart_cast<CWeaponRPG7*>(it) ? 1 : 0, smart_cast<CWeaponRG6*>(it) ? 1 : 0);
	}

	// PDA in hand: put it away and go for the KNIFE (GS parks the scene here; we want the scene, so
	// this takes the same road as an unusable weapon -- MINUS the drop: throwing the
	// pda_show_animator phantom on the ground asserts in get_rank as soon as an NPC looks at it).
	if (wm && wm->UsesPdaCursorAnims())
	{
		if (!inventory().ItemFromSlot(KNIFE_SLOT))	return false;
		m_eSuicideState		= eSuicidePlanning;
		m_dwSuicideNextTm	= 0;
		m_bSuicideBroken	= false;
		m_bControllerSees	= true;
		m_bSuicideDropped	= true;				// nothing to drop here, ever
		return true;
	}

	// GRENADE in hand: GS makes the victim pull the pin and let go at minimal force -- it lands at his
	// own feet (PrepareGrenadeForSuicideThrow + SetImmediateThrowStatus). Only when the controller is
	// farther than controller_g_attack_min_dist, so it does not blow the controller up as well.
	{
		CGrenade* gr = smart_cast<CGrenade*>(it);
		if (gr && !READ_IF_EXISTS(pSettings, r_bool, gr->HudSection().c_str(), "prohibit_suicide", FALSE))
		{
			const float mind = READ_IF_EXISTS(pSettings, r_float, gr->HudSection().c_str(),
											  "controller_g_attack_min_dist", 10.f);
			if (m_fCtrlDist > mind)
			{
				gr->Action(kWPN_FIRE, CMD_START);		// pin out
				gr->Action(kWPN_FIRE, CMD_STOP);		// released instantly -> minimum force
				return true;							// the explosion finishes the job
			}
		}
	}

	// GRENADE-LAUNCHER mode: GS turns the launcher off first (controller_can_switch_gl), and drops the
	// weapon when it cannot. RPG/RG6 too close to the controller are dropped as well
	// (controller_shoot_expl_min_dist) -- an explosive shot at that range is the controller's problem.
	if (wm && smart_cast<CWeapon*>(it) && smart_cast<CWeapon*>(it)->IsGrenadeMode())
	{
		LPCSTR hs = wm->HudSection().c_str();
		CDBG("~ctrl BRANCH: GL mode, hud=%s", hs);
		const bool can_shoot_gl  = !!READ_IF_EXISTS(pSettings, r_bool, hs, "controller_can_shoot_gl", FALSE);
		const bool can_switch_gl = !!READ_IF_EXISTS(pSettings, r_bool, hs, "controller_can_switch_gl", FALSE);
		const float gl_min_dist  =   READ_IF_EXISTS(pSettings, r_float, hs, "controller_shoot_gl_min_dist", 10.f);
		// GS GetAmmoInGLCount: in grenade mode iAmmoElapsed IS the launcher's grenades (PerformSwitchGL
		// swaps the magazines), so this is the loaded-grenade test, not the rifle's rounds.
		if (can_shoot_gl && wm->GetAmmoElapsed() > 0 && m_fCtrlDist > gl_min_dist)
		{
			// far enough to eat the grenade himself -- fall through to the normal firearm scene
		}
		else if (can_switch_gl && wm->SuicideRifleAmmo() > 0 && !wm->IsMisfire())
		{
			wm->SwitchState(CWeapon::eSwitch);		// put the launcher away, then the rifle scene
			m_eSuicideState		= eSuicidePlanning;
			m_dwSuicideNextTm	= Device.dwTimeGlobal + 500;
			m_bSuicideBroken	= false;
			m_bControllerSees	= true;
			return true;
		}
		else
		{
			// GS PerformDrop: neither shooting nor switching is allowed -> the weapon goes on the ground
			CDBG("~ctrl BRANCH: GL unusable -> drop + knife");
			return SuicideDropAndTakeKnife();
		}
	}

	if (wm && (smart_cast<CWeaponRPG7*>(it) || smart_cast<CWeaponRG6*>(it)) &&
		m_fCtrlDist < READ_IF_EXISTS(pSettings, r_float, wm->HudSection().c_str(),
									  "controller_shoot_expl_min_dist", 10.f))
	{
		CDBG("~ctrl BRANCH: explosive too close (dist=%.1f) -> drop + knife", m_fCtrlDist);
		return SuicideDropAndTakeKnife();
	}

	// knife in hand: GS only raises the flags -- the knife's own attack selector then plays
	// anm_prepare_suicide / anm_selfkill instead of a stab, and the psi pulse re-triggers the
	// attack (PsiEffects: SwitchState(eFire) whenever no suicide animation is running).
	if (kn)
	{
		CDBG("~ctrl START knife: has_prepare=%d", kn->HasSuicideAnim("anm_prepare_suicide") ? 1 : 0);
		if (!kn->HasSuicideAnim("anm_prepare_suicide"))	return false;
		m_eSuicideState		= eSuicideKnifePrep;
		m_dwSuicideNextTm	= 0;
		m_bSuicideBroken	= false;
		m_bControllerSees	= true;
		return true;
	}

	// firearm that can fire right now but has NO suicide animation (RPG-7, RG-6, and anything with
	// `suicide_by_animation` off): GS's else-branch in PsiEffects -- no motion at all, the weapon is
	// walked to the head by the `hud_move_suicide_offset` HUD offset (WeaponInertion.AddSuicideOffset)
	// and the shot goes off when the hands have converged on that pose.
	if (wm && wm->CanSuicide() && !wm->SuicideByAnimation() && wm->HasSuicideHudOffset())
	{
		CDBG("~ctrl BRANCH: no-anim (hud offset)");
		if (m_eSuicideState != eSuicideNoAnim)		// already travelling -> do not restart the timer
		{
			wm->FireEnd();							// GS controller_queue_stop_prob cuts a held burst
			m_eSuicideState		= eSuicideNoAnim;
			m_bSuicideNoAnimPose= true;
			// floor on the travel: the hands start from wherever the last frame left them, so a pose
			// that happens to be close to the target must still not fire on the frame the scene opens
			m_dwSuicideNextTm	= Device.dwTimeGlobal + 250;
			m_bSuicideBroken	= false;
			m_bControllerSees	= true;
		}
		return true;
	}

	// firearm that can fire right now: the gesture + the shot
	if (wm && wm->CanSuicide())
	{
		if (m_eSuicideState == eSuicideAnim)	return true;	// already playing -- never restart it
		const u32 lock	= wm->SuicideStart();		// GS times it from `lock_time_<alias>`
		if (!lock)
		{
			// busy this instant (firing / another gesture): GS does not fall back to the stock
			// attack, it holds the victim and retries -- PsiEffects even cuts the burst first.
			wm->FireEnd();
			m_eSuicideState		= eSuicidePlanning;
			m_dwSuicideNextTm	= Device.dwTimeGlobal + 200;
			m_bSuicideBroken	= false;
			m_bControllerSees	= true;
			return true;
		}
		m_eSuicideState		= eSuicideAnim;
		m_dwSuicideNextTm	= Device.dwTimeGlobal + lock;
		m_bSuicideBroken	= false;
		m_bControllerSees	= true;
		return true;
	}

	// Empty / jammed / unusable weapon, or empty hands: GS does NOT fall back to the vanilla attack --
	// it drops the useless weapon and makes the victim draw his KNIFE (Update: PerformDrop +
	// ActivateActorSlot(KNIFE_SLOT)). Only a victim with no knife at all gets the stock psi hit.
	if (!inventory().ItemFromSlot(KNIFE_SLOT))	return false;
	m_eSuicideState		= eSuicidePlanning;
	m_dwSuicideNextTm	= 0;
	m_bSuicideBroken	= false;
	m_bControllerSees	= true;
	return true;
}

// GS PsiEffects does the drop RIGHT IN THE BRANCH (`PerformDrop(act); exit;`) -- it never leaves the
// weapon in hand for a later state to deal with. Deferring it to eSuicidePlanning meant a weapon that
// still passed CanSuicide (a loaded launcher does, and so does a rifle in GL mode) took the "it was
// only busy, retry the gesture" road instead and was never thrown away.
bool CActor::SuicideDropAndTakeKnife()
{
	if (!inventory().ItemFromSlot(KNIFE_SLOT))			return false;

	CWeaponMagazined* wm = smart_cast<CWeaponMagazined*>(inventory().ActiveItem());
	// `ammo_class` is NOT the test for "a real firearm": gwr_base_usable, the parent of every gesture
	// phantom, carries it too -- which is how the quick-kick animator ended up on the ground.
	const bool real_gun	 = wm && !IsGesturePhantom(inventory().ActiveItem());

	// A weapon in the middle of its own gesture -- a bayonet stab, a torch toggle -- must be left alone
	// until it finishes: dropping it destroys the hud item whose animation is running, which crashes
	// with no log. Hold the scene in planning; the pulse comes back every 300 ms.
	if (real_gun && wm->IsPending())
	{
		m_eSuicideState		= eSuicidePlanning;
		m_dwSuicideNextTm	= Device.dwTimeGlobal + 300;
		m_bSuicideBroken	= false;
		m_bControllerSees	= true;
		return true;
	}

	if (real_gun && !m_bSuicideDropped)
	{
		m_bSuicideDropped = true;
		PerformDropForced();
	}
	inventory().Activate(KNIFE_SLOT);

	m_eSuicideState		= eSuicidePlanning;		// the knife takes over as soon as it is out
	m_dwSuicideNextTm	= 0;
	m_bSuicideBroken	= false;
	m_bControllerSees	= true;
	return true;
}

// GS DoSuicideShot, called from the hud_move update once the hands have reached the suicide pose
// (WeaponInertion.pas: the remaining distance is below the jitter amplitude). Same tail as the
// animated branch -- one round, then death after `suicide_delay`.
void CActor::SuicideHudOffsetArrived()
{
	if (m_eSuicideState != eSuicideNoAnim)		return;
	if (Device.dwTimeGlobal < m_dwSuicideNextTm)	return;
	if (!m_bControllerSees)						return;		// GS gates the shot itself on visibility
	CWeaponMagazined* wm = smart_cast<CWeaponMagazined*>(inventory().ActiveItem());
	if (!wm)	{ m_eSuicideState = eSuicideNone; return; }

	CDBG("~ctrl SHOT (no-anim): sect=%s state=%d", wm->cNameSect().c_str(), (int)wm->GetState());
	// same order as the animated branch: the scene must be irreversible before the trigger, or its own
	// shot is refused by the block that keeps the VICTIM from firing
	const float delay	= READ_IF_EXISTS(pSettings, r_float, wm->HudSection().c_str(), "suicide_delay", 0.1f);
	m_eSuicideState		= eSuicideShot;
	m_dwSuicideNextTm	= Device.dwTimeGlobal + u32(delay * 1000.f);
	wm->SuicideShoot	();
}

// GS OnSuicideAnimEnd takes the decision when the gesture ENDS; the animation itself is never cut.
void CActor::StopControllerSuicide()
{
	if (m_eSuicideState == eSuicideShot || m_eSuicideState == eSuicideKnifeKill)	return;	// no way back
	m_bSuicideBroken	= true;
}

// GS CheckActorVisibilityForController: on veteran+ the visibility requirement is DROPPED unless the
// controller's own section asks for it (`mandatory_suicide_visibility_check`), so hiding behind a tree
// does not save you on the higher difficulties -- that is GS behaviour, not a bug.
void CActor::NotifyControllerSees(bool sees, bool mandatory_check)
{
	if (!mandatory_check && g_SingleGameDifficulty >= egdVeteran)	sees = true;
	m_bControllerSees	= sees;
}

void CActor::UpdateControllerSuicide()
{
	// the knife's cut has landed (requested from its animation callback, performed here where
	// destroying the actor cannot pull the rug from under the hud item)
	if (m_bSuicideKillPending)
	{
		m_bSuicideKillPending	= false;
		m_eSuicideState			= eSuicideNone;
		m_dwSuicideNextTm		= 0;
		m_dwControlledUntil		= 0;
		if (g_Alive())
		{
			Fvector dir; dir.set(0.f, -1.f, 0.f);
			SHit HDS = SHit(1000.f, 1000.f, dir, this, u16(0), Fvector().set(0.f, 0.f, 0.f),
							0.f, ALife::eHitTypeWound, 0.f, false);
			Hit(&HDS);
		}
		return;
	}
	// the head-aim pose belongs to the no-animation scene and to the frames right after its shot; any
	// other state (aborted, re-picked into planning, over) drops it
	if (m_eSuicideState != eSuicideNoAnim && m_eSuicideState != eSuicideShot)
		m_bSuicideNoAnimPose = false;

	// GS Update:378 -- the moment the control timer expires with no shot fired, the victim is let go
	// and his hands shake for `actor_shock_time`. We only did this on one knife path, so simply
	// surviving a grab left the hands perfectly steady.
	if (m_bWasControlled && !IsActorControlled())
	{
		m_bWasControlled = false;
		if (!IsSuicideIrreversible())
		{
			SetHandsJitterTime(u32(1000.f * READ_IF_EXISTS(pSettings, r_float, "gunslinger_base",
															"actor_shock_time", 10.f)));
			// GS ResetActorControl on the same edge: the hold is over, so the scene is dropped. It
			// CLEARS FLAGS AND PLAYS NOTHING -- a running gesture is never cut. GS's decision always
			// belongs to the end of the animation (OnSuicideAnimEnd:537), which then finds the flags
			// down and plays anm_stop_suicide by itself. Calling SuicideAbort here instead cut the
			// animation dead, and with controller_time 5 s against a 7.4 s gesture that was every time.
			// Anything with a MOTION on screen is left alone -- the knife scene included. GS's chain
			// (Update:372 vs :381) skips the reset entirely once `_death_action_started`, and its knife
			// selector is what plays anm_stop_suicide when the grab turns out to be gone. Clearing the
			// state from here instead pulled the selector out from under a running animation, which is
			// why the knife suddenly ended early.
			if (m_eSuicideState == eSuicideAnim ||
				m_eSuicideState == eSuicideKnifePrep || m_eSuicideState == eSuicideKnifeKill)
				m_bSuicideBroken	= true;		// let it play out; the end decides
			else if (m_eSuicideState != eSuicideNone)
			{
				m_eSuicideState		= eSuicideNone;
				m_dwSuicideNextTm	= 0;
				m_bSuicideNoAnimPose= false;
			}
		}
	}
	else if (IsActorControlled())
		m_bWasControlled = true;

	if (m_eSuicideState == eSuicideNone)					return;

	// GS keeps the victim rooted and takes his finger off the trigger / out of the sight while it
	// holds him (Update: movement actions forced off, kfUNZOOM, SetWorkingState(false)).
	mstate_wishful &= ~(mcSprint | mcAccel | mcJump);
	// GS Update:389 -- after the shot ALL movement is forced off, not just the sprint/jump suppressed
	// during the grab: the victim stands where he is for the moment it takes him to fall.
	if (IsSuicideIrreversible())
		mstate_wishful &= ~(mcFwd | mcBack | mcLStrafe | mcRStrafe);

	// GS Update: a detector in the left hand is forced away for the duration of the grab
	{
		CCustomDetector* det = smart_cast<CCustomDetector*>(inventory().ItemFromSlot(DETECTOR_SLOT));
		if (det && det->IsWorking())	det->HideDetector(true);
	}

	CInventoryItem*   it = inventory().ActiveItem();
	CWeaponKnife*     kn = smart_cast<CWeaponKnife*>(it);
	CWeaponMagazined* wm = smart_cast<CWeaponMagazined*>(it);
	if (wm && wm->IsZoomed())							wm->OnZoomOut();

	// GS Update:411 -- a victim who was holding the trigger when the grab landed is allowed to keep
	// firing, but the burst is cut once he is down to his last few rounds: the scene needs a loaded
	// weapon, and an emptied magazine turns the whole thing into the drop-and-take-the-knife path.
	if (wm && IsActorControlled() && !IsSuicideIrreversible() &&
		wm->GetState() == CWeapon::eFire && wm->GetAmmoElapsed() <= 3)
		wm->FireEnd();

	// ---- planning: get a knife into the hands (GS Update, the "cannot use this item" branch)
	if (m_eSuicideState == eSuicidePlanning)
	{
		if (m_bSuicideBroken)	{ m_eSuicideState = eSuicideNone; return; }
		// GS: the branch is re-picked every pulse (distance to the controller may have changed)
		if (IsActorControlled() && Device.dwTimeGlobal >= m_dwSuicideNextTm)	StartControllerSuicide();
		if (m_eSuicideState != eSuicidePlanning)	return;
		if (kn)					// the knife is out -> its selector takes over from here
		{
			if (kn->HasSuicideAnim("anm_prepare_suicide"))
				m_eSuicideState = eSuicideKnifePrep;
			return;
		}
		if (Device.dwTimeGlobal < m_dwSuicideNextTm)	return;
		m_dwSuicideNextTm = Device.dwTimeGlobal + 300;
		// "it was only busy" -- retry the GESTURE. Only ever true for a weapon that HAS one: an RPG
		// parked here because it is too close to the controller also passes CanSuicide(), and it used
		// to land in this branch, fail SuicideStart (no anm_suicide) and return -- so the drop below
		// was never reached and the launcher stayed in hand however close the victim walked. GS drops
		// it inside PsiEffects itself and never gets near this path.
		if (wm && wm->CanSuicide() && wm->SuicideByAnimation())
		{
			wm->FireEnd();
			const u32 lock = wm->SuicideStart();
			if (lock)
			{
				m_eSuicideState		= eSuicideAnim;
				m_dwSuicideNextTm	= Device.dwTimeGlobal + lock;
			}
			return;
		}
		// Only a REAL firearm is thrown away. The hud-only phantoms our item-use animations put in the
		// hands (medkit_hud_model, pda_show_animator, the kick animator) are not inventory weapons --
		// dropping one asserts in get_rank ("cannot find rank for medkit_hud_model"). NOTE the test is
		// the fake visual, NOT `ammo_class`: gwr_base_usable, which every phantom inherits, has one.
		const bool real_gun = wm && !IsGesturePhantom(inventory().ActiveItem());
		if (real_gun && wm->IsPending())	return;			// mid-gesture (bayonet stab): retry next pulse
		if (real_gun && !m_bSuicideDropped)					// GS PerformDrop
		{
			m_bSuicideDropped = true;
			PerformDropForced();		// g_PerformDrop refuses while g_block_wpn_switch is up -- and
										// the scene raises that flag itself, so the weapon was only
										// holstered by the knife activation below instead of thrown
		}
		inventory().Activate(KNIFE_SLOT);
		return;
	}

	// GS's whole design rests on `lock_time_<alias>` being SHORTER than the motion: the shot cuts the
	// gesture's dead tail while it is still on screen. Our lock values are copied from GS but our omf
	// motions are not always as long, and once the motion ends the weapon has nothing to play -- the
	// engine falls back to the idle, which takes the pose and swallows the queued shot. Whether that
	// lands in the same frame as the lock is pure timing, which is why the shot was there one run and
	// gone the next. Never let the timer outlive the motion it is supposed to cut.
	if (m_eSuicideState == eSuicideAnim && wm)
	{
		// Clamping to the motion end EXACTLY was not enough: both deadlines then landed on the same
		// tick, and which of the two updates ran first that frame decided the outcome. CHudItem ends
		// the motion on `curr > end` and calls OnAnimationEnd; we fire on `now >= next`. Weapon first
		// -> the gesture is already gone and the queued shot is swallowed; actor first -> the shot
		// goes off. Nothing in the engine orders those two, which is exactly why the same scene fired
		// on one run and not the next. Pull the shot a hair in front of the motion instead: GS wants
		// the round to leave WHILE the gesture is still on screen anyway (that is what `lock_time`
		// cutting the dead tail means), so an early shot is the correct behaviour, not a workaround.
		const u32 mend = wm->MotionEndTm();
		if (mend)
		{
			const u32 guard	= 150;		// ms of clearance; ~9 frames at 60 fps
			const u32 shoot	= (mend > Device.dwTimeGlobal + guard) ? (mend - guard) : Device.dwTimeGlobal;
			if (shoot < m_dwSuicideNextTm)	m_dwSuicideNextTm = shoot;
		}

		// GS runs the WHOLE of PsiEffects on every pulse, a playing gesture included -- only the KNIFE
		// branch checks "is a suicide animation already running". So walking into
		// controller_shoot_expl_min_dist with a launcher mounted must throw it away mid-gesture, the
		// same as it does before the gesture starts. We left the animated scene alone entirely, so the
		// distance stopped mattering the moment the animation began.
		if (IsActorControlled() && !m_bSuicideBroken && Device.dwTimeGlobal >= m_dwSuicideRepickTm)
		{
			m_dwSuicideRepickTm = Device.dwTimeGlobal + 300;
			StartControllerSuicide();
			if (m_eSuicideState != eSuicideAnim)	return;		// re-picked into a drop / another branch
		}
	}

	if (Device.dwTimeGlobal < m_dwSuicideNextTm)			return;

	// ---- knife: prepare -> cut (the kill happens when anm_selfkill ends, in CWeaponKnife)
	if (m_eSuicideState == eSuicideKnifePrep || m_eSuicideState == eSuicideKnifeKill)
	{
		if (!kn)	{ m_eSuicideState = eSuicideNone; return; }
		// GS PsiEffects: pulse an attack whenever no suicide animation is running -- the knife's
		// selector (CActor::KnifeSuicideAnim) decides which one that attack becomes.
		if (kn->GetState() == CHUDState::eIdle && !kn->IsPendingPublic())
		{
			CDBG("~ctrl pulse: state=%d", (int)m_eSuicideState);
			kn->SwitchState(CWeapon::eFire);
		}
		return;
	}

	if (!wm)	{ m_eSuicideState = eSuicideNone; m_dwSuicideNextTm = 0; return; }

	// ---- no suicide animation (RPG-7 / RG-6 / GL): the HUD offset does the aiming, player_hud fires
	// the shot when it arrives. There is nothing to abort here -- with no motion running the offset
	// simply relaxes back to the normal pose, which is GS's behaviour too.
	if (m_eSuicideState == eSuicideNoAnim)
	{
		// GS Update (ControllerMonster.pas:583) drops the scene on a lost line of sight ONLY while no
		// suicide is running yet (`and not IsActorSuicideNow()`). Cancelling a running one on a single
		// unseen frame restarted the whole thing: the pose target snapped back to normal and the hands
		// flew there at the ordinary hud_move speed, which is the jerk right after the grab lands.
		// A BROKEN grab (dead controller) does not end it here either: GS keeps `_suicide_now` up until
		// the control timer runs out, so the aim offset is dropped -- SuicideHudAimActive -- while the
		// slow suicide pace stays, and the weapon LOWERS smoothly instead of snapping back.

		// GS runs the WHOLE of PsiEffects on every pulse, so the branch is re-chosen from the CURRENT
		// distance for as long as the scene lasts. Nothing re-entered it here, so a launcher that was
		// legal at 18 m stayed legal after walking right up to the controller -- and the shot then
		// killed them both. Now closing inside controller_shoot_expl_min_dist swaps it for the knife
		// mid-scene, exactly as stepping back out of range starts the scene in the first place.
		if (IsActorControlled() && !m_bSuicideBroken && Device.dwTimeGlobal >= m_dwSuicideRepickTm)
		{
			m_dwSuicideRepickTm = Device.dwTimeGlobal + 300;
			StartControllerSuicide();
		}
		return;
	}

	if (m_eSuicideState == eSuicideAnim)
	{
		// Firing straight out of the gesture is fine -- CWeaponMagazined::FireStart accepts eActionAnim
		// and switches to eFire itself; only the PENDING lock had to go, which SuicideShoot drops.
		// Routing through idle first (what this used to do) returned the weapon to the hip for a frame
		// and the round left forwards instead of into his own head.

		// GS OnSuicideAnimEnd: shoot only if the grab still holds AND a controller still sees us;
		// otherwise lower the weapon (anm_stop_suicide) and live.
		if (m_bSuicideBroken || !m_bControllerSees)
		{
			// Release the pose BEFORE playing the abort gesture, not after. SuicideAbort reaches
			// anm_stop_suicide through PlayHudActionAnim, which demands GetState()==eIdle and gets
			// there with a SwitchState(eIdle) -- the very transition the pose hold blocks. Aborting
			// with the hold still up made PlayHudActionAnim return false, so the lowering animation
			// never played on ANY weapon and the gesture just snapped off. The scene is over at this
			// point, so there is nothing left for the hold to protect.
			m_eSuicideState		= eSuicideNone;
			m_dwSuicideNextTm	= 0;
			wm->SuicideAbort();
			return;
		}

		// irreversible FIRST, then fire: the trigger block above (CWeapon::SuicideBlocksFire) lets a
		// shot through only once the scene is irreversible -- GS's DoSuicideShot sets its flags before
		// it pulls the trigger for exactly this reason.
		const float delay	= READ_IF_EXISTS(pSettings, r_float, wm->HudSection().c_str(), "suicide_delay", 0.1f);
		m_eSuicideState		= eSuicideShot;
		m_dwSuicideNextTm	= Device.dwTimeGlobal + u32(delay * 1000.f);
		wm->SuicideShoot	();
		return;
	}

	// eSuicideShot: GS KillActor(act, act) `suicide_delay` after the shot
	wm->SuicideStopFire	();
	m_eSuicideState		= eSuicideNone;
	m_dwSuicideNextTm	= 0;
	if (g_Alive())
	{
		Fvector dir; dir.set(0.f, -1.f, 0.f);
		SHit HDS			= SHit(1000.f, 1000.f, dir, this, u16(0), Fvector().set(0.f, 0.f, 0.f),
								   0.f, ALife::eHitTypeWound, 0.f, false);
		Hit					(&HDS);
	}
}

//////////////////////////////////////////////////////////////////////////
// GS controller GRAB: everything the victim suffers while a controller holds him, ported from
// ControllerMonster.pas (PsiEffects / Update / GetCurrentControllerInputCorrectionParams /
// GetControllerInputRandomOffset / GetCurrentSuicideWalkKoef). All params live in the
// [gunslinger_base] section of gunslinger_params.ltx, GS's own names and values.
//////////////////////////////////////////////////////////////////////////
static float gwr_ctrl_f(LPCSTR key, float def)
{
	return READ_IF_EXISTS(pSettings, r_float, "gunslinger_base", key, def);
}

bool CActor::IsActorControlled() const
{
	return Device.dwTimeGlobal < m_dwControlledUntil;
}

void CActor::SetHandsJitterTime(u32 ms)
{
	m_dwJitterUntil = Device.dwTimeGlobal + ms;
}

// GS PsiEffects:591 -- `_controlled_time_remains := GetControllerTime()` on every pulse. The weapon's
// hud section may set its own controller_time (GS reads it there first).
void CActor::RefreshControlTime()
{
	float t = gwr_ctrl_f("controller_time", 3.f);
	CHudItem* hi = smart_cast<CHudItem*>(inventory().ActiveItem());
	if (hi)	t = READ_IF_EXISTS(pSettings, r_float, hi->HudSection().c_str(), "controller_time", t);
	m_dwControlledUntil = Device.dwTimeGlobal + u32(t * 1000.f);
}

// GS IsPsiBlocked (ControllerMonster.pas:119) asks the actor's conditions for an active telepathic-
// protection booster. CS has no such booster, so the equivalent here is the one thing that already
// dulls the mind: being drunk. Vodka raises the actor's alcohol level (eat_alcohol) and it decays on
// its own, so "the vodka is still working" is exactly `alcohol > 0`.
bool CActor::IsPsiBlocked() const
{
	// Two sources, both of which stop a controller from taking hold: the vodka still in him, and the
	// psi blockade. Only the blockade also cuts telepathic DAMAGE (CActorCondition::ConditionHit) --
	// vodka does nothing but keep the controller out.
	if (PsiBlockadeActive())	return true;
	return const_cast<CActor*>(this)->conditions().GetAlcohol() > 0.f;
}

// GS UpdatePsiBlockFailedState (:93): the protection is not absolute -- it FAILS with a probability
// that grows as the controller gets closer (controller_psi_unblock_* in [gunslinger_base]). Rolled
// once per grab, at the psi attack's activate, exactly like GS.
void CActor::RollPsiBlock(float dist)
{
	const float dmin  = gwr_ctrl_f("controller_psi_unblock_mindist", 5.f);
	const float dmax  = gwr_ctrl_f("controller_psi_unblock_maxdist", 19.f);
	const float pmin  = gwr_ctrl_f("controller_psi_unblock_mindist_prob", 0.75f);
	const float pmax  = gwr_ctrl_f("controller_psi_unblock_maxdist_prob", 0.05f);

	float prob;
	if (dist <= dmin)		prob = pmin;
	else if (dist >= dmax)	prob = pmax;
	else
	{
		prob = 1.f - (dist - dmin) / (dmax - dmin);
		prob = prob * (pmin - pmax) + pmax;
	}
	m_bPsiBlockFailed = (::Random.randF(0.f, 1.f) < prob);
}

// GS's own shorthand: `IsPsiBlocked(act) and not IsPsiBlockFailed()`
bool CActor::ControllerPsiBlocked() const
{
	return IsPsiBlocked() && !m_bPsiBlockFailed;
}

// GS OnPsyHitActivate (ControllerMonster.pas:863) -- the psi attack's WINDUP, before the grab lands.
// IsControllerPreparing() stays true for `controller_prepare_time + 1s` from here, and most of the
// blocks the grab applies (aim, sprint, jump, quick keys, mouse nudge, walk speed) already hold.
void CActor::StartControllerPrepare(float dist)
{
	if (!IsActorControlled())	RollPsiBlock(dist);		// GS rolls it once per grab
	m_dwCtrlPrepareStart = Device.dwTimeGlobal;
}

bool CActor::IsControllerPreparing() const
{
	if (!m_dwCtrlPrepareStart)		return false;
	if (ControllerPsiBlocked())		return false;	// GS :888 -- a protected victim feels no windup
	const u32 win = u32(1000.f * gwr_ctrl_f("controller_prepare_time", 3.f)) + 1000;
	return (Device.dwTimeGlobal - m_dwCtrlPrepareStart) < win;
}

// GS GetHandJitterScale (ActorUtils.pas:3656)
float CActor::HandsJitterScale(float stop_time_ms) const
{
	if ((IsActorControlled() || IsSuicideInProgress()) && !IsSuicideIrreversible())	return 1.f;
	if (Device.dwTimeGlobal >= m_dwJitterUntil)										return 0.f;
	const float rem = float(m_dwJitterUntil - Device.dwTimeGlobal);
	if (stop_time_ms <= 0.f || rem > stop_time_ms)									return 1.f;
	return rem / stop_time_ms;
}

// GS ChangeInputRotateAngle + the controlled-time reset: called by the psy hit when the grab lands.
void CActor::StartControllerGrab(float dist_to_controller)
{
	// GS rolls the distortion once per grab, not per frame
	if (!IsActorControlled())
	{
		const float MIN_ANGLE = deg2rad(70.f), MAX_ANGLE = deg2rad(290.f);
		const float smin = gwr_ctrl_f("controller_mouse_sense_min", 0.1f);
		const float smax = gwr_ctrl_f("controller_mouse_sense_max", 0.5f);
		m_fCtrlRotAngle	= ::Random.randF(MIN_ANGLE, MAX_ANGLE);
		m_fCtrlSenseX	= ::Random.randF(smin, smax);
		m_fCtrlSenseY	= ::Random.randF(smin, smax);
		m_bCtrlInvertY	= ::Random.randF() < 0.5f;
		m_bSuicideDropped = false;
	}

	RefreshControlTime();

	// GS controller_queue_stop_prob: a burst the victim was firing is usually cut
	CWeaponMagazined* wm = smart_cast<CWeaponMagazined*>(inventory().ActiveItem());
	if (wm && wm->GetState() == CWeapon::eFire &&
		::Random.randF() < gwr_ctrl_f("controller_queue_stop_prob", 0.95f))
		wm->FireEnd();

	m_fCtrlDist = dist_to_controller;	// GS gates the grenade/RPG branches on it
}

// GS GetCurrentSuicideWalkKoef
float CActor::ControlledSpeedKoef() const
{
	if (!IsActorControlled() && m_eSuicideState == eSuicideNone)	return 1.f;
	return gwr_ctrl_f("controlled_actor_speed_koef", 1.f);
}

// GS GetCurrentControllerInputCorrectionParams + GetControllerInputRandomOffset: while the victim is
// controlled the mouse is rotated by a fixed random angle, scaled down per axis, sometimes inverted,
// and every frame gets a random nudge -- you fight your own aim.
void CActor::UpdatePlannedMonsterKick()
{
	if (!s_kick_planned)	return;

	if (Device.dwTimeGlobal > s_kick_expire_at)	{ s_kick_planned = false; return; }

	// hands must be free: the phantom attaches where a detector would sit, and a weapon still on
	// screen means the drop has not gone through yet
	if (inventory().ActiveItem())					return;
	if (g_player_hud && g_player_hud->attached_item(1))	return;

	s_kick_planned		= false;
	gwr_call_action_animator("gwr_eatable.on_monster_kick", s_kick_from_back, true);
}

void CActor::ApplyControlledMouse(int& dx, int& dy)
{
	if (!IsActorControlled() && m_eSuicideState == eSuicideNone)	return;

	const float c = _cos(m_fCtrlRotAngle), s = _sin(m_fCtrlRotAngle);
	const float x = float(dx), y = float(dy);
	float nx = (x * c - y * s) * m_fCtrlSenseX;
	float ny = (x * s + y * c) * m_fCtrlSenseY;
	if (m_bCtrlInvertY)	ny = -ny;

	const int omin = (int)gwr_ctrl_f("controller_mouse_offset_min", -5.f);
	const int omax = (int)gwr_ctrl_f("controller_mouse_offset_max",  5.f);
	if (omax > omin)
	{
		nx += float(::Random.randI(omin, omax + 1));
		ny += float(::Random.randI(omin, omax + 1));
	}
	dx = iFloor(nx);
	dy = iFloor(ny);
}

// GS knife selector (WeaponEvents): planning + seen -> blade to the throat, then the cut;
// grab broken while a suicide animation was running -> lower it again. NULL = normal attack.
LPCSTR CActor::KnifeSuicideAnim()
{
	CDBG("~ctrl selector: state=%d broken=%d sees=%d", (int)m_eSuicideState, m_bSuicideBroken?1:0, m_bControllerSees?1:0);
	if (m_eSuicideState != eSuicideKnifePrep && m_eSuicideState != eSuicideKnifeKill)	return NULL;

	// grab broken / lost sight: GS lowers the blade AND calls ResetActorControl right here, so the
	// scene ends with this one animation instead of looping forever.
	// ...but anm_stop_suicide is the END of a gesture, and GS only hands it out when a suicide
	// animation WAS playing. Losing sight during the knife's own draw, before anm_prepare_suicide has
	// ever run, ended the scene by lowering the blade from a pose it never took -- on screen the draw
	// was followed straight by the stop, with the preparation apparently skipped.
	if (m_bSuicideBroken || !m_bControllerSees)
	{
		const bool played	= m_bSuicidePrepPlayed;
		m_eSuicideState		= eSuicideNone;
		m_bSuicidePrepPlayed= false;
		m_dwControlledUntil	= 0;
		SetHandsJitterTime	(u32(1000.f * READ_IF_EXISTS(pSettings, r_float, "gunslinger_base",
														   "actor_shock_time", 10.f)));
		return played ? "anm_stop_suicide" : NULL;	// nothing started -> nothing to lower
	}

	// GS sets _suicide_now while anm_prepare_suicide plays, so the NEXT attack is the cut.
	if (m_eSuicideState == eSuicideKnifePrep)
	{
		m_eSuicideState			= eSuicideKnifeKill;
		m_bSuicidePrepPlayed	= true;		// from here on there IS something to lower
		return "anm_prepare_suicide";
	}
	return "anm_selfkill";
}
