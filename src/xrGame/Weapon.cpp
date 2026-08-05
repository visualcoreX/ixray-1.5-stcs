#include "stdafx.h"
#include "Weapon.h"
#include "ParticlesObject.h"
#include "HUDManager.h"
#include "entity_alive.h"
#include "inventory_item_impl.h"
#include "inventory.h"
#include "xrserver_objects_alife_items.h"
#include "actor.h"
#include "actoreffector.h"
#include "level.h"
#include "xr_level_controller.h"
#include "game_cl_base.h"
#include "../Include/xrRender/Kinematics.h"
#include "ai_object_location.h"
#include "mathutils.h"
#include "object_broker.h"
#include "player_hud.h"
#include "gamepersistent.h"
#include "effectorFall.h"
#include "debug_renderer.h"
#include "static_cast_checked.hpp"
#include "clsid_game.h"
#include "ui/UIWindow.h"
#include "IXRayGameConstants.h"
#include "../xrEngine/gamemtllib.h"

#define WEAPON_REMOVE_TIME		60000
#define ROTATION_TIME			0.25f

BOOL	b_toggle_weapon_aim		= FALSE;

// debug console override for zoom_hide_crosshair across ALL weapons: -1 = per-weapon config (default),
// 0 = force show crosshair while aiming, 1 = force hide. Runtime-only, NOT saved to user.ltx.
int		g_dbg_zoom_hide_crosshair	= -1;

// GS `npc_lasers` console flag (gunsl_config.pas:1173). 1 = a weapon in an NPC's hands keeps its laser
// BEAM lit (the dot is actor-only anyway), 0 = the laser is switched off the moment somebody else picks
// the weapon up. GS default: on in rspec_default/high/extreme, off in rspec_low/minimum -- so ON here.
// The mounted FLASHLIGHT has no such option in GS: it is always killed for NPCs (see CWeapon::UpdateCL).
int		g_npc_lasers				= 1;

CWeapon::CWeapon()
{
	SetState				(eHidden);
	SetNextState			(eHidden);
	m_sub_state				= eSubstateReloadBegin;
	m_bTriStateReload		= false;
	m_bZoomKeyHeld			= false;
	m_bZoomToggleWanted		= false;
	m_scope_illum_value		= 0.f;
	m_scope_illum_jitter	= 0.f;
	m_scope_illum_step		= 0;
	m_scope_illum_steps		= 0;
	m_scope_illum_min		= 0.f;
	m_scope_illum_max		= 0.f;
	m_lens_step				= 0;
	m_lens_steps			= 0;
	m_lens_min				= 0.f;
	m_lens_max				= 0.f;
	m_bAlterZoom			= false;
	m_bAlterZoomLast		= false;
	m_fAlterZoomFactor		= 0.f;
	for (int i = 0; i < 16; ++i)	m_lens_step_by_scope[i] = -1;
	SetDefaults				();

	m_Offset.identity		();
	m_StrapOffset.identity	();

	m_bLaserInstalled		= false;
	m_gwr_world_bones_sig	= u32(-1);
	m_bLaserEnabled			= false;
	m_bBayonetInstalled		= false;
	m_bBayonetBlockedBySilencer	= true;
	m_bBayonetBlockedByGL	= true;
	m_pLaserDot				= NULL;
	m_dwLaserToggleAt		= 0;
	m_bLaserPendingState	= false;
	m_dwGLSwitchStartTm		= 0;
	m_dwGLSwitchEndTm		= 0;
	m_iLaserParticleIdx		= -1;
	m_vLaserOffset.set		(0.f,0.f,0.f);
	m_vLaserWorldOffset.set	(0.f,0.f,0.f);

	m_bFlashInstalled		= false;
	m_bFlashEnabled			= false;
	m_fFlashFade			= 0.f;
	m_dwFlashToggleAt		= 0;
	m_bFlashPendingState	= false;
	m_vFlashOffset.set		(0.f,0.f,0.f);
	m_vFlashWorldOffset.set	(0.f,0.f,0.f);
	m_vFlashOmniWorldOffset.set(0.f,0.f,0.f);
	m_bFlashHudModeNow		= true;
	m_bFlashGlow			= false;

	iAmmoCurrent			= -1;
	m_dwAmmoCurrentCalcFrame= 0;

	iAmmoElapsed			= -1;
	iMagazineSize			= -1;
	m_ammoType				= 0;
	m_ammoName				= NULL;

	eHandDependence			= hdNone;

	m_zoom_params.m_fCurrentZoomFactor			= g_fov;
	m_zoom_params.m_fZoomRotationFactor			= 0.f;

	m_pAmmo					= NULL;


	m_pFlameParticles2		= NULL;
	m_sFlameParticles2		= NULL;


	m_fCurrentCartirdgeDisp = 1.f;

	m_strap_bone0			= 0;
	m_strap_bone1			= 0;
	m_StrapOffset.identity	();
	m_strapped_mode			= false;
	m_can_be_strapped		= false;
	m_ef_main_weapon_type	= u32(-1);
	m_ef_weapon_type		= u32(-1);
	m_UIScope				= NULL;
	m_set_next_ammoType_on_reload = u32(-1);
	m_crosshair_inertion	= 0.f;

	bReloadKeyPressed = false;
	bAmmotypeKeyPressed = false;
}

CWeapon::~CWeapon		()
{
	xr_delete	(m_UIScope);
}

void CWeapon::Hit					(SHit* pHDS)
{
	inherited::Hit(pHDS);
}



void CWeapon::UpdateXForm	()
{
	if (Device.dwFrame == dwXF_Frame)
		return;

	dwXF_Frame				= Device.dwFrame;

	if (!H_Parent())
		return;

	// Get access to entity and its visual
	CEntityAlive*			E = smart_cast<CEntityAlive*>(H_Parent());
	
	if (!E) {
		if (!IsGameTypeSingle())
			UpdatePosition	(H_Parent()->XFORM());

		return;
	}

	const CInventoryOwner	*parent = smart_cast<const CInventoryOwner*>(E);
	if (parent && parent->use_simplified_visual())
		return;

	if (parent->attached(this))
		return;

	IKinematics*			V = smart_cast<IKinematics*>	(E->Visual());
	VERIFY					(V);

	// Get matrices
	int						boneL = -1, boneR = -1, boneR2 = -1;

	// this ugly case is possible in case of a CustomMonster, not a Stalker, nor an Actor
	E->g_WeaponBones		(boneL,boneR,boneR2);

	if (boneR == -1)		return;

	if ((HandDependence() == hd1Hand) || (GetState() == eReload) || (!E->g_Alive()))
		boneL				= boneR2;

	V->CalculateBones		();
	Fmatrix& mL				= V->LL_GetTransform(u16(boneL));
	Fmatrix& mR				= V->LL_GetTransform(u16(boneR));
	// Calculate
	Fmatrix					mRes;
	Fvector					R,D,N;
	D.sub					(mL.c,mR.c);	

	if(fis_zero(D.magnitude())) {
		mRes.set			(E->XFORM());
		mRes.c.set			(mR.c);
	}
	else {		
		D.normalize			();
		R.crossproduct		(mR.j,D);

		N.crossproduct		(D,R);			
		N.normalize			();

		mRes.set			(R,N,D,mR.c);
		mRes.mulA_43		(E->XFORM());
	}

	UpdatePosition			(mRes);
}

void CWeapon::UpdateFireDependencies_internal()
{
	if (Device.dwFrame!=dwFP_Frame) 
	{
		dwFP_Frame			= Device.dwFrame;

		UpdateXForm			();

		if ( GetHUDmode() )
		{
			HudItemData()->setup_firedeps		(m_current_firedeps);
			VERIFY(_valid(m_current_firedeps.m_FireParticlesXForm));
		} else 
		{
			// 3rd person or no parent
			Fmatrix& parent			= XFORM();
			Fvector& fp				= vLoadedFirePoint;
			Fvector& fp2			= vLoadedFirePoint2;
			Fvector& sp				= vLoadedShellPoint;

			parent.transform_tiny	(m_current_firedeps.vLastFP,fp);
			parent.transform_tiny	(m_current_firedeps.vLastFP2,fp2);
			parent.transform_tiny	(m_current_firedeps.vLastSP,sp);
			
			m_current_firedeps.vLastFD.set	(0.f,0.f,1.f);
			parent.transform_dir	(m_current_firedeps.vLastFD);

			m_current_firedeps.m_FireParticlesXForm.set(parent);
			VERIFY(_valid(m_current_firedeps.m_FireParticlesXForm));
		}
	}
}

void CWeapon::ForceUpdateFireParticles()
{
	if ( !GetHUDmode() )
	{//update particlesXFORM real bullet direction

		if (!H_Parent())		return;

		Fvector					p, d; 
		smart_cast<CEntity*>(H_Parent())->g_fireParams	(this, p,d);

		Fmatrix						_pxf;
		_pxf.k						= d;
		_pxf.i.crossproduct			(Fvector().set(0.0f,1.0f,0.0f),	_pxf.k);
		_pxf.j.crossproduct			(_pxf.k,		_pxf.i);
		_pxf.c						= XFORM().c;
		
		m_current_firedeps.m_FireParticlesXForm.set	(_pxf);
	}
}

void CWeapon::Load		(LPCSTR section)
{
	inherited::Load					(section);
	CShootingObject::Load			(section);

	m_base_inertion = m_current_inertion;

	m_zoom_inertion.PitchOffsetR = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_aim_pitch_offset_r", 0.0f);
	m_zoom_inertion.PitchOffsetD = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_aim_pitch_offset_d", 0.0f);
	m_zoom_inertion.PitchOffsetN = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_aim_pitch_offset_n", 0.0f);

	m_zoom_inertion.OriginOffset = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_aim_origin_offset", ORIGIN_OFFSET * 0.5f);
	m_zoom_inertion.TendtoSpeed = READ_IF_EXISTS(pSettings, r_float, hud_sect, "inertion_aim_tendto_speed", TENDTO_SPEED);
	
	if(pSettings->line_exist(section, "flame_particles_2"))
		m_sFlameParticles2 = pSettings->r_string(section, "flame_particles_2");

	// Does an attached silencer / GL take the bayonet blade away (and disable its stab)? Default TRUE
	// for both -- that is how the ak74 behaves, where the blade and the GP-25 fight for the barrel.
	// The AN-94 mounts its knife clear of the launcher, so Gunslinger keeps it on: bayonet_blocked_by_gl = false.
	m_bBayonetBlockedBySilencer	= !!READ_IF_EXISTS(pSettings, r_bool, section, "bayonet_blocked_by_silencer", TRUE);
	m_bBayonetBlockedByGL		= !!READ_IF_EXISTS(pSettings, r_bool, section, "bayonet_blocked_by_gl", TRUE);
	// Which bone IS the blade, i.e. what gets taken off the model when the above blocks it. `knife`
	// on the AK family (ak74/ak101/abakan), but GS names it per model -- the l85's is `bayonet`.
	m_sBayonetBone				= READ_IF_EXISTS(pSettings, r_string, section, "bayonet_bone", "knife");

	// Gunslinger scope-brightness feedback: a click played when the reticle/NV illumination step
	// changes (CWeapon::ChangeScopeIllum). Optional per weapon -- guarded so weapons without the
	// keys stay silent (LoadSound asserts on a missing line).
	if (pSettings->line_exist(section, "snd_scope_brightness_plus"))
		m_sounds.LoadSound(section, "snd_scope_brightness_plus", "sndScopeBrightPlus");
	if (pSettings->line_exist(section, "snd_scope_brightness_minus"))
		m_sounds.LoadSound(section, "snd_scope_brightness_minus", "sndScopeBrightMinus");

	// load ammo classes
	m_ammoTypes.clear	(); 
	LPCSTR				S = pSettings->r_string(section,"ammo_class");
	if (S && S[0]) 
	{
		string128		_ammoItem;
		int				count		= _GetItemCount	(S);
		for (int it=0; it<count; ++it)	
		{
			_GetItem				(S,it,_ammoItem);
			m_ammoTypes.push_back	(_ammoItem);
		}
		m_ammoName = pSettings->r_string(*m_ammoTypes[0],"inv_name_short");
	}
	else
		m_ammoName = 0;

	iAmmoElapsed		= pSettings->r_s32		(section,"ammo_elapsed"		);
	iMagazineSize		= pSettings->r_s32		(section,"ammo_mag_size"	);
	
	////////////////////////////////////////////////////
	// дисперсия стрельбы

	//подбрасывание камеры во время отдачи
	u8 rm = READ_IF_EXISTS( pSettings, r_u8, section, "cam_return", 1 );
	cam_recoil.ReturnMode = (rm == 1);
	
	rm = READ_IF_EXISTS( pSettings, r_u8, section, "cam_return_stop", 0 );
	cam_recoil.StopReturn = (rm == 1);

	float temp_f = 0.0f;
	temp_f					= pSettings->r_float( section,"cam_relax_speed" );
	cam_recoil.RelaxSpeed	= _abs( deg2rad( temp_f ) );
	VERIFY( !fis_zero(cam_recoil.RelaxSpeed) );
	if ( fis_zero(cam_recoil.RelaxSpeed) )
	{
		cam_recoil.RelaxSpeed = EPS_L;
	}

	cam_recoil.RelaxSpeed_AI = cam_recoil.RelaxSpeed;
	if ( pSettings->line_exist( section, "cam_relax_speed_ai" ) )
	{
		temp_f						= pSettings->r_float( section, "cam_relax_speed_ai" );
		cam_recoil.RelaxSpeed_AI	= _abs( deg2rad( temp_f ) );
		VERIFY( !fis_zero(cam_recoil.RelaxSpeed_AI) );
		if ( fis_zero(cam_recoil.RelaxSpeed_AI) )
		{
			cam_recoil.RelaxSpeed_AI = EPS_L;
		}
	}
	temp_f						= pSettings->r_float( section, "cam_max_angle" );
	cam_recoil.MaxAngleVert		= _abs( deg2rad( temp_f ) );
	VERIFY( !fis_zero(cam_recoil.MaxAngleVert) );
	if ( fis_zero(cam_recoil.MaxAngleVert) )
	{
		cam_recoil.MaxAngleVert = EPS;
	}
	
	temp_f						= pSettings->r_float( section, "cam_max_angle_horz" );
	cam_recoil.MaxAngleHorz		= _abs( deg2rad( temp_f ) );
	VERIFY( !fis_zero(cam_recoil.MaxAngleHorz) );
	if ( fis_zero(cam_recoil.MaxAngleHorz) )
	{
		cam_recoil.MaxAngleHorz = EPS;
	}
	
	temp_f						= pSettings->r_float( section, "cam_step_angle_horz" );
	cam_recoil.StepAngleHorz	= deg2rad( temp_f );
	
	cam_recoil.DispersionFrac	= _abs( READ_IF_EXISTS( pSettings, r_float, section, "cam_dispersion_frac", 0.7f ) );

	//подбрасывание камеры во время отдачи в режиме zoom ==> ironsight or scope
	//zoom_cam_recoil.Clone( cam_recoil ); ==== нельзя !!!!!!!!!!
	zoom_cam_recoil.RelaxSpeed		= cam_recoil.RelaxSpeed;
	zoom_cam_recoil.RelaxSpeed_AI	= cam_recoil.RelaxSpeed_AI;
	zoom_cam_recoil.DispersionFrac	= cam_recoil.DispersionFrac;
	zoom_cam_recoil.MaxAngleVert	= cam_recoil.MaxAngleVert;
	zoom_cam_recoil.MaxAngleHorz	= cam_recoil.MaxAngleHorz;
	zoom_cam_recoil.StepAngleHorz	= cam_recoil.StepAngleHorz;

	zoom_cam_recoil.ReturnMode		= cam_recoil.ReturnMode;
	zoom_cam_recoil.StopReturn		= cam_recoil.StopReturn;

	
	if ( pSettings->line_exist( section, "zoom_cam_relax_speed" ) )
	{
		zoom_cam_recoil.RelaxSpeed		= _abs( deg2rad( pSettings->r_float( section, "zoom_cam_relax_speed" ) ) );
		VERIFY( !fis_zero(zoom_cam_recoil.RelaxSpeed) );
		if ( fis_zero(zoom_cam_recoil.RelaxSpeed) )
		{
			zoom_cam_recoil.RelaxSpeed = EPS_L;
		}
	}
	if ( pSettings->line_exist( section, "zoom_cam_relax_speed_ai" ) )
	{
		zoom_cam_recoil.RelaxSpeed_AI	= _abs( deg2rad( pSettings->r_float( section,"zoom_cam_relax_speed_ai" ) ) ); 
		VERIFY( !fis_zero(zoom_cam_recoil.RelaxSpeed_AI) );
		if ( fis_zero(zoom_cam_recoil.RelaxSpeed_AI) )
		{
			zoom_cam_recoil.RelaxSpeed_AI = EPS_L;
		}
	}
	if ( pSettings->line_exist( section, "zoom_cam_max_angle" ) )
	{
		zoom_cam_recoil.MaxAngleVert	= _abs( deg2rad( pSettings->r_float( section, "zoom_cam_max_angle" ) ) );
		VERIFY( !fis_zero(zoom_cam_recoil.MaxAngleVert) );
		if ( fis_zero(zoom_cam_recoil.MaxAngleVert) )
		{
			zoom_cam_recoil.MaxAngleVert = EPS;
		}
	}
	if ( pSettings->line_exist( section, "zoom_cam_max_angle_horz" ) )
	{
		zoom_cam_recoil.MaxAngleHorz	= _abs( deg2rad( pSettings->r_float( section, "zoom_cam_max_angle_horz" ) ) ); 
		VERIFY( !fis_zero(zoom_cam_recoil.MaxAngleHorz) );
		if ( fis_zero(zoom_cam_recoil.MaxAngleHorz) )
		{
			zoom_cam_recoil.MaxAngleHorz = EPS;
		}
	}
	if ( pSettings->line_exist( section, "zoom_cam_step_angle_horz" ) )	{
		zoom_cam_recoil.StepAngleHorz	= deg2rad( pSettings->r_float( section, "zoom_cam_step_angle_horz" ) ); 
	}
	if ( pSettings->line_exist( section, "zoom_cam_dispersion_frac" ) )	{
		zoom_cam_recoil.DispersionFrac	= _abs( pSettings->r_float( section, "zoom_cam_dispersion_frac" ) );
	}

	m_pdm.m_fPDM_disp_base			= pSettings->r_float( section, "PDM_disp_base"			);
	m_pdm.m_fPDM_disp_vel_factor	= pSettings->r_float( section, "PDM_disp_vel_factor"	);
	m_pdm.m_fPDM_disp_accel_factor	= pSettings->r_float( section, "PDM_disp_accel_factor"	);
	m_pdm.m_fPDM_disp_crouch		= pSettings->r_float( section, "PDM_disp_crouch"		);
	m_pdm.m_fPDM_disp_crouch_no_acc	= pSettings->r_float( section, "PDM_disp_crouch_no_acc" );
	m_crosshair_inertion			= READ_IF_EXISTS(pSettings, r_float, section, "crosshair_inertion",	5.91f);
	m_first_bullet_controller.load	(section);

	fireDispersionConditionFactor = pSettings->r_float(section,"fire_dispersion_condition_factor");
	// GS detector_disp_factor: how much wider the cone gets while a detector is out in the other hand.
	// Absent key = 1 = untouched, so only the sections that opt in are affected (GS ships it on pistols).
	m_fDetectorDispFactor		= READ_IF_EXISTS(pSettings,r_float,section,"detector_disp_factor",1.f);
	misfireProbability			  = pSettings->r_float(section,"misfire_probability");
	misfireConditionK			  = READ_IF_EXISTS(pSettings, r_float, section, "misfire_condition_k",	1.0f);
	// Gunslinger condition-range misfire model (opt-in via misfire_start_condition)
	misfireStartCondition		  = READ_IF_EXISTS(pSettings, r_float, section, "misfire_start_condition", 0.0f);
	misfireEndCondition			  = READ_IF_EXISTS(pSettings, r_float, section, "misfire_end_condition",   0.0f);
	misfireStartProb			  = READ_IF_EXISTS(pSettings, r_float, section, "misfire_start_prob",      0.0f);
	misfireEndProb				  = READ_IF_EXISTS(pSettings, r_float, section, "misfire_end_prob",        0.0f);
	conditionDecreasePerShot	  = pSettings->r_float(section,"condition_shot_dec");
	conditionDecreasePerShotQueue = READ_IF_EXISTS(pSettings, r_float, section, "condition_queue_shot_dec", conditionDecreasePerShot);
		
	vLoadedFirePoint	= pSettings->r_fvector3		(section,"fire_point"		);
	
	if(pSettings->line_exist(section,"fire_point2")) 
		vLoadedFirePoint2= pSettings->r_fvector3	(section,"fire_point2");
	else 
		vLoadedFirePoint2= vLoadedFirePoint;

	// hands
	eHandDependence		= EHandDependence(pSettings->r_s32(section,"hand_dependence"));
	m_bIsSingleHanded	= true;
	if (pSettings->line_exist(section, "single_handed"))
		m_bIsSingleHanded	= !!pSettings->r_bool(section, "single_handed");
	// 
	m_fMinRadius		= pSettings->r_float		(section,"min_radius");
	m_fMaxRadius		= pSettings->r_float		(section,"max_radius");


	// информация о возможных апгрейдах и их визуализации в инвентаре
	m_eScopeStatus			 = (ALife::EWeaponAddonStatus)pSettings->r_s32(section,"scope_status");
	m_eSilencerStatus		 = (ALife::EWeaponAddonStatus)pSettings->r_s32(section,"silencer_status");
	m_eGrenadeLauncherStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section,"grenade_launcher_status");

	m_zoom_params.m_bZoomEnabled		= !!pSettings->r_bool(section,"zoom_enabled");
	m_zoom_params.m_fZoomRotateTime		= pSettings->r_float(section,"zoom_rotate_time");

	if ( m_eScopeStatus == ALife::eAddonAttachable )
	{
		m_sScopeName = pSettings->r_string(section, "scope_name");

		if (GameConstants::GetUseHQ_Icons())
		{
			m_iScopeX = pSettings->r_s32(section, "scope_x") * 2;
			m_iScopeY = pSettings->r_s32(section, "scope_y") * 2;
		}
		else
		{
			m_iScopeX = pSettings->r_s32(section, "scope_x");
			m_iScopeY = pSettings->r_s32(section, "scope_y");
		}
	}

	// GS multi-scope: `scopes_sect` = comma-separated list of per-weapon scope sections (each carrying its own
	// scope_name/bones/offsets/zoom/lens). When present the weapon accepts ANY of these scope items.
	// Load unconditionally (not gated on scope_status): the ak74's scope slot is enabled by a mechanic UPGRADE
	// that flips scope_status to attachable at runtime, but scopes_sect lives on the base weapon section.
	m_scopes.clear();
	if (pSettings->line_exist(section, "scopes_sect"))
	{
		LPCSTR list = pSettings->r_string(section, "scopes_sect");
		string256 one;
		for (int i = 0, n = _GetItemCount(list); i < n; ++i)
		{
			_GetItem(list, i, one);
			if (one[0])	m_scopes.push_back(one);
		}
	}

	if ( m_eSilencerStatus == ALife::eAddonAttachable )
	{
		m_sSilencerName = pSettings->r_string(section,"silencer_name");

		if (GameConstants::GetUseHQ_Icons())
		{
			m_iSilencerX = pSettings->r_s32(section, "silencer_x") * 2;
			m_iSilencerY = pSettings->r_s32(section, "silencer_y") * 2;
		}
		else
		{
			m_iSilencerX = pSettings->r_s32(section, "silencer_x");
			m_iSilencerY = pSettings->r_s32(section, "silencer_y");
		}
	}

	if ( m_eGrenadeLauncherStatus == ALife::eAddonAttachable )
	{
		m_sGrenadeLauncherName = pSettings->r_string(section,"grenade_launcher_name");

		if (GameConstants::GetUseHQ_Icons())
		{
			m_iGrenadeLauncherX = pSettings->r_s32(section, "grenade_launcher_x") * 2;
			m_iGrenadeLauncherY = pSettings->r_s32(section, "grenade_launcher_y") * 2;
		}
		else
		{
			m_iGrenadeLauncherX = pSettings->r_s32(section, "grenade_launcher_x");
			m_iGrenadeLauncherY = pSettings->r_s32(section, "grenade_launcher_y");
		}
	}

	InitAddons();
	if(pSettings->line_exist(section,"weapon_remove_time"))
		m_dwWeaponRemoveTime = pSettings->r_u32(section,"weapon_remove_time");
	else
		m_dwWeaponRemoveTime = WEAPON_REMOVE_TIME;

	if(pSettings->line_exist(section,"auto_spawn_ammo"))
		m_bAutoSpawnAmmo = pSettings->r_bool(section,"auto_spawn_ammo");
	else
		m_bAutoSpawnAmmo = TRUE;



	m_zoom_params.m_bHideCrosshairInZoom		= true;

	if(pSettings->line_exist(hud_sect, "zoom_hide_crosshair"))
		m_zoom_params.m_bHideCrosshairInZoom = !!pSettings->r_bool(hud_sect, "zoom_hide_crosshair");	

	Fvector			def_dof;
	def_dof.set		(-1,-1,-1);
	m_zoom_params.m_ZoomDof		= READ_IF_EXISTS(pSettings, r_fvector3, section, "zoom_dof", Fvector().set(-1,-1,-1));
	m_zoom_params.m_bZoomDofEnabled	= !def_dof.similar(m_zoom_params.m_ZoomDof);

	// GS ReadZoomDOFVector (ActorDOF.pas:320) keeps the aim DOF in the HUD section as three separate
	// keys over the [gunslinger_base] defaults, not as one vector in the weapon section. Every weapon
	// ported from GS already carries them -- they were simply never read here. They win over
	// `zoom_dof` where present, and their presence alone is enough to turn the aim DOF on.
	const CGamePersistent::SDofDefaults& DD = CGamePersistent::DofDefaults();
	if (pSettings->line_exist(hud_sect,"zoom_dof_near")  ||
		pSettings->line_exist(hud_sect,"zoom_dof_focus") ||
		pSettings->line_exist(hud_sect,"zoom_dof_far"))
	{
		if (!m_zoom_params.m_bZoomDofEnabled)	m_zoom_params.m_ZoomDof = DD.zoom;
		m_zoom_params.m_ZoomDof.x = READ_IF_EXISTS(pSettings,r_float,hud_sect,"zoom_dof_near", m_zoom_params.m_ZoomDof.x);
		m_zoom_params.m_ZoomDof.y = READ_IF_EXISTS(pSettings,r_float,hud_sect,"zoom_dof_focus",m_zoom_params.m_ZoomDof.y);
		m_zoom_params.m_ZoomDof.z = READ_IF_EXISTS(pSettings,r_float,hud_sect,"zoom_dof_far",  m_zoom_params.m_ZoomDof.z);
		m_zoom_params.m_bZoomDofEnabled = true;
	}
	// GS `disable_zoom_dof` (RefreshZoomDOF) -- an explicit opt-out for a weapon that must not blur.
	if (READ_IF_EXISTS(pSettings, r_bool, section, "disable_zoom_dof", FALSE))
		m_zoom_params.m_bZoomDofEnabled = false;
	// ...and the two speeds that make aiming in snappy and coming out slow (3 / 1 by default).
	m_zoom_params.m_fZoomInDofSpeed  = READ_IF_EXISTS(pSettings,r_float,hud_sect,"zoom_in_dof_speed", DD.speed_in);
	m_zoom_params.m_fZoomOutDofSpeed = READ_IF_EXISTS(pSettings,r_float,hud_sect,"zoom_out_dof_speed",DD.speed_out);

	// GS ReadLensDOFVector: the DOF used when aiming through a 3D PiP scope. GS's shipped values put
	// the focus at ~1m with the far plane at 2m, so the world around the scope body blurs while the
	// magnified image inside the lens (drawn at HUD depth) stays sharp.
	m_zoom_params.m_LensDof		= DD.zoom;
	m_zoom_params.m_LensDof.x	= READ_IF_EXISTS(pSettings,r_float,hud_sect,"lens_dof_near", m_zoom_params.m_LensDof.x);
	m_zoom_params.m_LensDof.y	= READ_IF_EXISTS(pSettings,r_float,hud_sect,"lens_dof_focus",m_zoom_params.m_LensDof.y);
	m_zoom_params.m_LensDof.z	= READ_IF_EXISTS(pSettings,r_float,hud_sect,"lens_dof_far",  m_zoom_params.m_LensDof.z);


	m_bHasTracers			= READ_IF_EXISTS(pSettings, r_bool, section, "tracers", true);
	m_u8TracerColorID		= READ_IF_EXISTS(pSettings, r_u8, section, "tracers_color_ID", u8(-1));

	string256						temp;
	for (int i=egdNovice; i<egdCount; ++i) 
	{
		strconcat					(sizeof(temp),temp,"hit_probability_",get_token_name(difficulty_type_token,i));
		m_hit_probability[i]		= READ_IF_EXISTS(pSettings,r_float,section,temp,1.f);
	}

	// Added by Axel, to enable optional condition use on any item
	m_flags.set(FUsingCondition, READ_IF_EXISTS(pSettings, r_bool, section, "use_condition", true));

	LoadLaserParams();
	LoadFlashlightParams();
}

// Read the laser designator params from the weapon's laser_params_section (if any). Installing it is
// normally the upgrade's job (laser_installed on the upgrade section, WeaponUpgrade.cpp) -- but a weapon
// can carry the designator PERMANENTLY, with no node to install: GS does that by giving the weapon
// section the [red_laser] parent (the P90). So `laser_installed` is honoured on the weapon section too;
// without this the module is on the model, the bone is visible, and the toggle key does nothing.
void CWeapon::LoadLaserParams()
{
	m_bLaserInstalled	= !!READ_IF_EXISTS(pSettings, r_bool, cNameSect().c_str(), "laser_installed", FALSE);
	m_sLaserBone		= NULL;
	m_LaserParticles.clear	();
	m_LaserSwitchDist.clear	();
	m_iLaserParticleIdx	= -1;
	m_vLaserOffset.set	(0.f,0.f,0.f);
	m_fLaserCosHudTreshold	= _cos(deg2rad(10.f));
	m_fLaserHudRecalcKoef	= 1.0f;
	m_bLaserCorrection		= TRUE;
	LPCSTR wsect = cNameSect().c_str();
	if (!pSettings->line_exist(wsect, "laser_params_section"))	return;
	LPCSTR lp = pSettings->r_string(wsect, "laser_params_section");
	if (!lp || !lp[0] || !pSettings->section_exist(lp))			return;

	if (pSettings->line_exist(lp, "laserdot_attach_bone"))	m_sLaserBone = pSettings->r_string(lp, "laserdot_attach_bone");
	// GS laser_ray_bones: the visible beam bones (e.g. "line, line2"). Shown with the dot, hidden when the
	// dot is suppressed (GS ProcessLaserdot SetWeaponMultipleBonesStatus(ray_bones, ...)). Statically shown by
	// the laser upgrade's show_bones; UpdateLaserDot takes over their visibility while the laser is enabled.
	if (pSettings->line_exist(lp, "laser_ray_bones"))		m_sLaserRayBones = pSettings->r_string(lp, "laser_ray_bones");
	// distance-switched dot: laserdot_particle_0 (default) + _1,_2,... each swapped in at laserdot_dist_<i>
	for (int i = 0; ; ++i)
	{
		string64 k;		xr_sprintf(k, "laserdot_particle_%d", i);
		if (!pSettings->line_exist(lp, k))	break;
		m_LaserParticles.push_back(pSettings->r_string(lp, k));
		if (i > 0)
		{
			string64 dk;	xr_sprintf(dk, "laserdot_dist_%d", i);
			m_LaserSwitchDist.push_back(READ_IF_EXISTS(pSettings, r_float, lp, dk, 0.f));
		}
	}
	m_vLaserOffset.x = READ_IF_EXISTS(pSettings, r_float, lp, "laserdot_attach_offset_x", 0.f);
	m_vLaserOffset.y = READ_IF_EXISTS(pSettings, r_float, lp, "laserdot_attach_offset_y", 0.f);
	m_vLaserOffset.z = READ_IF_EXISTS(pSettings, r_float, lp, "laserdot_attach_offset_z", 0.f);
	// world-model origin (GS WeaponAdditionalBuffer.pas:1291): used when there's no HUD item to hang the
	// dot off, so it's cast from the weapon's own transform instead of the hud bone.
	m_vLaserWorldOffset.x = READ_IF_EXISTS(pSettings, r_float, lp, "laserdot_world_attach_offset_x", 0.f);
	m_vLaserWorldOffset.y = READ_IF_EXISTS(pSettings, r_float, lp, "laserdot_world_attach_offset_y", 0.f);
	m_vLaserWorldOffset.z = READ_IF_EXISTS(pSettings, r_float, lp, "laserdot_world_attach_offset_z", 0.f);
	// camera-pull remap curve (GS laserdot correction): the dot is drawn at a compressed distance in front
	// of the camera so it never z-clips into surfaces and keeps a consistent screen size.
	m_LaserScaleDist.clear();	m_LaserScaleMul.clear();
	int nd = READ_IF_EXISTS(pSettings, r_s32, lp, "laserdot_dists_count", 0);
	for (int i = 1; i <= nd; ++i)
	{
		string64 tk, sk;	xr_sprintf(tk, "laserdot_dist_treshold_%d", i);	xr_sprintf(sk, "laserdot_dist_scale_%d", i);
		m_LaserScaleDist.push_back(READ_IF_EXISTS(pSettings, r_float, lp, tk, 0.f));
		m_LaserScaleMul.push_back (READ_IF_EXISTS(pSettings, r_float, lp, sk, 1.f));
	}
	// GS mode switch (there it's the laserdot_correction console cmd, on in every GS rspec preset)
	m_bLaserCorrection = READ_IF_EXISTS(pSettings, r_bool, lp, "laserdot_correction", TRUE);
	// world->hud POSITION reprojection strength for the real-depth (correction=off) dot, so it converges onto the
	// `line` bone on screen instead of sitting off it (IX-Ray renders the HUD weapon with a narrower psHUD_FOV
	// projection than the world). 0 = raw world position (old behavior), 1 = full GS cos-ratio. Tune per weapon.
	m_fLaserHudPointKoef = READ_IF_EXISTS(pSettings, r_float, lp, "laserdot_hud_point_koef", 1.0f);
	// boresight distance: aim the hip ray so the dot lands on screen center at this range (0 = off, use bone dir).
	m_fLaserZeroDist = READ_IF_EXISTS(pSettings, r_float, lp, "laserdot_zero_dist", 0.f);
	// how far up the ray the real-depth dot sits (GS 0.85 = 15% in front of the surface; 1.0 = on the object).
	m_fLaserSurfacePull = READ_IF_EXISTS(pSettings, r_float, lp, "laserdot_surface_pull", 0.85f);
	// GS reads laserdot_hud_treshold (degrees, default 10) from the weapon's HUD section: when the laser
	// direction deviates from the camera by more than this, the 1st-person dot is HIDDEN (GS stops the
	// particle instead of switching it to a HUD render -- WeaponAdditionalBuffer.pas PlayLaserdotParticle).
	if (hud_sect.size())
	{
		m_fLaserCosHudTreshold = _cos(deg2rad(READ_IF_EXISTS(pSettings, r_float, hud_sect, "laserdot_hud_treshold", 10.f)));
		m_fLaserHudRecalcKoef  = READ_IF_EXISTS(pSettings, r_float, hud_sect, "hud_recalc_koef", 1.0f);
	}
}

void CWeapon::StopLaserDot()
{
	if (m_pLaserDot)	CParticlesObject::Destroy(m_pLaserDot);
}

// GS collimator glitch (WeaponUpdate.pas ~952): the electronic reticle of a collimator sight fails during an
// emission. Reads `collimator_sights_bones` from the ACTIVE scope section (falls back to the weapon section) --
// e.g. metka_cobra for the cobra red-dot -- and hides those bones on the HUD model with a probability that
// ramps with the electronics-problems level: from the moment the level is >0 the reticle starts flickering,
// probability = level/collimator_problems_level, reaching 1 (solid off) at the peak, then recovering on the way
// down. The reticle stays lit normally otherwise. Only the active scope's reticle is touched.
void CWeapon::UpdateCollimatorGlitch()
{
	// A weapon can carry the collimator BUILT IN (the P90: no scope item, `collimator_sights_bones` on the
	// weapon section) -- so gate on the bones key, not on an attached scope.
	shared_str scope = IsScopeAttached() ? GetCurrentScopeSection() : shared_str();
	LPCSTR bones = (scope.size() && pSettings->line_exist(*scope, "collimator_sights_bones"))
					? pSettings->r_string(*scope, "collimator_sights_bones")
					: (pSettings->line_exist(cNameSect(), "collimator_sights_bones")
						? pSettings->r_string(cNameSect(), "collimator_sights_bones") : nullptr);
	if (!bones || !bones[0])	return;

	attachable_hud_item* hi = GetHUDmode() ? HudItemData() : nullptr;
	if (!hi || !hi->m_model)	return;

	extern float g_electronics_problems;
	float lvl = READ_IF_EXISTS(pSettings, r_float, "gwr_blowout", "collimator_disabling_level", 6.f);
	if (scope.size())	lvl = READ_IF_EXISTS(pSettings, r_float, *scope, "collimator_problems_level", lvl);

	BOOL show = TRUE;
	if (lvl > 0.f && g_electronics_problems > 0.f)
	{
		const float prob = (g_electronics_problems >= lvl) ? 1.f : (g_electronics_problems / lvl);
		if (::Random.randF() < prob)	show = FALSE;
	}
	// GS hide_collimator_sights_in_alter_zoom: the alter pose looks over/through the BACKUP sights, so the
	// red-dot housing's reticle is taken off the model for as long as that pose is held.
	if (show && AlterZoomBlend() > 0.5f
		&& READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "hide_collimator_sights_in_alter_zoom", FALSE))
		show = FALSE;
	gwr_SetWorldBonesCSV(hi->m_model, bones, show);
}

void CWeapon::ScheduleLaserToggle(bool e, u32 delay_ms)
{
	if (!delay_ms)	{ m_bLaserEnabled = e; m_dwLaserToggleAt = 0; return; }
	m_bLaserPendingState	= e;
	m_dwLaserToggleAt		= Device.dwTimeGlobal + delay_ms;
}

// GS TraceAsView (RayPick.pas): the laser ray uses CHudTarget's pick_trace_callback semantics -- static
// geometry whose material is vision-transparent (glass, foliage, nets: fVisTransparencyFactor keeps the
// accumulated power above 0.4) does NOT stop the ray, so the dot lands on what's visible BEHIND the alpha
// surface. Dynamic objects always stop it.
struct SLaserPickParam
{
	collide::rq_result	RQ;
	float				power;
};
ICF static BOOL laser_trace_callback(collide::rq_result& result, LPVOID params)
{
	SLaserPickParam* pp = (SLaserPickParam*)params;
	if (result.O)
	{
		pp->RQ = result;
		return FALSE;
	}
	CDB::TRI* T		= Level().ObjectSpace.GetStaticTris() + result.element;
	SGameMtl* mtl	= GMLib.GetMaterialByIdx(T->material);
	pp->power		*= mtl->fVisTransparencyFactor;
	if (pp->power > 0.4f)
		return TRUE;
	pp->RQ = result;
	return FALSE;
}

float CWeapon::TraceLaserAsView(const Fvector& pos, const Fvector& dir, float range, CObject* ignore)
{
	// NO OPT_CULL here (unlike CHudTarget and every other pick in the engine): with backface
	// culling the ray ignores any triangle whose winding faces away, so on level geometry built
	// single-sided the dot SHOOTS THROUGH the wall and lands on whatever is behind it -- visible
	// from the other side, where the same triangles face you, it works. A laser reflects off the
	// first surface it meets regardless of winding. Safe for the "see through glass/foliage"
	// behaviour below: _RayQuery r_sort()s the hits, so the callback still walks them nearest
	// first and stops at the first one that is not vision-transparent.
	collide::ray_defs RD(pos, dir, range, 0, collide::rqtBoth);
	if (fis_zero(RD.dir.square_magnitude()))	return range;
	SLaserPickParam pp;
	pp.RQ.set(NULL, range, -1);
	pp.power = 1.0f;
	static collide::rq_results RQR;
	RQR.r_clear();
	Level().ObjectSpace.RayQuery(RQR, RD, laser_trace_callback, &pp, NULL, ignore);
	return pp.RQ.range;
}

extern ENGINE_API float psHUD_FOV;	// hud fov as a fraction of the world fov

// GS ActorUtils.pas:3046 CorrectPointFromWorldToHud -- commented out in GS because CoP renders the HUD weapon at
// ~world FOV, so a world particle at a bone's true world position already lines up with the on-screen weapon.
// IX-Ray renders the HUD weapon with the NARROWER psHUD_FOV*fFOV projection (rmNear), so a world-rendered dot at
// the `line` bone's world position lands OFF the visible emitter -- and never converges onto it as you near a wall
// (the flashlight is immune: it's diffuse world lighting, not a sharp sprite). This remaps a world point P so that,
// rendered in the WORLD projection, it appears where the HUD projection would show P: keep the along-view-axis
// distance (depth/occlusion unchanged), scale only the camera-perpendicular (screen-plane) offset by the FOV cos
// ratio. koef scales the effect (0 = raw world pos, 1 = full ratio) for per-weapon live tuning.
static void LaserCorrectPointWorldToHud(Fvector& p, float koef)
{
	if (koef <= EPS_L)	return;
	const Fvector& cpos = Device.vCameraPosition;
	const Fvector& cdir = Device.vCameraDirection;			// normalized view axis
	Fvector v;		v.sub(p, cpos);							// camera -> point
	float along = v.dotproduct(cdir);						// signed depth along the view axis
	if (along <= EPS_L)	return;								// behind camera: leave it
	Fvector par;	par.mul(cdir, along);					// parallel (depth) component -- preserved
	Fvector perp;	perp.sub(v, par);						// perpendicular (screen-plane) component
	// HUD is drawn with fov = psHUD_FOV*fFOV (narrower). cos(hud)/cos(world) > 1 pushes the point further off the
	// view axis so the world sprite appears where the magnified HUD weapon draws the bone.
	float ratio = _cos(deg2rad(0.5f*psHUD_FOV*Device.fFOV)) / _cos(deg2rad(0.5f*Device.fFOV));
	float s = 1.f + (ratio - 1.f) * koef;
	perp.mul(s);
	p.add(cpos, par);	p.add(perp);
}

// Ray-cast from the laser bone (HUD model) and place the dot particle at the hit point (GS ProcessLaserdot,
// 1st-person branch): actor's active weapon, aim-blend to the crosshair when zoomed, hud-fov dir correction
// at hip, alpha-transparent trace, camera-pull correction (laserdot_correction) or real-depth mode.
void CWeapon::UpdateLaserDot()
{
	// pending toggle from the anm_laser_on/off gesture (the beam flips at lock_time_start like GS)
	if (m_dwLaserToggleAt && Device.dwTimeGlobal >= m_dwLaserToggleAt)
	{
		m_bLaserEnabled		= m_bLaserPendingState;
		m_dwLaserToggleAt	= 0;
	}
	if (!m_bLaserInstalled || !m_bLaserEnabled || m_LaserParticles.empty() || !m_sLaserBone.size())	{ StopLaserDot(); return; }
	// GS ProcessLaserdot (WeaponUpdate.pas ~127): during an emission the laser DOT is suppressed while the
	// actor's electronics-problems level is high -- the laser stays ENABLED (m_bLaserEnabled untouched), only
	// the projected dot is hidden, so it returns by itself after the surge. Solid off at the peak (prob=1
	// while the surge is still ramping/active), flickers on the way down. GS gates this on a per-weapon
	// `laser_problems_level` (default 0 = never); here `[gwr_blowout] laser_disabling_level` does it globally.
	{
		// GS ProcessLaserdot: during an emission the laser DOT is suppressed while the electronics-problems
		// level is high (the laser stays enabled; the dot returns after). The BEAM bones (line, line2) are
		// hidden in lockstep by CWeaponMagazined::gwr_UpdateBones, which runs every frame and is the bone
		// authority -- doing it here loses, because that re-applies the upgrade's show_bones each frame.
		extern float g_electronics_problems;
		const float laser_lvl = READ_IF_EXISTS(pSettings, r_float, "gwr_blowout", "laser_disabling_level", 8.f);
		if (laser_lvl > 0.f && g_electronics_problems >= laser_lvl)	{ StopLaserDot(); return; }
	}
	// GS disable_laserdot_when_gl_enabled (WeaponUpdate.pas): with the GL raised, stop the projected dot (the
	// far dot is what made the beam read as reaching the target -> without it the laser looks like a short stub,
	// "линия короче"). GS times the dot to the raise/lower anim, not instantly: it stays through the FIRST
	// laser_switch_time_to_gl ms of the switch-to-GL anim, then hides; on the way back it reappears only in the
	// LAST laser_switch_time_from_gl ms. m_dwGLSwitch* is the switch window (PlayAnimModeSwitch).
	if (READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "disable_laserdot_when_gl_enabled", FALSE))
	{
		const bool switching = (m_dwGLSwitchEndTm && Device.dwTimeGlobal < m_dwGLSwitchEndTm);
		if (switching)
		{
			if (IsGrenadeMode())	// raising the GL: keep the dot for to_gl ms, then drop it
			{
				u32 thr = READ_IF_EXISTS(pSettings, r_u32, cNameSect(), "laser_switch_time_to_gl", 500);
				if (Device.dwTimeGlobal - m_dwGLSwitchStartTm > thr)	{ StopLaserDot(); return; }
			}
			else					// lowering the GL: dot stays off until the last from_gl ms of the anim
			{
				u32 thr = READ_IF_EXISTS(pSettings, r_u32, cNameSect(), "laser_switch_time_from_gl", 700);
				if (m_dwGLSwitchEndTm - Device.dwTimeGlobal > thr)		{ StopLaserDot(); return; }
			}
		}
		else if (IsGrenadeMode())	{ StopLaserDot(); return; }	// fully in GL: dot off
	}
	// ---- WORLD branch (GS WeaponUpdate.pas:200) ----
	// No hud item to hang the dot off: cast the ray from the weapon's OWN transform +
	// laserdot_world_attach_offset_* instead of the hud bone, and place the dot at the real hit -- none of
	// the 1st-person work (zoom blend, hud-fov widening, camera-pull) applies here.
	// Exactly as GS gates it: ONLY the actor's own ACTIVE weapon. A dropped weapon or one in an NPC's hands
	// gets no dot at all (GS's trailing `else` is StopLaserdotParticle), even with the laser installed and on.
	attachable_hud_item* hi = GetHUDmode() ? HudItemData() : nullptr;
	if (!hi || !hi->m_model)
	{
		CObject* owner = H_Parent();
		bool actor_active = false;
		if (owner && owner == Level().CurrentEntity())
			if (CInventoryOwner* io = smart_cast<CInventoryOwner*>(owner))
				actor_active = (io->inventory().ActiveItem() == (CInventoryItem*)this);
		if (!actor_active)														{ StopLaserDot(); return; }

		const Fmatrix& X = XFORM();
		Fvector wpos;	X.transform_tiny(wpos, m_vLaserWorldOffset);
		Fvector wdir;	wdir.set(X.k);	wdir.normalize_safe();
		if (!_valid(wpos) || !_valid(wdir))										{ StopLaserDot(); return; }
		float wdist = TraceLaserAsView(wpos, wdir, 200.0f, smart_cast<CObject*>(this)) * 0.99f;
		Fvector wdot;	wdot.mad(wpos, wdir, wdist);
		PlaceLaserDot(wdot, 0);
		return;
	}

	IKinematics* K = hi->m_model;
	u16 bid = K->LL_BoneID(m_sLaserBone);
	if (bid == BI_NONE)															{ StopLaserDot(); return; }

	// laser bone -> world (same construction as the fire point): item_transform * bone_local
	Fmatrix full;	full.mul_43(hi->m_item_transform, K->LL_GetTransform(bid));
	Fvector pos;	full.transform_tiny(pos, m_vLaserOffset);
	Fvector dir;	dir.set(full.k);	dir.normalize_safe();
	if (!_valid(pos) || !_valid(dir))											{ StopLaserDot(); return; }

	// GS ProcessLaserdot 1st-person ray setup (WeaponUpdate.pas:153-177). Note GS's condition:
	// `if (IsAimNow or IsHolderInAimState) and (not IsGrenadeMode)` -- aiming the GRENADE LAUNCHER
	// does NOT pull the dot to the crosshair, because the sight you are looking through is the GL's,
	// not the weapon's; the dot keeps coming off the emitter like it does from the hip.
	// Hip and aim used to be an if/else on IsZoomed(), so every difference between them switched in ONE
	// frame and the dot visibly jumped as ADS began (user: "точка резко снапится"). m_fZoomRotationFactor
	// already ramps 0..1 over zoom_rotate_time in BOTH directions, so use it as a blend weight everywhere
	// instead of a boolean: at 0 this is exactly the old hip path, at 1 exactly the old aim path.
	// GS keeps the hip ray while the GRENADE LAUNCHER is up (the sight in use is the GL's), so aim_k is
	// forced to 0 there.
	float aim_k = IsGrenadeMode() ? 0.f : GetZoomRotationFactor();
	clamp(aim_k, 0.f, 1.f);
	const bool aim_ray = (aim_k >= 1.f);
	{
		Fvector cd;	cd.set(Device.vCameraDirection);

		// hip: CorrectDirFromWorldToHud -- the hud model is rendered with the narrower hud projection, so
		// the world-space ray's deviation from the camera must be widened by hud_recalc_koef*fov/hud_fov
		// for the dot to land where the beam visually points (psHUD_FOV is the hud/world fov fraction)
		const float m = m_fLaserHudRecalcKoef / psHUD_FOV;
		Fvector dir_hip;	dir_hip.sub(dir, cd);	dir_hip.mul(m);	dir_hip.add(cd);	dir_hip.normalize_safe();

		// aiming: blend the ray origin/direction from the laser bone toward the camera by the zoom factor,
		// so the dot converges exactly onto the crosshair at full ADS (dir/pos = cam + (cam-bone)*(f-1))
		const float f1 = aim_k - 1.f;
		Fvector t, dir_aim;
		t.sub(cd, dir);							t.mul(f1);
		dir_aim.add(cd, t);						dir_aim.normalize_safe();
		t.sub(Device.vCameraPosition, pos);		t.mul(f1);
		pos.add(Device.vCameraPosition, t);		// continuous already: f1 = -1 at hip gives the bone position

		dir.lerp(dir_hip, dir_aim, aim_k);		dir.normalize_safe();
		// (parallax zero is applied to the DOT position below, not the dir, so the gun sway is preserved)
	}

	// GS hud_treshold (laserdot_hud_treshold, GS base.ltx sets 90 = hide only when the laser points away
	// from the camera): GS stops the 1st-person dot instead of switching it to a HUD render
	if (Device.vCameraDirection.dotproduct(dir) < m_fLaserCosHudTreshold)		{ StopLaserDot(); return; }

	CObject* ignore = smart_cast<CObject*>(H_Parent());
	// GS: dist = TraceAsView(...)*0.99 -- alpha-transparent statics don't stop the ray
	float dist = TraceLaserAsView(pos, dir, 200.0f, ignore) * 0.99f;


	// GS's two modes (laserdot_correction on/off). OFF = the dot is drawn at (near) its REAL distance,
	// pulled 15% up the ray so the sprite clears the surface it lands on; real depth means the z-test
	// handles everything physically: the rmNear-squashed HUD hands/weapon always occlude it, bush leaves
	// occlude per-pixel (the dot shows through the gaps), glass tints it, walls cut it hard. Apparent size
	// is kept by switching laserdot_particle_0..N at laserdot_dist_1..N.
	const bool corr = m_bLaserCorrection && !m_LaserScaleDist.empty();
	// GS pulls the real-depth dot 15% up the ray off the surface (dist*=0.85). Combined with our z=-0.6 origin
	// pull-back that lands the dot noticeably in FRONT of the wall (closer to camera => the sprite looks bigger
	// than GS, where it appears to sit ON the object). Configurable via laserdot_surface_pull (1.0 = on the
	// surface, GS default 0.85). Tune toward 1 to seat the dot on the object and shrink the apparent size.
	if (!corr)		dist *= m_fLaserSurfacePull;

	Fvector dot;	dot.mad(pos, dir, dist);

	// PARALLAX ZERO (laserdot_zero_dist), applied to the DOT (not the dir) so the gun SWAY is preserved: the dot's
	// steady offset from screen center comes from the emitter sitting off the view axis by a ~constant vector P.
	// Subtract t*P where t ramps 0->1 over 0..zero_dist and CLAMPS at 1 beyond it, so:
	//  - close (t<1): only part of P removed -> the dot still runs to the device near a wall (converge, unchanged);
	//  - at/after zero_dist (t=1): full P removed -> the RESTING dot sits at screen center and STAYS there at any
	//    farther range (no drift), while the bone motion (which lives in dir*dist, not P) keeps it breathing.
	// ...faded out over the SAME aim blend (aim_k): at full ADS the ray already points at the crosshair,
	// so removing P again would double-correct -- but switching it off in one frame is exactly what made
	// the dot jump when ADS started, so it is scaled by (1 - aim_k) instead of gated on the boolean.
	if (m_fLaserZeroDist > 0.f && aim_k < 1.f)
	{
		Fvector rel;	rel.sub(pos, Device.vCameraPosition);					// emitter relative to camera
		float apos =	rel.dotproduct(Device.vCameraDirection);
		Fvector P;		P.mad(rel, Device.vCameraDirection, -apos);				// emitter's off-axis (perpendicular) part
		Fvector dv;		dv.sub(dot, Device.vCameraPosition);
		float along =	dv.dotproduct(Device.vCameraDirection);					// dot depth along the view axis
		float t = along / m_fLaserZeroDist;	clamp(t, 0.f, 1.f);
		t *= (1.f - aim_k);														// fade out across the ADS blend
		dot.mad(dot, P, -t);													// dot -= t*P

		// ...but that shift is a pure TRANSLATION in the screen plane, so on a wall that is not
		// perpendicular to the view it slides the dot OFF the surface it was computed on -- toward the
		// far side wherever the wall recedes. The dot then sits physically behind the wall (the log
		// showed it a steady 0.16-0.22 m past the hit), our own occlusion check sees a wall between the
		// eye and the dot, and hides it: "исчезает под определённым углом". Re-seat the shifted dot on
		// whatever surface is actually visible in its new direction -- only ever pulling it CLOSER, so
		// a shift into open air keeps its computed position and nothing new can appear in front.
		Fvector cd;		cd.sub(dot, Device.vCameraPosition);
		float cdd =		cd.magnitude();
		if (cdd > EPS_L)
		{
			cd.mul(1.f / cdd);
			const float hit = TraceLaserAsView(Device.vCameraPosition, cd, cdd, ignore) * 0.99f;
			if (hit < cdd)	dot.mad(Device.vCameraPosition, cd, hit);
		}
	}

	// Real-depth (correction=off) HIP dot: the trace/hit is in true world space, but the HUD weapon is drawn with
	// the narrower psHUD_FOV projection, so the world sprite lands off the visible `line` bone and doesn't converge
	// onto it near a wall (user: "не сдвигается в точку откуда идёт лазер"). Reproject the hit into HUD-apparent
	// screen space (depth preserved) so it tracks the emitter like the flashlight cone. Aim (zoom) already blends
	// to the crosshair; correction=on draws near the camera and is handled by its own camera-pull below.
	Fvector dot_raw = dot;	// pre-reprojection (debug)
	if (!corr && aim_k < 1.f)
		LaserCorrectPointWorldToHud(dot, m_fLaserHudPointKoef * (1.f - aim_k));	// faded over the ADS blend, not switched

	// TEMP DEBUG (laserdot convergence): throttled dump of the geometry so we can see why the dot doesn't track
	// the emitter. Remove once tuned. Prints only for the non-zoom real-depth path.
	if (!corr && !IsZoomed())
	{
		static u32 s_next = 0;
		if (Device.dwTimeGlobal >= s_next)
		{
			s_next = Device.dwTimeGlobal + 400;
			const Fmatrix& M = Device.mFullTransform;
			auto ndcx = [&](const Fvector& v){ float x=v.x*M._11+v.y*M._21+v.z*M._31+M._41; float w=v.x*M._14+v.y*M._24+v.z*M._34+M._44; return (_abs(w)>EPS_L)?x/w:0.f; };
			auto ndcy = [&](const Fvector& v){ float y=v.x*M._12+v.y*M._22+v.z*M._32+M._42; float w=v.x*M._14+v.y*M._24+v.z*M._34+M._44; return (_abs(w)>EPS_L)?y/w:0.f; };
			Msg("~LZR fov=%.1f hud=%.3f koef=%.2f dist=%.2f off(%.3f %.3f %.3f) | NDC bone(%.2f %.2f) posOff(%.2f %.2f) rawDot(%.2f %.2f) finDot(%.2f %.2f)",
				Device.fFOV, psHUD_FOV, m_fLaserHudPointKoef, dist, m_vLaserOffset.x, m_vLaserOffset.y, m_vLaserOffset.z,
				ndcx(full.c), ndcy(full.c), ndcx(pos), ndcy(pos), ndcx(dot_raw), ndcy(dot_raw), ndcx(dot), ndcy(dot));
		}
	}

	if (corr)
	{
		// physical occlusion (ours, not in GS): the camera-pull correction below draws the dot right in
		// front of the camera, so the z-test can no longer hide it when an obstacle blocks the VIEW of the
		// dot (weapon-ray vs eye parallax at edges -> a dim dot "through" the obstacle). Trace camera->dot
		// with the same vision-transparent callback (glass/nets do not occlude) and hide when blocked.
		{
			Fvector cdir;	cdir.sub(dot, Device.vCameraPosition);
			float cdist = cdir.magnitude();
			if (cdist > EPS_L)
			{
				cdir.mul(1.f / cdist);
				// Stop short of the surface the dot sits on. The margin must be ABSOLUTE, not a
				// percentage: the eye and the emitter see the same wall from different points, so at
				// a grazing angle the camera->dot ray clips the wall a few centimetres before the dot.
				// With the old flat 2% that slack was 2.4 cm at 1.2 m -> the dot hid itself against
				// any angled wall up close, and only "came back" past ~5 m where 2% finally exceeded
				// the parallax error (user: "вблизи не видно, отошёл на 5 метров - появился").
				// 12 cm covers the eye/emitter separation; the relative term keeps long shots sane.
				float vis_range = cdist - _max(0.12f, cdist * 0.02f);
				const float occl = (vis_range > EPS_L)
					? TraceLaserAsView(Device.vCameraPosition, cdir, vis_range, ignore) : vis_range;
				if (vis_range > EPS_L && occl < vis_range - EPS)
					{ StopLaserDot(); return; }
			}
		}

		// GS laserdot correction: pull the dot toward the camera along the camera->hit line to a COMPRESSED
		// distance (per the treshold/scale segments) so it renders in front of geometry (no z-clip into
		// surfaces -> "visible only on edges") at a consistent screen size, still exactly over the aim point.
		Fvector to_cam;	to_cam.sub(Device.vCameraPosition, dot);
		float d2cam = to_cam.magnitude();
		if (d2cam > EPS_L)
		{
			// NOTE the implicit GS zero-segment (InstallLaser, WeaponAdditionalBuffer.pas:1276-1277):
			// element[0] = {startdist 0, multiplier 1} -- the first treshold_1 meters are NOT compressed
			// (a point-blank dot sits at its REAL distance), and every farther distance carries that
			// uncompressed 0.5m. Missing this shifted the whole curve and made the dot ~2-4x too big.
			float newd = 0.f, tmp = d2cam;
			u32 n = m_LaserScaleDist.size();
			float seg = m_LaserScaleDist[0];
			if (tmp > seg)	{ tmp -= seg; newd += seg; }
			else			{ newd = tmp; tmp = 0.f; }
			for (u32 i = 1; i < n && tmp > 0.f; ++i)
			{
				seg = m_LaserScaleDist[i] - m_LaserScaleDist[i-1];
				if (tmp > seg)	{ tmp -= seg; newd += seg * m_LaserScaleMul[i-1]; }
				else			{ newd += tmp * m_LaserScaleMul[i-1]; tmp = 0.f; }
			}
			newd += tmp * m_LaserScaleMul[n-1];
			to_cam.mul(1.f / d2cam);					// normalize
			dot.mad(dot, to_cam, d2cam - newd);			// move toward camera so |cam-dot| == newd
		}
	}

	// correction mode always uses particle_0 (the compression keeps a fixed apparent size); the real-depth
	// mode picks the particle by distance (GS distswitch: >= laserdot_dist_i -> particle_i)
	int idx = 0;
	if (!corr)
	{
		for (u32 j = 0; j < m_LaserSwitchDist.size(); ++j)
			if (dist >= m_LaserSwitchDist[j])	idx = int(j) + 1;
		if (idx >= int(m_LaserParticles.size()))	idx = int(m_LaserParticles.size()) - 1;
	}
	// TEMP DEBUG (dot size): which particle + how far the dot sits from the camera (apparent size ~ 1/d2cam)
	if (!corr && !IsZoomed())
	{
		static u32 s_nsz = 0;
		if (Device.dwTimeGlobal >= s_nsz)
		{
			s_nsz = Device.dwTimeGlobal + 400;
			Fvector dd;	dd.sub(dot, Device.vCameraPosition);
			Msg("~LZRSZ idx=%d particle=%s dist=%.2f d2cam=%.3f", idx,
				(idx>=0 && idx<int(m_LaserParticles.size())) ? m_LaserParticles[idx].c_str() : "?", dist, dd.magnitude());
		}
	}
	PlaceLaserDot(dot, idx);
}

// Create (or re-create on a particle switch) the dot and park it at `dot`, billboarded to the camera.
void CWeapon::PlaceLaserDot(const Fvector& dot, int idx)
{
	if (idx < 0 || idx >= int(m_LaserParticles.size()))	return;
	if (!m_pLaserDot || m_iLaserParticleIdx != idx)
	{
		StopLaserDot();
		m_pLaserDot			= CParticlesObject::Create(m_LaserParticles[idx].c_str(), FALSE, false);
		m_iLaserParticleIdx	= idx;
	}
	// face the dot toward the camera (billboard) so the sprite isn't seen edge-on
	Fmatrix xf;	xf.identity();
	Fvector to_cam2;	to_cam2.sub(Device.vCameraPosition, dot);
	if (to_cam2.magnitude() > EPS_L)
	{
		to_cam2.normalize();
		xf.k.set(to_cam2);
		Fvector::generate_orthonormal_basis_normalized(xf.k, xf.j, xf.i);
	}
	xf.c.set(dot);
	m_pLaserDot->SetXFORM(xf);
	// WORLD render like GS (SetParticlesHudStatus(false)): the dot depth-tests against the scene and the
	// hands/weapon (the HUD pass draws over everything and with the hud-fov projection, which also made the
	// sprite oversized). Smooth tracking comes from SetXFORM (rigid whole-system move) each frame, not from
	// the render pass. NOTE CParticlesObject::Play(true) latches SetHudMode(true) and never clears it.
	if (!m_pLaserDot->IsPlaying())	m_pLaserDot->Play(false);
}

// ---- weapon-mounted flashlight (GS LightUtils NewTorchlight port) --------------------------------
void CWeapon::LoadFlashlightParams()
{
	m_sFlashBone	= NULL;
	m_vFlashOffset.set(0.f,0.f,0.f);
	LPCSTR wsect = cNameSect().c_str();
	if (!pSettings->line_exist(wsect, "flashlight_params_section"))	return;
	LPCSTR fp = pSettings->r_string(wsect, "flashlight_params_section");
	if (!fp || !fp[0] || !pSettings->section_exist(fp))				return;

	if (pSettings->line_exist(fp, "torch_light_bone"))	m_sFlashBone = pSettings->r_string(fp, "torch_light_bone");
	m_vFlashOffset.x = READ_IF_EXISTS(pSettings, r_float, fp, "torch_attach_offset_x", 0.f);
	m_vFlashOffset.y = READ_IF_EXISTS(pSettings, r_float, fp, "torch_attach_offset_y", 0.f);
	m_vFlashOffset.z = READ_IF_EXISTS(pSettings, r_float, fp, "torch_attach_offset_z", 0.f);
	// world-model origins (GS LightUtils.pas:362-372). The omni pair defaults to the spot pair, as in GS.
	m_vFlashWorldOffset.x = READ_IF_EXISTS(pSettings, r_float, fp, "torch_world_attach_offset_x", 0.f);
	m_vFlashWorldOffset.y = READ_IF_EXISTS(pSettings, r_float, fp, "torch_world_attach_offset_y", 0.f);
	m_vFlashWorldOffset.z = READ_IF_EXISTS(pSettings, r_float, fp, "torch_world_attach_offset_z", 0.f);
	m_vFlashOmniWorldOffset.x = READ_IF_EXISTS(pSettings, r_float, fp, "torch_omni_world_attach_offset_x", m_vFlashWorldOffset.x);
	m_vFlashOmniWorldOffset.y = READ_IF_EXISTS(pSettings, r_float, fp, "torch_omni_world_attach_offset_y", m_vFlashWorldOffset.y);
	m_vFlashOmniWorldOffset.z = READ_IF_EXISTS(pSettings, r_float, fp, "torch_omni_world_attach_offset_z", m_vFlashWorldOffset.z);
	// r2 (deferred) colour/range set -- IX-Ray runs r2/r3
	m_FlashColor.set(
		READ_IF_EXISTS(pSettings, r_float, fp, "torch_r2_color_r", 0.6f),
		READ_IF_EXISTS(pSettings, r_float, fp, "torch_r2_color_g", 0.55f),
		READ_IF_EXISTS(pSettings, r_float, fp, "torch_r2_color_b", 0.55f),
		READ_IF_EXISTS(pSettings, r_float, fp, "torch_r2_color_a", 1.0f));
	m_fFlashRange	= READ_IF_EXISTS(pSettings, r_float, fp, "torch_r2_range", 50.f);
	m_fFlashCone	= deg2rad(READ_IF_EXISTS(pSettings, r_float, fp, "torch_spot_angle", 60.f));
	m_sFlashSpotTex	= pSettings->line_exist(fp, "torch_spot_texture") ? pSettings->r_string(fp, "torch_spot_texture") : "internal\\internal_light_torch_r2";
	m_FlashOmniColor.set(
		READ_IF_EXISTS(pSettings, r_float, fp, "torch_r2_omni_color_r", 1.0f),
		READ_IF_EXISTS(pSettings, r_float, fp, "torch_r2_omni_color_g", 1.0f),
		READ_IF_EXISTS(pSettings, r_float, fp, "torch_r2_omni_color_b", 1.0f),
		READ_IF_EXISTS(pSettings, r_float, fp, "torch_r2_omni_color_a", 0.0f));
	m_fFlashOmniRange	= READ_IF_EXISTS(pSettings, r_float, fp, "torch_r2_omni_range", 0.25f);
	m_bFlashGlow		= !!READ_IF_EXISTS(pSettings, r_bool, fp, "create_glow", TRUE);
	m_sFlashGlowTex		= pSettings->line_exist(fp, "torch_glow_texture") ? pSettings->r_string(fp, "torch_glow_texture") : "glow\\glow_torch_r2";
	m_fFlashGlowRadius	= READ_IF_EXISTS(pSettings, r_float, fp, "torch_glow_radius", 0.3f);
}

void CWeapon::StopFlashlight()
{
	if (m_pFlashSpot)	m_pFlashSpot->set_active(false);
	if (m_pFlashOmni)	m_pFlashOmni->set_active(false);
	if (m_pFlashGlowObj)	m_pFlashGlowObj->set_active(false);
	m_fFlashFade = 0.f;		// so the next draw fades the light back in from dark (task)
}

void CWeapon::ScheduleFlashlightToggle(bool e, u32 delay_ms)
{
	if (!delay_ms)	{ m_bFlashEnabled = e; m_dwFlashToggleAt = 0; return; }
	m_bFlashPendingState	= e;
	m_dwFlashToggleAt		= Device.dwTimeGlobal + delay_ms;
}

// Position the spot/omni/glow at the flash bone (HUD model) each render frame. Mirrors CTorch's
// bone-attached light but in HUD space (like the laser dot), so the beam tracks the gun smoothly.
void CWeapon::UpdateFlashlight()
{
	// pending toggle from the anm_torch_on/off gesture (flips at lock_time_start like GS)
	if (m_dwFlashToggleAt && Device.dwTimeGlobal >= m_dwFlashToggleAt)
	{
		m_bFlashEnabled		= m_bFlashPendingState;
		m_dwFlashToggleAt	= 0;
	}
	if (!m_bFlashInstalled || !m_bFlashEnabled)							{ StopFlashlight(); return; }

	// GS WeaponAdditionalBuffer.pas:1490 -- three cases:
	//  1. the ACTOR owns it but it isn't the active item -> light off (it stays `enabled`, just not shining)
	//  2. hud item present AND it's the actor's active item -> 1st-person, off the HUD bone
	//  3. otherwise (NPC-held, dropped, or no hud) -> WORLD, off the weapon's own transform
	CObject* owner = H_Parent();
	bool is_actor_owned = (owner && owner == Level().CurrentEntity());
	attachable_hud_item* hi = (GetHUDmode() && m_sFlashBone.size()) ? HudItemData() : nullptr;
	bool hud_branch = (hi && hi->m_model && is_actor_owned);
	if (is_actor_owned && !hud_branch)									{ StopFlashlight(); return; }

	Fvector pos, dir, omnipos, right;
	if (hud_branch)
	{
		IKinematics* K = hi->m_model;
		u16 bid = K->LL_BoneID(m_sFlashBone);
		if (bid == BI_NONE)												{ StopFlashlight(); return; }
		Fmatrix full;	full.mul_43(hi->m_item_transform, K->LL_GetTransform(bid));
		full.transform_tiny(pos, m_vFlashOffset);
		omnipos = pos;
		dir.set(full.k);	dir.normalize_safe();
		right.set(full.i);	right.normalize_safe();
	}
	else
	{
		// world: origin = weapon XFORM applied to the world offsets, direction = the barrel (fire) direction
		const Fmatrix& X = XFORM();
		X.transform_tiny(pos,     m_vFlashWorldOffset);
		X.transform_tiny(omnipos, m_vFlashOmniWorldOffset);
		dir.set(X.k);		dir.normalize_safe();
		right.set(X.i);		right.normalize_safe();
	}
	if (!_valid(pos) || !_valid(dir) || !_valid(omnipos))				{ StopFlashlight(); return; }

	// lazily create the lights (r2/r3 deferred)
	if (!m_pFlashSpot)
	{
		m_pFlashSpot = ::Render->light_create();
		m_pFlashSpot->set_type(IRender_Light::SPOT);
		m_pFlashSpot->set_shadow(true);
		m_pFlashSpot->set_cone(m_fFlashCone);
		m_pFlashSpot->set_range(m_fFlashRange);
		m_pFlashSpot->set_color(m_FlashColor);
		m_pFlashSpot->set_texture(m_sFlashSpotTex.c_str());
		// WORLD light, NOT hud: the muzzle-mounted flashlight sits FORWARD of the hands, so it must light the
		// scene ahead -- never the HUD hands (hud_mode would light only the hands and nothing else). The main
		// character headlamp (CTorch) is the one that lights the hands.
		m_pFlashSpot->set_hud_mode(false);
		// ...but keep the ACTOR's own body out of its shadow map: the light sits on his weapon, so the
		// silhouette it would cast of him comes from inside him. Only matters with r__actor_shadow on.
		m_pFlashSpot->set_actor_shadow(false);
		m_pFlashOmni = ::Render->light_create();
		m_pFlashOmni->set_type(IRender_Light::POINT);
		m_pFlashOmni->set_shadow(false);
		m_pFlashOmni->set_range(m_fFlashOmniRange);
		m_pFlashOmni->set_color(m_FlashOmniColor);
		m_pFlashOmni->set_hud_mode(false);
		if (m_bFlashGlow)
		{
			m_pFlashGlowObj = ::Render->glow_create();
			m_pFlashGlowObj->set_texture(m_sFlashGlowTex.c_str());
			m_pFlashGlowObj->set_color(m_FlashColor);
			m_pFlashGlowObj->set_radius(m_fFlashGlowRadius);
		}
	}

	// The mounted flashlight is a WORLD light in both first-person and world views (it lights the scene, not
	// the HUD hands), so no hud<->world re-flag is needed -- it stays world-mode always.

	// draw/holster fade (task):
	//  - DRAW (eShowing): ramp the light in over the show anim (looks good, keep).
	//  - HOLSTER (eHiding): stay full and only fade out in the LAST FLASH_FADE_TIME of the hide anim, so the
	//    beam dies right as the weapon leaves view instead of dimming through the whole holster.
	//  - everything else (idle/fire, key-toggle on): snap to full -- toggling the flashlight on/off with the
	//    key must be ABRUPT, not a fade.
	const float FLASH_FADE_TIME = 0.22f;
	const float fade_step = (FLASH_FADE_TIME > 0.f) ? (Device.fTimeDelta / FLASH_FADE_TIME) : 1.0f;
	float fade_target;
	u32 wstate = GetState();
	if (!hud_branch)
	{
		// dropped / NPC-held WORLD weapon: the draw/holster fade is a 1st-person-only concept and its eShowing/
		// eHiding states don't apply here -- a dropped weapon can sit in eHiding (SwitchState(eHidden) on drop)
		// whose MotionEndTm() is already past, which would drive fade_target to 0 and kill the light. Force full.
		m_fFlashFade = 1.0f;
		fade_target  = 1.0f;
	}
	else if (wstate == eShowing)
	{
		fade_target = 1.0f;								// fade in over the draw
	}
	else if (wstate == eHiding)
	{
		u32 end = MotionEndTm();
		float remain = (end > Device.dwTimeGlobal) ? (float(end - Device.dwTimeGlobal) / 1000.0f) : 0.0f;
		fade_target = (remain <= FLASH_FADE_TIME) ? 0.0f : 1.0f;	// hold full until the hide anim's last moment
	}
	else
	{
		m_fFlashFade = 1.0f;							// abrupt on key-toggle / steady state
		fade_target  = 1.0f;
	}
	if (m_fFlashFade < fade_target)			m_fFlashFade += fade_step;
	else if (m_fFlashFade > fade_target)	m_fFlashFade -= fade_step;
	clamp(m_fFlashFade, 0.0f, 1.0f);
	if (m_fFlashFade <= 0.f)	{ StopFlashlight(); return; }

	Fcolor spot_clr = m_FlashColor;		spot_clr.mul_rgb(m_fFlashFade);
	Fcolor omni_clr = m_FlashOmniColor;	omni_clr.mul_rgb(m_fFlashFade);

	m_pFlashSpot->set_color(spot_clr);
	m_pFlashSpot->set_position(pos);
	m_pFlashSpot->set_rotation(dir, right);
	m_pFlashSpot->set_active(true);
	m_pFlashOmni->set_color(omni_clr);
	m_pFlashOmni->set_position(omnipos);
	m_pFlashOmni->set_rotation(dir, right);
	m_pFlashOmni->set_active(true);
	if (m_pFlashGlowObj)
	{
		// The glow is a billboard sitting ON the device, so it is meant to be read from a distance.
		// In 1st person the device is already ~0.3 m from the eye and ADS pulls it closer still --
		// the sprite then fills the screen as a haze and lights the weapon back (user: "при включении
		// фонаря на камере появляется свечение, в aim позе оружие подсвечивается"). Fade it out over
		// the last GLOW_NEAR metres so it disappears exactly when it would start glaring, and keep it
		// unchanged at any normal viewing distance (world weapon, other actors, dropped).
		const float GLOW_NEAR = 0.6f, GLOW_FULL = 1.2f;
		float d = Device.vCameraPosition.distance_to(pos);
		float k = (d - GLOW_NEAR) / (GLOW_FULL - GLOW_NEAR);
		clamp(k, 0.f, 1.f);
		if (k <= EPS_L)
			m_pFlashGlowObj->set_active(false);
		else
		{
			Fcolor gc = spot_clr;	gc.mul_rgb(k);
			m_pFlashGlowObj->set_color(gc);
			m_pFlashGlowObj->set_position(pos);
			m_pFlashGlowObj->set_direction(dir);
			m_pFlashGlowObj->set_active(true);
		}
	}
}

void CWeapon::LoadFireParams		(LPCSTR section)
{
	cam_recoil.Dispersion = deg2rad( pSettings->r_float( section,"cam_dispersion" ) ); 
	cam_recoil.DispersionInc = 0.0f;

	if ( pSettings->line_exist( section, "cam_dispersion_inc" ) )	{
		cam_recoil.DispersionInc = deg2rad( pSettings->r_float( section, "cam_dispersion_inc" ) ); 
	}
	
	zoom_cam_recoil.Dispersion		= cam_recoil.Dispersion;
	zoom_cam_recoil.DispersionInc	= cam_recoil.DispersionInc;

	if ( pSettings->line_exist( section, "zoom_cam_dispersion" ) )	{
		zoom_cam_recoil.Dispersion		= deg2rad( pSettings->r_float( section, "zoom_cam_dispersion" ) ); 
	}
	if ( pSettings->line_exist( section, "zoom_cam_dispersion_inc" ) )	{
		zoom_cam_recoil.DispersionInc	= deg2rad( pSettings->r_float( section, "zoom_cam_dispersion_inc" ) ); 
	}

	CShootingObject::LoadFireParams(section);
};



BOOL CWeapon::net_Spawn		(CSE_Abstract* DC)
{
	BOOL bResult					= inherited::net_Spawn(DC);
	CSE_Abstract					*e	= (CSE_Abstract*)(DC);
	CSE_ALifeItemWeapon			    *E	= smart_cast<CSE_ALifeItemWeapon*>(e);

	//iAmmoCurrent					= E->a_current;
	iAmmoElapsed					= E->a_elapsed;
	m_flagsAddOnState				= E->m_addon_flags.get();
	m_ammoType						= E->ammo_type;
	SetState						(E->wpn_state);
	SetNextState					(E->wpn_state);
	
	m_DefaultCartridge.Load(*m_ammoTypes[m_ammoType], u8(m_ammoType));	
	if(iAmmoElapsed) 
	{
		m_fCurrentCartirdgeDisp = m_DefaultCartridge.param_s.kDisp;
		for(int i = 0; i < iAmmoElapsed; ++i) 
			m_magazine.push_back(m_DefaultCartridge);
	}

	UpdateAddonsVisibility();
	InitAddons();

	m_dwWeaponIndependencyTime = 0;

	VERIFY((u32)iAmmoElapsed == m_magazine.size());
	m_bAmmoWasSpawned		= false;

	return bResult;
}

void CWeapon::net_Destroy	()
{
	inherited::net_Destroy	();

	//удалить объекты партиклов
	StopFlameParticles	();
	StopFlameParticles2	();
	StopLight			();
	StopLaserDot		();
	Light_Destroy		();

	while (m_magazine.size()) m_magazine.pop_back();
}

BOOL CWeapon::IsUpdating()
{	
	bool bIsActiveItem = m_pInventory && m_pInventory->ActiveItem()==this;
	return bIsActiveItem || bWorking || IsPending() || getVisible();
}

void CWeapon::net_Export(NET_Packet& P)
{
	inherited::net_Export	(P);

	P.w_float_q8			(GetCondition(),0.0f,1.0f);


	u8 need_upd				= IsUpdating() ? 1 : 0;
	P.w_u8					(need_upd);
	P.w_u16					(u16(iAmmoElapsed));
	P.w_u8					(m_flagsAddOnState);
	P.w_u8					((u8)m_ammoType);
	P.w_u8					((u8)GetState());
	P.w_u8					((u8)IsZoomed());
}

void CWeapon::net_Import(NET_Packet& P)
{
	inherited::net_Import (P);
	
	float _cond;
	P.r_float_q8			(_cond,0.0f,1.0f);
	SetCondition			(_cond);

	u8 flags				= 0;
	P.r_u8					(flags);

	u16 ammo_elapsed = 0;
	P.r_u16					(ammo_elapsed);

	u8						NewAddonState;
	P.r_u8					(NewAddonState);

	m_flagsAddOnState		= NewAddonState;
	UpdateAddonsVisibility	();

	u8 ammoType, wstate;
	P.r_u8					(ammoType);
	P.r_u8					(wstate);

	u8 Zoom;
	P.r_u8					((u8)Zoom);

	if (H_Parent() && H_Parent()->Remote())
	{
		if (Zoom) OnZoomIn();
		else OnZoomOut();
	};
	switch (wstate)
	{	
	case eFire:
	case eFire2:
	case eSwitch:
	case eReload:
		{
		}break;	
	default:
		{
			if (ammoType >= m_ammoTypes.size())
				Msg("!! Weapon [%d], State - [%d]", ID(), wstate);
			else
			{
				m_ammoType = ammoType;
				SetAmmoElapsed((ammo_elapsed));
			}
		}break;
	}
	
	VERIFY((u32)iAmmoElapsed == m_magazine.size());
}

void CWeapon::save(NET_Packet &output_packet)
{
	inherited::save	(output_packet);
	save_data		(iAmmoElapsed,					output_packet);
	save_data		(m_flagsAddOnState, 			output_packet);
	save_data		(m_ammoType,					output_packet);
	save_data		(m_zoom_params.m_bIsZoomModeNow,output_packet);
	save_data		(m_bLaserEnabled,				output_packet);	// GS laser toggle survives save/load (SAVE FORMAT +1 byte)
	save_data		(m_bFlashEnabled,				output_packet);	// GS flashlight toggle (SAVE FORMAT +1 byte)
	save_data		(m_cur_scope,					output_packet);	// GS multi-scope: which scope is attached (SAVE FORMAT +1 byte)
	for (int i = 0; i < 16; ++i)	save_data(m_scope_illum_step_by_scope[i], output_packet);	// per-scope brightness (SAVE FORMAT +64 bytes)
}

void CWeapon::load(IReader &input_packet)
{
	inherited::load	(input_packet);
	load_data		(iAmmoElapsed,					input_packet);
	load_data		(m_flagsAddOnState,				input_packet);
	UpdateAddonsVisibility			();
	load_data		(m_ammoType,					input_packet);
	load_data		(m_zoom_params.m_bIsZoomModeNow,input_packet);
	load_data		(m_bLaserEnabled,				input_packet);
	load_data		(m_bFlashEnabled,				input_packet);
	load_data		(m_cur_scope,					input_packet);	// GS multi-scope
	for (int i = 0; i < 16; ++i)	load_data(m_scope_illum_step_by_scope[i], input_packet);	// per-scope brightness
	{	// restore the active scope's brightness step
		const int idx = (m_cur_scope < 16) ? (int)m_cur_scope : 0;
		if (m_scope_illum_step_by_scope[idx] >= 0)	m_scope_illum_step = m_scope_illum_step_by_scope[idx];
		LoadScopeIllumParams();
	}
	// GS variable magnification is runtime-only (deliberately NOT in the save stream, so loading an old save
	// stays compatible) -- re-derive it from the attached scope's section.
	ResetLensStepToDefault();
	m_bAlterZoom = false;

	if (m_zoom_params.m_bIsZoomModeNow)
			OnZoomIn();
		else			
			OnZoomOut();
}


void CWeapon::OnEvent(NET_Packet& P, u16 type) 
{
	switch (type)
	{
	case GE_ADDON_CHANGE:
		{
			P.r_u8					(m_flagsAddOnState);
			InitAddons();
			UpdateAddonsVisibility();
		}break;

	case GE_WPN_STATE_CHANGE:
		{
			u8				state;
			P.r_u8			(state);
			P.r_u8			(m_sub_state);		
//			u8 NewAmmoType = 
				P.r_u8();
			u8 AmmoElapsed = P.r_u8();
			u8 NextAmmo = P.r_u8();
			if (NextAmmo == u8(-1))
				m_set_next_ammoType_on_reload = u32(-1);
			else
				m_set_next_ammoType_on_reload = u8(NextAmmo);

			if (OnClient()) SetAmmoElapsed(int(AmmoElapsed));			
			OnStateSwitch	(u32(state));
		}
		break;
	default:
		{
			inherited::OnEvent(P,type);
		}break;
	}
};

void CWeapon::shedule_Update	(u32 dT)
{
	// Queue shrink
//	u32	dwTimeCL		= Level().timeServer()-NET_Latency;
//	while ((NET.size()>2) && (NET[1].dwTimeStamp<dwTimeCL)) NET.pop_front();	

	// Inherited
	inherited::shedule_Update	(dT);
}

void CWeapon::OnH_B_Independent	(bool just_before_destroy)
{
	RemoveShotEffector			();

	// Dropped from hands -> becomes a world object. The UpdateCL world-branch (the UpdateFlashlight call for
	// weapons the actor isn't holding) keeps ticking a dropped/NPC-held weapon's mounted flashlight, so KEEP
	// it lit when it was on -- a dropped weapon with the flashlight on goes on shining on the ground. The laser
	// dot is restricted to the actor's own active weapon (UpdateLaserDot world-branch), so still kill it here.
	if (!m_bFlashEnabled)		StopFlashlight();
	StopLaserDot				();

	inherited::OnH_B_Independent(just_before_destroy);

	FireEnd						();
	SetPending					(FALSE);
	SwitchState					(eHidden);

	m_strapped_mode				= false;
	m_zoom_params.m_bIsZoomModeNow	= false;
	UpdateXForm					();

}

void CWeapon::OnH_A_Independent	()
{
	m_dwWeaponIndependencyTime = Level().timeServer();
	inherited::OnH_A_Independent();
	Light_Destroy				();
	UpdateAddonsVisibility		();
};

void CWeapon::OnH_A_Chield		()
{
	inherited::OnH_A_Chield		();
	UpdateAddonsVisibility		();
	// Picked up into an inventory. A dropped weapon keeps its mounted flashlight lit (see
	// OnH_B_Independent), and that light is driven by the UpdateCL WORLD branch -- which is gated on
	// `H_Parent() != Level().CurrentEntity()`. The moment the ACTOR takes it, that branch stops running
	// and CActor::UpdateCL only ticks the ACTIVE item, so nothing ever calls UpdateFlashlight again and
	// the ref_lights stay set_active(true) frozen where the weapon was lying. Kill them here; if the
	// weapon is (or becomes) the active one, UpdateFlashlight re-creates and re-activates them on its
	// next tick. An NPC picking it up still has the world branch, so it just relights next frame.
	StopFlashlight				();
};

void CWeapon::OnActiveItem ()
{
	//. from Activate
	UpdateAddonsVisibility();
	m_dwAmmoCurrentCalcFrame = 0;

//. Show
	SwitchState					(eShowing);
//-

	inherited::OnActiveItem		();
	//если мы занружаемся и оружие было в руках
//.	SetState					(eIdle);
//.	SetNextState				(eIdle);
}

void CWeapon::OnHiddenItem ()
{
	m_dwAmmoCurrentCalcFrame = 0;
	StopFlashlight	();		// don't leave the mounted light on after holstering (UpdateFlashlight only runs for the active weapon)
	StopLaserDot	();
//. Hide
	if(IsGameTypeSingle())
		SwitchState(eHiding);
	else
		SwitchState(eHidden);
	OnZoomOut();
//-
	inherited::OnHiddenItem		();
//.	SetState					(eHidden);
//.	SetNextState				(eHidden);

	m_set_next_ammoType_on_reload = u32(-1);
}

void CWeapon::SendHiddenItem()
{
	if (!CHudItem::object().getDestroy() && m_pInventory)
	{
		// !!! Just single entry for given state !!!
		NET_Packet		P;
		CHudItem::object().u_EventGen		(P,GE_WPN_STATE_CHANGE,CHudItem::object().ID());
		P.w_u8			(u8(eHiding));
		P.w_u8			(u8(m_sub_state));
		P.w_u8			(u8(m_ammoType& 0xff));
		P.w_u8			(u8(iAmmoElapsed & 0xff));
		P.w_u8			(u8(m_set_next_ammoType_on_reload & 0xff));
		CHudItem::object().u_EventSend		(P, net_flags(TRUE, TRUE, FALSE, TRUE));
		SetPending		(TRUE);
	}
}


void CWeapon::OnH_B_Chield		()
{
	m_dwWeaponIndependencyTime = 0;
	inherited::OnH_B_Chield		();

	OnZoomOut					();
	m_set_next_ammoType_on_reload	= u32(-1);
}

extern u32 hud_adj_mode;

void CWeapon::UpdateCL		()
{
	inherited::UpdateCL		();
	UpdateHUDAddonsVisibility();
	UpdateAlterZoomBlend	(Device.fTimeDelta);	// GS alter zoom: eased ramp between the two aim poses
	// world-model attachment bones (scope / reticle illum / laser ray / bayonet / flashlight lens): these
	// toggle at runtime and UpdateAddonsVisibility only fires on addon+upgrade events, so track them here.
	// Guarded by the state signature, so it's a no-op on the frames nothing changed.
	if (IKinematics* wk = smart_cast<IKinematics*>(Visual()))
		gwr_UpdateWorldBones(wk, false);
	// SOMEBODY ELSE picked this weapon up (an NPC): GS kills the mounted light and the laser on it.
	//  - torch (WeaponAdditionalBuffer.pas:1457): `if (GetOwner<>nil) and (GetOwner<>GetActor()) then
	//    SwitchTorch(false)` -- unconditional, and unlike the "in the actor's inventory but not in hands"
	//    case (:1490, which re-sets enabled=true so the light returns when you draw it) the ENABLED FLAG
	//    is really cleared: take the gun back off a corpse and the flashlight is off until you press the key.
	//  - laser (WeaponUpdate.pas:83): same owner test, but gated on the `npc_lasers` console flag (GS ships
	//    it ON in rspec_default/high/extreme, off in low/minimum). With it on the BEAM stays visible on the
	//    NPC's weapon; the projected DOT is actor-active-weapon-only in both engines anyway.
	// A DROPPED weapon (H_Parent()==nullptr) is deliberately not covered -- it keeps shining on the ground.
	{
		// GS tests GetOwner(wpn) against GetActor(), NOT against the current VIEW entity -- and so do we:
		// the flags cleared below are persistent (saved), so a frame where Level().CurrentEntity() happens
		// to be something else must not wipe the player's own laser/flashlight state.
		CObject* wowner = H_Parent();
		CActor*  wactor = Actor();
		if (wowner && wactor && wowner != (CObject*)wactor)
		{
			if (m_bFlashInstalled && (m_bFlashEnabled || m_dwFlashToggleAt))
			{
				m_bFlashEnabled		= false;
				m_dwFlashToggleAt	= 0;
			}
			extern int g_npc_lasers;
			if (!g_npc_lasers && m_bLaserInstalled && (m_bLaserEnabled || m_dwLaserToggleAt))
			{
				m_bLaserEnabled		= false;
				m_dwLaserToggleAt	= 0;
				StopLaserDot		();
			}
		}
	}
	// World-model flashlight. For the ACTOR's own weapon this is driven from CActor::UpdateCL instead (there
	// it runs after g_player_hud->update(), so the hud bone transform is fresh) -- so only handle the weapons
	// the actor isn't holding: dropped ones and the ones in NPC hands, which otherwise never got updated at
	// all and so never lit anything. NOT done for the laser dot: GS restricts that to the actor's own active
	// weapon, so there is nothing to update here for it.
	if (m_bFlashInstalled && H_Parent() != Level().CurrentEntity())
		UpdateFlashlight();
	//подсветка от выстрела
	UpdateLight				();
	// лазерный целеуказатель обновляется из CActor::UpdateCL ПОСЛЕ g_player_hud->update(), когда
	// HUD-трансформ кости уже свежий для этого кадра рендера -> точка не дёргается (не отстаёт на кадр)

	//нарисовать партиклы
	UpdateFlameParticles	();
	UpdateFlameParticles2	();

	if(!IsGameTypeSingle())
		make_Interpolation		();
	
	auto i1 = g_player_hud->attached_item(1);
	if (i1 && HudItemData())
	{
		auto det = smart_cast<CCustomDetector*>(i1->m_parent_hud_item);
		if (det && (det->GetState() == CCustomDetector::eIdle || !det->NeedActivation()))
		{
			if (bAmmotypeKeyPressed || bReloadKeyPressed)
			{
				if (bReloadKeyPressed)
				{
					bReloadKeyPressed = false;
					Action(kWPN_RELOAD, CMD_START);
				}
				else
				{
					bAmmotypeKeyPressed = false;
					Action(kWPN_NEXT, CMD_START);
				}
			}
		}
	}

	if( (GetNextState()==GetState()) && IsGameTypeSingle() && H_Parent()==Level().CurrentEntity())
	{
		CActor* pActor	= smart_cast<CActor*>(H_Parent());
		if(pActor && !pActor->AnyMove() && this==pActor->inventory().ActiveItem())
		{
			if (!m_bDisableBore &&		// GS disable_bore (hud section, default true): skip the idle fidget anim
				hud_adj_mode==0 &&
				GetState()==eIdle &&
				(Device.dwTimeGlobal-m_dw_curr_substate_time>20000) &&
				!IsZoomed()&&
				g_player_hud->attached_item(1)==NULL)
			{
				SwitchState			(eBore);
				ResetSubStateTime	();
			}
		}
	}

	if (!!GetHUDmode()) {
		m_current_inertion.lerp(m_base_inertion, m_zoom_inertion, m_zoom_params.m_fZoomRotationFactor);
	}
}

bool  CWeapon::need_renderable()
{
	return !( IsZoomed() && ZoomTexture() && !IsRotatingToZoom() );
}

void CWeapon::renderable_Render		()
{
	UpdateXForm				();

	//нарисовать подсветку

	RenderLight				();	

	//если мы в режиме снайперки, то сам HUD рисовать не надо
	if(IsZoomed() && !IsRotatingToZoom() && ZoomTexture())
		RenderHud		(FALSE);
	else
		RenderHud		(TRUE);

	inherited::renderable_Render	();
}

void CWeapon::signal_HideComplete()
{
	if(H_Parent()) 
		setVisible			(FALSE);
	SetPending				(FALSE);
}

void CWeapon::SetDefaults()
{
	SetPending			(FALSE);

	m_flags.set			(FUsingCondition, TRUE);
	bMisfire			= false;
	m_bMisfireCooldown	= false;
	m_flagsAddOnState	= 0;
	m_cur_scope			= 0xFF;
	for (int i = 0; i < 16; ++i)	m_scope_illum_step_by_scope[i] = -1;	// per-scope brightness, unset
	m_zoom_params.m_bIsZoomModeNow	= false;
}

void CWeapon::UpdatePosition(const Fmatrix& trans)
{
	Position().set		(trans.c);
	XFORM().mul			(trans,m_strapped_mode ? m_StrapOffset : m_Offset);
	VERIFY				(!fis_zero(DET(renderable.xform)));
}


bool CWeapon::Action(s32 cmd, u32 flags) 
{
	if(inherited::Action(cmd, flags)) return true;

	
	switch(cmd) 
	{
		case kWPN_FIRE: 
			{
				//если оружие чем-то занято, то ничего не делать
				{				
					if(IsPending())		
						return				false;

					if(flags&CMD_START) 
						FireStart			();
					else 
						FireEnd				();
				};
			} 
			return true;
		case kWPN_NEXT: 
			{
				return SwitchAmmoType(flags);
			} 

		case kWPN_ZOOM:
			// Track the physical aim key (down between CMD_START and CMD_STOP), mirroring m_bTriggerHeld
			// for fire. Convenience flag for aim-held logic; set regardless of whether zoom is enabled.
			m_bZoomKeyHeld = !!(flags & CMD_START);
			if(IsZoomEnabled())
			{
				if(b_toggle_weapon_aim)
				{
					if(flags&CMD_START)
					{
						if(!IsZoomed())
						{
							if(!IsPending())
							{
								// aiming stops the current action (incl. firing) and plays the
								// aim-in transition (deferred to switch2_Idle when mid-fire)
								if(GetState()!=eIdle)
									SwitchState(eIdle);
								m_bZoomToggleWanted = false;
								OnZoomIn	();
							}
							else
								// busy (mid-shot on a slow gun, reloading...) -- the press would
								// otherwise be lost, and in toggle mode there is no held key to fall
								// back on. Remember it; CWeaponMagazined::UpdateCL aims when we settle.
								m_bZoomToggleWanted = true;
						}else
						{
							m_bZoomToggleWanted = false;
							OnZoomOut	();
						}
					}
				}else
				{
					if(flags&CMD_START)
					{
						if(!IsZoomed() && !IsPending())
						{
							// aiming stops the current action (incl. firing) and plays the
							// aim-in transition (deferred to switch2_Idle when mid-fire)
							if(GetState()!=eIdle)
								SwitchState(eIdle);
							OnZoomIn	();
						}
					}else
						if(IsZoomed())
							OnZoomOut	();
				}
				return true;
			}else 
				return false;

		case kWPN_ZOOM_INC:
		case kWPN_ZOOM_DEC:
			if(IsZoomEnabled() && IsZoomed())
			{
				// GS variable magnification first: a dual/variable-power optic (ELCAN) steps its LENS power
				// with the wheel. Returns false for fixed-power scopes so the stock dynamic zoom still runs.
				if (ChangeLensStep(cmd==kWPN_ZOOM_INC ? +1 : -1))	return true;
				if(cmd==kWPN_ZOOM_INC)  ZoomInc();
				else					ZoomDec();
				return true;
			}else
				return false;
	}
	return false;
}

bool CWeapon::SwitchAmmoType( u32 flags ) 
{
	if (IsPending() || OnClient())
		return false;

	if (!(flags & CMD_START))
		return false;

	if (bReloadKeyPressed || bAmmotypeKeyPressed)
		return false;

	bAmmotypeKeyPressed = true;

	auto i1 = g_player_hud->attached_item(1);
	if (i1 && HudItemData())
	{
		auto det = smart_cast<CCustomDetector*>(i1->m_parent_hud_item);
		if (det && det->GetState() != CCustomDetector::eIdle)
			return false;
	}

	u32 l_newType = m_ammoType;
	bool b1, b2;
	do 
	{
		l_newType = (l_newType+1) % m_ammoTypes.size();
		b1 = l_newType != m_ammoType;
		b2 = unlimited_ammo() ? false : ( !m_pInventory->GetAny( *m_ammoTypes[l_newType] ) );						
	} while( b1 && b2 );

	if ( l_newType != m_ammoType )
	{
		m_set_next_ammoType_on_reload = l_newType;
		if ( OnServer() )
		{
			Reload();
		}
	}
	return true;
}

void CWeapon::SpawnAmmo(u32 boxCurr, LPCSTR ammoSect, u32 ParentID) 
{
	if(!m_ammoTypes.size())			return;
	if (OnClient())					return;
	m_bAmmoWasSpawned				= true;
	
	int l_type						= 0;
	l_type							%= m_ammoTypes.size();

	if(!ammoSect) ammoSect			= *m_ammoTypes[l_type]; 
	
	++l_type; 
	l_type							%= m_ammoTypes.size();

	CSE_Abstract *D					= F_entity_Create(ammoSect);

	if (D->m_tClassID==CLSID_OBJECT_AMMO	||
		D->m_tClassID==CLSID_OBJECT_A_M209	||
		D->m_tClassID==CLSID_OBJECT_A_VOG25	||
		D->m_tClassID==CLSID_OBJECT_A_OG7B)
	{	
		CSE_ALifeItemAmmo *l_pA		= smart_cast<CSE_ALifeItemAmmo*>(D);
		R_ASSERT					(l_pA);
		l_pA->m_boxSize				= (u16)pSettings->r_s32(ammoSect, "box_size");
		D->s_name					= ammoSect;
		D->set_name_replace			("");
//.		D->s_gameid					= u8(GameID());
		D->s_RP						= 0xff;
		D->ID						= 0xffff;
		if (ParentID == 0xffffffff)	
			D->ID_Parent			= (u16)H_Parent()->ID();
		else
			D->ID_Parent			= (u16)ParentID;

		D->ID_Phantom				= 0xffff;
		D->s_flags.assign			(M_SPAWN_OBJECT_LOCAL);
		D->RespawnTime				= 0;
		l_pA->m_tNodeID				= g_dedicated_server ? u32(-1) : ai_location().level_vertex_id();

		if(boxCurr == 0xffffffff) 	
			boxCurr					= l_pA->m_boxSize;

		while(boxCurr) 
		{
			l_pA->a_elapsed			= (u16)(boxCurr > l_pA->m_boxSize ? l_pA->m_boxSize : boxCurr);
			NET_Packet				P;
			D->Spawn_Write			(P, TRUE);
			Level().Send			(P,net_flags(TRUE));

			if(boxCurr > l_pA->m_boxSize) 
				boxCurr				-= l_pA->m_boxSize;
			else 
				boxCurr				= 0;
		}
	};
	F_entity_Destroy				(D);
}

int CWeapon::GetSuitableAmmoTotal(bool use_item_to_spawn) const
{
	int l_count = iAmmoElapsed;
	if(!m_pInventory) return l_count;

	//чтоб не делать лишних пересчетов
	if(m_pInventory->ModifyFrame()<=m_dwAmmoCurrentCalcFrame)
		return l_count + iAmmoCurrent;

 	m_dwAmmoCurrentCalcFrame = Device.dwFrame;
	iAmmoCurrent = 0;

	for(int i = 0; i < (int)m_ammoTypes.size(); ++i) 
	{
		LPCSTR l_ammoType = *m_ammoTypes[i];

		for(TIItemContainer::iterator l_it = m_pInventory->m_belt.begin(); m_pInventory->m_belt.end() != l_it; ++l_it) 
		{
			CWeaponAmmo *l_pAmmo = smart_cast<CWeaponAmmo*>(*l_it);

			if(l_pAmmo && !xr_strcmp(l_pAmmo->cNameSect(), l_ammoType)) 
			{
				iAmmoCurrent = iAmmoCurrent + l_pAmmo->m_boxCurr;
			}
		}

		for(TIItemContainer::iterator l_it = m_pInventory->m_ruck.begin(); m_pInventory->m_ruck.end() != l_it; ++l_it) 
		{
			CWeaponAmmo *l_pAmmo = smart_cast<CWeaponAmmo*>(*l_it);
			if(l_pAmmo && !xr_strcmp(l_pAmmo->cNameSect(), l_ammoType)) 
			{
				iAmmoCurrent = iAmmoCurrent + l_pAmmo->m_boxCurr;
			}
		}

		if (!use_item_to_spawn)
			continue;

		if (!inventory_owner().item_to_spawn())
			continue;

		iAmmoCurrent += inventory_owner().ammo_in_box_to_spawn();
	}
	return l_count + iAmmoCurrent;
}

int CWeapon::GetCurrentTypeAmmoTotal() const
{
	int l_count = iAmmoElapsed;
	if ( !m_pInventory )
	{
		return l_count;
	}

	//чтоб не делать лишних пересчетов
	if ( m_pInventory->ModifyFrame() <= m_dwAmmoCurrentCalcFrame )
	{
		return l_count + iAmmoCurrent;
	}

	m_dwAmmoCurrentCalcFrame = Device.dwFrame;
	iAmmoCurrent = 0;

	VERIFY( 0 <= m_ammoType && m_ammoType < m_ammoTypes.size() );
	{
		LPCSTR l_ammoType = m_ammoTypes[m_ammoType].c_str();

		for(TIItemContainer::iterator l_it = m_pInventory->m_belt.begin(); m_pInventory->m_belt.end() != l_it; ++l_it) 
		{
			CWeaponAmmo *l_pAmmo = smart_cast<CWeaponAmmo*>(*l_it);

			if(l_pAmmo && !xr_strcmp(l_pAmmo->cNameSect(), l_ammoType)) 
			{
				iAmmoCurrent = iAmmoCurrent + l_pAmmo->m_boxCurr;
			}
		}

		for(TIItemContainer::iterator l_it = m_pInventory->m_ruck.begin(); m_pInventory->m_ruck.end() != l_it; ++l_it) 
		{
			CWeaponAmmo *l_pAmmo = smart_cast<CWeaponAmmo*>(*l_it);
			if(l_pAmmo && !xr_strcmp(l_pAmmo->cNameSect(), l_ammoType)) 
			{
				iAmmoCurrent = iAmmoCurrent + l_pAmmo->m_boxCurr;
			}
		}
	}
	return l_count + iAmmoCurrent;
}

int CWeapon::GetAmmoCountByType(u32 type) const
{
	if (!m_pInventory || type >= m_ammoTypes.size())	return 0;
	int cnt = 0;
	LPCSTR sect = m_ammoTypes[type].c_str();
	for (TIItemContainer::iterator it = m_pInventory->m_belt.begin(); it != m_pInventory->m_belt.end(); ++it)
	{ CWeaponAmmo* a = smart_cast<CWeaponAmmo*>(*it); if (a && !xr_strcmp(a->cNameSect(), sect)) cnt += a->m_boxCurr; }
	for (TIItemContainer::iterator it = m_pInventory->m_ruck.begin(); it != m_pInventory->m_ruck.end(); ++it)
	{ CWeaponAmmo* a = smart_cast<CWeaponAmmo*>(*it); if (a && !xr_strcmp(a->cNameSect(), sect)) cnt += a->m_boxCurr; }
	return cnt;
}

float CWeapon::GetConditionMisfireProbability() const
{
	// Gunslinger model: no jams while condition is above misfireStartCondition; below it, the jam chance ramps
	// linearly from misfireStartProb (at start) to misfireEndProb (at/below misfireEndCondition).
	if (misfireStartCondition > 0.f)
	{
		const float cond = GetCondition();
		if (cond >= misfireStartCondition)	return 0.0f;
		const float span = misfireStartCondition - misfireEndCondition;
		float t = (span > 1e-4f) ? (misfireStartCondition - cond) / span : 1.0f;
		clamp(t, 0.0f, 1.0f);
		float mis = misfireStartProb + t * (misfireEndProb - misfireStartProb);
		clamp(mis, 0.0f, 0.99f);
		return mis;
	}

	// legacy model
	if( GetCondition()>0.95f ) return 0.0f;
	float mis = misfireProbability+powf(1.f-GetCondition(), 3.f)*misfireConditionK;
	clamp(mis,0.0f,0.99f);
	return mis;
}

BOOL CWeapon::CheckForMisfire	()
{
	if (OnClient()) return FALSE;

	// GS (WeaponEvents.pas:824): during a controller suicide the weapon is force-UNjammed and never
	// rolls a misfire -- the scene must not be rescued by a dud round.
	{
		CActor* act = smart_cast<CActor*>(H_Parent());
		if (act && act == Actor() && act->IsSuicideInProgress())
		{
			bMisfire = false;
			return FALSE;
		}
	}

	// the shot right after clearing a jam never jams again -> no back-to-back jams (min 1 clean shot)
	if (m_bMisfireCooldown)
	{
		m_bMisfireCooldown = false;
		return FALSE;
	}

	float rnd = ::Random.randF(0.f,1.f);
	float mp = GetConditionMisfireProbability();
	if(rnd < mp)
	{
		FireEnd();

		bMisfire = true;
		m_bMisfireCooldown = true;	// suppress the very next shot's jam roll
		SwitchState(eMisfire);

		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

BOOL CWeapon::IsMisfire() const
{	
	return bMisfire;
}
void CWeapon::Reload()
{
	OnZoomOut();
}


bool CWeapon::IsGrenadeLauncherAttached() const
{
	return (ALife::eAddonAttachable == m_eGrenadeLauncherStatus &&
			0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher)) || 
			ALife::eAddonPermanent == m_eGrenadeLauncherStatus;
}

bool CWeapon::IsScopeAttached() const
{
	return (ALife::eAddonAttachable == m_eScopeStatus &&
			0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonScope)) || 
			ALife::eAddonPermanent == m_eScopeStatus;

}

int CWeapon::ScopeIndexByItem(LPCSTR item_sect) const
{
	if (!item_sect)	return -1;
	for (u32 i = 0; i < m_scopes.size(); ++i)
		if (pSettings->line_exist(m_scopes[i], "scope_name") &&
			0 == xr_strcmp(pSettings->r_string(m_scopes[i], "scope_name"), item_sect))
			return (int)i;
	return -1;
}

shared_str CWeapon::GetCurrentScopeSection() const
{
	if (m_cur_scope < m_scopes.size())	return m_scopes[m_cur_scope];
	// attached but index unknown (legacy save / alife spawn): match the weapon's single scope_name to a section,
	// else return that scope_name (legacy per-item params) so the single-scope path keeps working.
	if (!m_scopes.empty() && m_sScopeName.size())
	{
		int i = ScopeIndexByItem(*m_sScopeName);
		if (i >= 0)	return m_scopes[i];
	}
	return m_sScopeName;
}

shared_str CWeapon::GetAttachedScopeName() const
{
	// the ATTACHED scope's addon item (its section's scope_name), so detach spawns the right object and the
	// inventory shows the right icon -- not the weapon's default scope_name.
	shared_str sc = GetCurrentScopeSection();
	if (sc.size() && sc != m_sScopeName && pSettings->line_exist(*sc, "scope_name"))
		return pSettings->r_string(*sc, "scope_name");
	return m_sScopeName;
}

bool CWeapon::IsSilencerAttached() const
{
	return (ALife::eAddonAttachable == m_eSilencerStatus &&
			0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonSilencer)) || 
			ALife::eAddonPermanent == m_eSilencerStatus;
}

bool CWeapon::GrenadeLauncherAttachable()
{
	return (ALife::eAddonAttachable == m_eGrenadeLauncherStatus);
}
bool CWeapon::ScopeAttachable()
{
	return (ALife::eAddonAttachable == m_eScopeStatus);
}
bool CWeapon::SilencerAttachable()
{
	return (ALife::eAddonAttachable == m_eSilencerStatus);
}

shared_str wpn_scope				= "wpn_scope";
shared_str wpn_silencer				= "wpn_silencer";
shared_str wpn_grenade_launcher		= "wpn_launcher";




void CWeapon::UpdateHUDAddonsVisibility()
{//actor only
	if(!GetHUDmode())										return;

//.	return;

	if(ScopeAttachable())
	{
		HudItemData()->set_bone_visible(wpn_scope, IsScopeAttached() );
	}

	if(m_eScopeStatus==ALife::eAddonDisabled )
	{
		HudItemData()->set_bone_visible(wpn_scope, FALSE, TRUE );
	}else
		if(m_eScopeStatus==ALife::eAddonPermanent)
			HudItemData()->set_bone_visible(wpn_scope, TRUE, TRUE );

	if(SilencerAttachable())
	{
		HudItemData()->set_bone_visible(wpn_silencer, IsSilencerAttached());
	}
	if(m_eSilencerStatus==ALife::eAddonDisabled )
	{
		HudItemData()->set_bone_visible(wpn_silencer, FALSE, TRUE);
	}
	else
		if(m_eSilencerStatus==ALife::eAddonPermanent)
			HudItemData()->set_bone_visible(wpn_silencer, TRUE, TRUE);

	if(GrenadeLauncherAttachable())
	{
		HudItemData()->set_bone_visible(wpn_grenade_launcher, IsGrenadeLauncherAttached());
	}
	if(m_eGrenadeLauncherStatus==ALife::eAddonDisabled )
	{
		HudItemData()->set_bone_visible(wpn_grenade_launcher, FALSE, TRUE);
	}else
		if(m_eGrenadeLauncherStatus==ALife::eAddonPermanent)
			HudItemData()->set_bone_visible(wpn_grenade_launcher, TRUE, TRUE);

}

// ---- Gunslinger world-model bone visibility (xr_BoneUtils.pas: SetWorldModelBoneStatus) ----
// Silent by design: one config lists bones shared across the hud and world .ogf, which don't carry the
// same set (the world model has no separate mag/rail meshes on some weapons), so a missing bone is a
// skip, not an assert -- exactly how the hud path treats it.
void CWeapon::gwr_SetWorldBone(IKinematics* K, LPCSTR bone, BOOL show)
{
	if (!K || !bone || !bone[0])	return;
	u16 bid = K->LL_BoneID(bone);
	if (bid == BI_NONE)				return;
	if (K->LL_GetBoneVisible(bid) != show)
		K->LL_SetBoneVisible(bid, show, TRUE);
}

// Comma-separated list -> world model. Mirrors CWeaponMagazined::gwr_SetBones' parsing.
void CWeapon::gwr_SetWorldBonesCSV(IKinematics* K, LPCSTR csv, BOOL show)
{
	if (!K || !csv || !csv[0])	return;
	string256 name;
	LPCSTR p = csv;
	while (*p)
	{
		while (*p == ' ' || *p == ',')	++p;			// skip separators/space
		LPCSTR s = p;
		while (*p && *p != ',')			++p;
		u32 n = (u32)(p - s);
		while (n && s[n-1] == ' ')		--n;			// trim trailing space
		if (n && n < sizeof(name))
		{
			strncpy_s(name, sizeof(name), s, n);  name[n] = 0;
			gwr_SetWorldBone(K, name, show);
		}
	}
}

// Static + per-upgrade bone lists applied to the WORLD visual. Same sources and same order as the hud
// path (CWeaponMagazined::gwr_UpdateBones): def_hide_bones then def_show_bones, then each installed
// upgrade in install order, reading BOTH its effect section (the `section` key, where GS keeps them)
// and the node itself (older CS style); hide first, then show, so show wins on overlap.
// Split a comma-separated bone list into `out` (appends, de-duplicated). Same parsing rules as
// gwr_SetBones, kept separate so callers can reason about a list without touching the models.
void CWeapon::gwr_CollectBoneNames(LPCSTR csv, xr_vector<shared_str>& out)
{
	if (!csv || !csv[0])	return;
	string256 name;
	LPCSTR p = csv;
	while (*p)
	{
		while (*p == ' ' || *p == ',')	++p;
		LPCSTR s = p;
		while (*p && *p != ',')			++p;
		u32 n = (u32)(p - s);
		while (n && s[n-1] == ' ')		--n;
		if (n && n < sizeof(name))
		{
			strncpy_s(name, sizeof(name), s, n);  name[n] = 0;
			shared_str b = name;
			if (std::find(out.begin(), out.end(), b) == out.end())	out.push_back(b);
		}
	}
}

void CWeapon::gwr_UpdateWorldBones(IKinematics* K, bool force)
{
	if (!K)	return;

	// Attachment-state signature: everything below that can change at runtime. The per-frame caller only
	// does the work when one of these flipped -- an upgrade install goes through the forced path instead.
	shared_str cur_scope = GetCurrentScopeSection();
	u32 sig =  (IsScopeAttached()						? 1u : 0u)
			| ((ScopeIllumValue() > 0.f)				? 2u : 0u)
			| ((m_bLaserInstalled && m_bLaserEnabled)	? 4u : 0u)
			| ((m_bFlashInstalled && m_bFlashEnabled)	? 8u : 0u)
			| (IsBayonetActive()						? 16u : 0u)
			| (IsGrenadeLauncherAttached()				? 32u : 0u)		// def_hide_bones_override_when_gl_attached
			| (u32(cur_scope.size() ? cur_scope._get()->dwCRC : 0) << 6);
	if (!force && sig == m_gwr_world_bones_sig)	return;
	m_gwr_world_bones_sig = sig;

	const shared_str& wsect = cNameSect();
	if (pSettings->line_exist(wsect, "def_hide_bones"))	gwr_SetWorldBonesCSV(K, pSettings->r_string(wsect, "def_hide_bones"), FALSE);
	if (pSettings->line_exist(wsect, "def_show_bones"))	gwr_SetWorldBonesCSV(K, pSettings->r_string(wsect, "def_show_bones"), TRUE);

	// Mirrors the hud path: remember which bones an upgrade NAMED, so the recursive show_bones collateral
	// can be undone below (set_bone_visible recurses, so revealing a parent reveals its whole subtree).
	xr_vector<shared_str> shown;
	if (pSettings->line_exist(wsect, "def_show_bones"))
		gwr_CollectBoneNames(pSettings->r_string(wsect, "def_show_bones"), shown);
	for (const shared_str& up : m_upgrades)
	{
		if (!up.size())	continue;
		shared_str esect = pSettings->line_exist(up, "section") ? (shared_str)pSettings->r_string(up, "section") : up;
		const shared_str srcs[2] = { esect, up };
		for (const shared_str& s : srcs)
		{
			if (!s.size())	continue;
			if (pSettings->line_exist(s, "hide_bones"))				gwr_SetWorldBonesCSV(K, pSettings->r_string(s, "hide_bones"), FALSE);
			if (pSettings->line_exist(s, "hide_bones_override"))	gwr_SetWorldBonesCSV(K, pSettings->r_string(s, "hide_bones_override"), FALSE);
			if (pSettings->line_exist(s, "show_bones"))
			{
				LPCSTR csv = pSettings->r_string(s, "show_bones");
				gwr_SetWorldBonesCSV(K, csv, TRUE);
				gwr_CollectBoneNames(csv, shown);
			}
		}
	}

	// addon-conditional OVERRIDES, a separate pass AFTER the loop like the hud path and like GS
	// (inline they would be undone by the same upgrade's show_bones)
	for (const shared_str& up : m_upgrades)
	{
		if (!up.size())	continue;
		shared_str esect = pSettings->line_exist(up, "section") ? (shared_str)pSettings->r_string(up, "section") : up;
		const shared_str srcs[2] = { esect, up };
		for (const shared_str& s : srcs)
		{
			if (!s.size())	continue;
			if (IsSilencerAttached() && pSettings->line_exist(s, "hide_bones_override_when_silencer_attached"))
				gwr_SetWorldBonesCSV(K, pSettings->r_string(s, "hide_bones_override_when_silencer_attached"), FALSE);
			if (IsScopeAttached() && pSettings->line_exist(s, "hide_bones_override_when_scope_attached"))
				gwr_SetWorldBonesCSV(K, pSettings->r_string(s, "hide_bones_override_when_scope_attached"), FALSE);
			if ((m_eGrenadeLauncherStatus == ALife::eAddonPermanent || IsGrenadeLauncherAttached())
				&& pSettings->line_exist(s, "hide_bones_override_when_gl_attached"))
				gwr_SetWorldBonesCSV(K, pSettings->r_string(s, "hide_bones_override_when_gl_attached"), FALSE);
		}
	}

	// re-hide every def_hide_bones entry nobody named (undo the recursive collateral)
	if (pSettings->line_exist(wsect, "def_hide_bones"))
	{
		xr_vector<shared_str> hide_list;
		gwr_CollectBoneNames(pSettings->r_string(wsect, "def_hide_bones"), hide_list);
		for (const shared_str& b : hide_list)
			if (std::find(shown.begin(), shown.end(), b) == shown.end())
				gwr_SetWorldBonesCSV(K, *b, FALSE);
	}

	// ---- attachment state, same passes the hud path runs after the upgrade loop ----
	// parts the launcher replaces -- same pass as the hud path (GS def_hide_bones_override_when_gl_attached)
	if ((m_eGrenadeLauncherStatus == ALife::eAddonPermanent || IsGrenadeLauncherAttached())
		&& pSettings->line_exist(wsect, "def_hide_bones_override_when_gl_attached"))
		gwr_SetWorldBonesCSV(K, pSettings->r_string(wsect, "def_hide_bones_override_when_gl_attached"), FALSE);

	// def_hide_bones above hides EVERY optional scope bone, so without this the world model never shows the
	// mounted optic: re-show the ATTACHED scope's own `bones` (per-scope section, weapon scope_bones fallback).
	// Hide EVERY other listed scope's bones first: a rail upgrade that names the mounts in its own show_bones
	// (winchester/protecta: `show_bones = rail, scope1..4`) would otherwise leave all four optics on the model.
	for (const shared_str& sc : m_scopes)
		if (sc.size() && sc != cur_scope && pSettings->line_exist(*sc, "bones"))
			gwr_SetWorldBonesCSV(K, pSettings->r_string(*sc, "bones"), FALSE);

	LPCSTR scope_bones = (cur_scope.size() && pSettings->line_exist(*cur_scope, "bones"))
							? pSettings->r_string(*cur_scope, "bones")
							: (pSettings->line_exist(wsect, "scope_bones") ? pSettings->r_string(wsect, "scope_bones") : nullptr);
	if (scope_bones)
		gwr_SetWorldBonesCSV(K, scope_bones, IsScopeAttached() ? TRUE : FALSE);
	else if (cur_scope.size() && !IsScopeAttached() && pSettings->line_exist(*cur_scope, "bones"))
		gwr_SetWorldBonesCSV(K, pSettings->r_string(*cur_scope, "bones"), FALSE);


	// what the mount does to the rest of the weapon while this optic is on (GS per-scope keys) --
	// same pass as the hud path in CWeaponMagazined::gwr_UpdateBones
	if (cur_scope.size() && IsScopeAttached())
	{
		if (pSettings->line_exist(*cur_scope, "overriding_hide_bones"))
			gwr_SetWorldBonesCSV(K, pSettings->r_string(*cur_scope, "overriding_hide_bones"), FALSE);
		if (pSettings->line_exist(*cur_scope, "overriding_show_bones"))
			gwr_SetWorldBonesCSV(K, pSettings->r_string(*cur_scope, "overriding_show_bones"), TRUE);
	}

	// reticle illumination bones: only while a scope is on AND brightness > 0
	LPCSTR illum_bones = (cur_scope.size() && pSettings->line_exist(*cur_scope, "scope_illum_bones"))
							? pSettings->r_string(*cur_scope, "scope_illum_bones")
							: (pSettings->line_exist(wsect, "scope_illum_bones") ? pSettings->r_string(wsect, "scope_illum_bones") : nullptr);
	if (illum_bones)
		gwr_SetWorldBonesCSV(K, illum_bones, (IsScopeAttached() && ScopeIllumValue() > 0.f) ? TRUE : FALSE);

	// laser ray bone follows the toggle (symmetric -- a PERMANENT designator has no upgrade show_bones to
	// bring it back, see the matching block in CWeaponMagazined::UpdateHUDAddonsVisibility)
	if (m_bLaserInstalled && m_sLaserBone.size())
		gwr_SetWorldBonesCSV(K, m_sLaserRayBones.size() ? m_sLaserRayBones.c_str() : m_sLaserBone.c_str(),
							 m_bLaserEnabled ? TRUE : FALSE);

	// bayonet blade removed when a silencer/GL occupies the barrel
	if (m_bBayonetInstalled && !IsBayonetActive())
		gwr_SetWorldBonesCSV(K, m_sBayonetBone.size() ? m_sBayonetBone.c_str() : "knife", FALSE);

	// flashlight glow lens follows the on/off toggle (the device itself stays shown by show_bones)
	if (m_bFlashInstalled)
	{
		LPCSTR fb = pSettings->line_exist(wsect, "flashlight_bone") ? pSettings->r_string(wsect, "flashlight_bone") : "flash";
		gwr_SetWorldBonesCSV(K, fb, m_bFlashEnabled ? TRUE : FALSE);
	}
}

// Per-scope inventory-icon offset (GS keeps scope_x/scope_y inside each [scope_*_<wpn>] section, since
// every optic lands on a different part of the icon). Falls back to the weapon-level value loaded at
// Load() when the active scope section doesn't define one, so single-scope weapons are unchanged.
// HQ icons double the offset, matching how m_iScopeX/Y are read.
int CWeapon::GetScopeX()
{
	shared_str s = GetCurrentScopeSection();
	if (s.size() && pSettings->line_exist(*s, "scope_x"))
		return pSettings->r_s32(*s, "scope_x") * (GameConstants::GetUseHQ_Icons() ? 2 : 1);
	return m_iScopeX;
}

int CWeapon::GetScopeY()
{
	shared_str s = GetCurrentScopeSection();
	if (s.size() && pSettings->line_exist(*s, "scope_y"))
		return pSettings->r_s32(*s, "scope_y") * (GameConstants::GetUseHQ_Icons() ? 2 : 1);
	return m_iScopeY;
}

void CWeapon::UpdateAddonsVisibility()
{
	IKinematics* pWeaponVisual = smart_cast<IKinematics*>(Visual()); R_ASSERT(pWeaponVisual);

	u16  bone_id;
	UpdateHUDAddonsVisibility								();

	pWeaponVisual->CalculateBones_Invalidate				();

	// GS bone lists FIRST, so the stock addon logic below (scope/silencer/GL) still has the final say
	// on wpn_scope/wpn_silencer/wpn_launcher -- same ordering the hud path uses.
	gwr_UpdateWorldBones									(pWeaponVisual);

	bone_id = pWeaponVisual->LL_BoneID					(wpn_scope);
	if(ScopeAttachable())
	{
		if(IsScopeAttached())
		{
			if(!pWeaponVisual->LL_GetBoneVisible		(bone_id))
			pWeaponVisual->LL_SetBoneVisible				(bone_id,TRUE,TRUE);
		}else{
			if(pWeaponVisual->LL_GetBoneVisible				(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
		}
	}
	if(m_eScopeStatus==ALife::eAddonDisabled && bone_id!=BI_NONE && 
		pWeaponVisual->LL_GetBoneVisible(bone_id) )
	{
		pWeaponVisual->LL_SetBoneVisible					(bone_id,FALSE,TRUE);
//		Log("scope", pWeaponVisual->LL_GetBoneVisible		(bone_id));
	}
	bone_id = pWeaponVisual->LL_BoneID						(wpn_silencer);
	if(SilencerAttachable())
	{
		if(IsSilencerAttached()){
			if(!pWeaponVisual->LL_GetBoneVisible		(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,TRUE,TRUE);
		}else{
			if( pWeaponVisual->LL_GetBoneVisible			(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
		}
	}
	if(m_eSilencerStatus==ALife::eAddonDisabled && bone_id!=BI_NONE && 
		pWeaponVisual->LL_GetBoneVisible(bone_id) )
	{
		pWeaponVisual->LL_SetBoneVisible					(bone_id,FALSE,TRUE);
//		Log("silencer", pWeaponVisual->LL_GetBoneVisible	(bone_id));
	}

	bone_id = pWeaponVisual->LL_BoneID						(wpn_grenade_launcher);
	if(GrenadeLauncherAttachable())
	{
		if(IsGrenadeLauncherAttached())
		{
			if(!pWeaponVisual->LL_GetBoneVisible		(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,TRUE,TRUE);
		}else{
			if(pWeaponVisual->LL_GetBoneVisible				(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
		}
	}
	if(m_eGrenadeLauncherStatus==ALife::eAddonDisabled && bone_id!=BI_NONE && 
		pWeaponVisual->LL_GetBoneVisible(bone_id) )
	{
		pWeaponVisual->LL_SetBoneVisible					(bone_id,FALSE,TRUE);
//		Log("gl", pWeaponVisual->LL_GetBoneVisible			(bone_id));
	}
	

	pWeaponVisual->CalculateBones_Invalidate				();
	pWeaponVisual->CalculateBones							(TRUE);
}


void CWeapon::InitAddons()
{
}

float CWeapon::CurrentZoomFactor()
{
	return IsScopeAttached() ? m_zoom_params.m_fScopeZoomFactor : m_zoom_params.m_fIronSightZoomFactor;
};


void CWeapon::OnZoomIn()
{
	// GS CanAimNow (WeaponAdditionalBuffer.pas:900): aiming is refused outright for the whole scene.
	// We used to only force an unzoom on the next frame, so the sight still flicked up on every press.
	if (SuicideBlocksAim())				return;

	m_zoom_params.m_bIsZoomModeNow		= true;
	m_zoom_params.m_fCurrentZoomFactor	= CurrentZoomFactor();
	// GS IsLastZoomAlter (collimator.pas:132): if the previous aim ENDED in the alter pose, come back
	// straight into it instead of the normal one. The blend factor is set to 1 rather than ramped --
	// this is a continuation of the pose the player left, not a fresh toggle, so it must not play the
	// switch transition on every re-aim.
	if (m_bAlterZoomLast && IsAlterZoomAllowed())
	{
		m_bAlterZoom		= true;
		m_fAlterZoomFactor	= 1.f;
	}

	RefreshZoomDOF						();
}

// GS ReadLensDOFVector (ActorDOF.pas:353): the hud section carries the weapon's lens DOF, and the
// section of the scope ACTUALLY ATTACHED gets the last word -- different optics focus differently,
// and the scope can be swapped between aims, so this is resolved here rather than at Load.
Fvector CWeapon::LensDof() const
{
	Fvector v = m_zoom_params.m_LensDof;
	shared_str sc = GetCurrentScopeSection();
	if (sc.size() && pSettings->section_exist(sc))
	{
		v.x = READ_IF_EXISTS(pSettings, r_float, *sc, "lens_dof_near",  v.x);
		v.y = READ_IF_EXISTS(pSettings, r_float, *sc, "lens_dof_focus", v.y);
		v.z = READ_IF_EXISTS(pSettings, r_float, *sc, "lens_dof_far",   v.z);
	}
	return v;
}

// GS RefreshZoomDOF (ActorDOF.pas:96). Which DOF an aim applies depends on what you are looking
// through, and the LENS branch comes first, exactly as in GS:
//   * PiP scope, normal pose  -> lens_dof_*: the lens image stays sharp, the world around it blurs
//   * no scope (iron sights)  -> the ordinary zoom_dof_*
//   * plain 2D scope / alter pose (lens faded out) -> nothing, so whatever is applied eases back out
// Called on aim-in and whenever the answer can change mid-aim (the alter-pose toggle turns the lens
// off and on, see [alter zoom]).
void CWeapon::RefreshZoomDOF()
{
	if (!IsZoomed())	return;

	if (IsLensedScope() && !m_bAlterZoom)
	{
		GamePersistent().SetEffectorDOF	(LensDof(), m_zoom_params.m_fZoomInDofSpeed);
		return;
	}
	if (m_zoom_params.m_bZoomDofEnabled && !IsScopeAttached())
	{
		GamePersistent().SetEffectorDOF	(m_zoom_params.m_ZoomDof, m_zoom_params.m_fZoomInDofSpeed);
		return;
	}
	// no-op when nothing was applied (RestoreEffectorDOF checks m_dof_changed)
	GamePersistent().RestoreEffectorDOF	(m_zoom_params.m_fZoomOutDofSpeed);
}

void CWeapon::OnZoomOut()
{
	m_zoom_params.m_bIsZoomModeNow		= false;
	m_zoom_params.m_fCurrentZoomFactor	= g_fov;
	m_bZoomToggleWanted					= false;	// leaving aim cancels any queued toggle aim-in
	// GS SetLastZoomAlter: remember which of the two aim poses the player left, so the next aim returns
	// to it (only while the scope still offers one -- see OnZoomIn).
	if (IsAlterZoomAllowed())			m_bAlterZoomLast = m_bAlterZoom;
	m_bAlterZoom						= false;	// GS: the second aim pose ends with the aim itself
	// leaving aim: forget any "sprint already entered" state (it can be stale-true through the aim-out
	// transition, which owns the idle slot). So sprinting straight out of aim always plays the enter anim
	// (anm_idle_sprint_start) instead of snapping into the loop. Pairs with the m_bPrevSprint edge check.
	m_bSprintStarted					= false;
	m_bPrevSprint						= false;

	// GS WeaponEvents.pas:1895 -- leaving aim eases the DOF back out over its OWN (slower) speed,
	// which is most of what made vanilla's flat 0.2s in/out feel wrong next to GS.
	GamePersistent().RestoreEffectorDOF	(m_zoom_params.m_fZoomOutDofSpeed);
	ResetSubStateTime					();
}

CUIWindow* CWeapon::ZoomTexture()
{
	if (UseScopeTexture())
		return m_UIScope;
	else
		return NULL;
}

// A lensed (3D PiP) scope must NOT use the vanilla 2D scope texture -- returning false here cascades:
// ZoomTexture()->NULL, so need_renderable() keeps the weapon visible, RenderHud stays on, and
// render_item_ui_query() stops drawing the full-screen 2D scope. The 3D lens ($user$scope) takes over.
// A collimator rides the same cascade to stay visible, but without any lens (see IsCollimatorScope).
bool CWeapon::UseScopeTexture()
{
	return !IsLensedScope() && !IsCollimatorScope();
}

// GS IsLensedScopeInstalled: the currently-attached scope (its addon section) is flagged need_lens_frame.
// Fallback to the weapon section (for a permanent/built-in lensed optic). Only true while a scope is on.
bool CWeapon::IsLensedScope() const
{
	if (!IsScopeAttached())	return false;
	if (IsGrenadeMode())	return false;	// aiming the GL ladder sight -- the scope zoom/lens must NOT apply
	shared_str sc = GetCurrentScopeSection();
	if (sc.size() && pSettings->line_exist(*sc, "need_lens_frame"))
		return !!pSettings->r_bool(*sc, "need_lens_frame");
	return READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "need_lens_frame", FALSE);
}

// GS `collimator`: a red-dot sight. Its reticle is part of the weapon MODEL, so the vanilla 2D scope path
// (which hides the weapon behind a full-screen picture) is wrong, and so is the PiP lens (nothing to
// magnify at 1x). This is the third mode: weapon + HUD stay visible, no lens, no world zoom.
bool CWeapon::IsCollimatorScope() const
{
	if (!IsScopeAttached())	return false;
	if (IsGrenadeMode())	return false;	// aiming the GL ladder sight -- the scope must not apply
	shared_str sc = GetCurrentScopeSection();
	if (sc.size() && pSettings->line_exist(*sc, "collimator"))
		return !!pSettings->r_bool(*sc, "collimator");
	return READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "collimator", FALSE);
}

// GS GetLensFOV: the FOV (degrees) to render the world at for the scope lens frame -- the base world FOV
// narrowed by the scope magnification (scope_lens_factor). fov_lens = 2*atan(tan(base/2)/factor). Read the
// factor from the attached scope's addon section first, else the weapon section (default 2.0). 0 = disabled.
float CWeapon::GetLensFOV() const
{
	extern float g_fov;
	float factor = 2.0f;
	shared_str sc = GetCurrentScopeSection();
	// GS variable magnification: a scope with lens_factor_levels_count steps between min_lens_factor and
	// max_lens_factor (ELCAN 1.8x <-> 10x, switched with the wheel). Fixed scope_lens_factor otherwise.
	if (m_lens_steps > 0)
	{
		int step = m_lens_step;
		clamp(step, 0, m_lens_steps);
		factor = m_lens_min + (m_lens_max - m_lens_min) * (float(step) / float(m_lens_steps));
	}
	else if (sc.size() && pSettings->line_exist(*sc, "scope_lens_factor"))
		factor = pSettings->r_float(*sc, "scope_lens_factor");
	else
		factor = READ_IF_EXISTS(pSettings, r_float, cNameSect(), "scope_lens_factor", 2.0f);
	if (factor < 1.01f)		factor = 1.01f;
	float half = deg2rad(g_fov) * 0.5f;
	return rad2deg(2.0f * atanf(tanf(half) / factor));
}

// GS variable magnification (min_lens_factor / max_lens_factor / lens_factor_levels_count on the active
// scope section). levels_count is the number of steps ABOVE the base, so `1` = a two-position optic.
// A scope that only sets the fixed scope_lens_factor gets m_lens_steps = 0 and behaves as before.
void CWeapon::LoadLensFactorParams()
{
	m_lens_steps = 0;
	m_lens_min = m_lens_max = 0.f;
	shared_str sc = GetCurrentScopeSection();
	// A PERMANENT optic (scope_status = 1, e.g. the gauss) has no scope section of its own; GS keeps its whole
	// lens block on the weapon section, so fall back there -- same order IsLensedScope/GetLensFOV already use.
	LPCSTR lsect = NULL;
	if (sc.size() && pSettings->line_exist(*sc, "lens_factor_levels_count"))			lsect = *sc;
	else if (pSettings->line_exist(cNameSect(), "lens_factor_levels_count"))			lsect = *cNameSect();
	if (!lsect)	return;

	m_lens_steps = (int)pSettings->r_u32(lsect, "lens_factor_levels_count");
	if (m_lens_steps < 1)	{ m_lens_steps = 0; return; }
	m_lens_min = READ_IF_EXISTS(pSettings, r_float, lsect, "min_lens_factor", 2.0f);
	m_lens_max = READ_IF_EXISTS(pSettings, r_float, lsect, "max_lens_factor", m_lens_min);
	if (m_lens_max < m_lens_min)	std::swap(m_lens_min, m_lens_max);
	clamp(m_lens_step, 0, m_lens_steps);
}

void CWeapon::ResetLensStepToDefault()
{
	const int idx = (m_cur_scope < 16) ? (int)m_cur_scope : 0;
	if (m_lens_step_by_scope[idx] >= 0)
		m_lens_step = m_lens_step_by_scope[idx];
	else
	{
		shared_str sc = GetCurrentScopeSection();
		m_lens_step = sc.size() ? (int)READ_IF_EXISTS(pSettings, r_u32, *sc, "default_lens_factor_step", 0) : 0;
	}
	LoadLensFactorParams();
}

bool CWeapon::ChangeLensStep(int delta)
{
	if (!IsScopeAttached() || !IsLensedScope())	return false;
	LoadLensFactorParams();						// the scope may have changed since the last call
	if (m_lens_steps <= 0)						return false;	// fixed-power optic -> not ours to handle

	const int prev = m_lens_step;
	m_lens_step += delta;
	clamp(m_lens_step, 0, m_lens_steps);
	const int idx = (m_cur_scope < 16) ? (int)m_cur_scope : 0;
	m_lens_step_by_scope[idx] = m_lens_step;
	// GS gates the detent on `lens_params.factor_min <> lens_params.factor_max` (WeaponEvents.pas:1679):
	// a scope whose min and max magnification are the SAME cannot actually zoom, so turning the wheel on
	// it must be silent even though it still declares lens_factor_levels_count. The PO 4x34 is exactly
	// that (min = max = 6 with 5 levels).
	if (m_lens_step != prev && !fsimilar(m_lens_min, m_lens_max))
	{
		// reuse the scope-brightness click: GS plays a mechanical detent for the magnifier lever too
		if (m_sounds.FindSoundItem("sndScopeBrightPlus", false) && delta > 0)
			PlaySound("sndScopeBrightPlus", get_LastFP());
		else if (m_sounds.FindSoundItem("sndScopeBrightMinus", false) && delta < 0)
			PlaySound("sndScopeBrightMinus", get_LastFP());
	}
	return true;								// consumed, even at the end of the range
}

// GS CanStartAimNow's grenade-mode clause (WeaponAdditionalBuffer.pas:919): while the launcher is raised,
// `prohibit_aim_for_grenade_mode` forbids aiming. GS reads it from the CURRENT SCOPE section when a scope
// is attached and from the hud section otherwise -- so a weapon can allow iron-sight GL aiming and still
// refuse it through a mounted optic (the Groza's 5 optics all set it).
bool CWeapon::AimProhibitedByGrenadeMode() const
{
	if (!IsGrenadeMode())	return false;
	shared_str sect = IsScopeAttached() ? GetCurrentScopeSection() : HudSection();
	if (!sect.size() || !pSettings->section_exist(*sect))	return false;
	return !!READ_IF_EXISTS(pSettings, r_bool, *sect, "prohibit_aim_for_grenade_mode", FALSE);
}

// GS alter_zoom_allowed: the active scope offers a SECOND aim pose (the ELCAN magnifier -- the eye moves
// to the other optic), toggled while aiming. Its own aim offset / hud fov are read where those are applied.
shared_str CWeapon::AlterZoomSection() const
{
	if (IsScopeAttached())
	{
		shared_str sc = GetCurrentScopeSection();
		if (sc.size())	return sc;
	}
	return HudSection();
}

bool CWeapon::IsAlterZoomAllowed() const
{
	shared_str sc = AlterZoomSection();
	return sc.size() && pSettings->section_exist(*sc)
		&& !!READ_IF_EXISTS(pSettings, r_bool, *sc, "alter_zoom_allowed", FALSE);
}

void CWeapon::ToggleAlterZoom()
{
	if (!IsAlterZoomAllowed() || !IsZoomed())	{ m_bAlterZoom = false; return; }
	m_bAlterZoom = !m_bAlterZoom;
	// the alter pose cross-fades the PiP lens away, so the lens DOF has to go with it (and come back)
	RefreshZoomDOF();
}

// Ramp m_fAlterZoomFactor toward the target over `alter_zoom_time` seconds (scope section, else the
// weapon's zoom_rotate_time, else 0.25). GS eases this transition instead of snapping between the two
// eye positions.
void CWeapon::UpdateAlterZoomBlend(float dt)
{
	const float target = (m_bAlterZoom && IsZoomed()) ? 1.f : 0.f;
	if (fsimilar(m_fAlterZoomFactor, target))	{ m_fAlterZoomFactor = target; return; }

	shared_str sc = AlterZoomSection();
	float t = (sc.size() && pSettings->section_exist(*sc))
			? READ_IF_EXISTS(pSettings, r_float, *sc, "alter_zoom_time", 0.f) : 0.f;
	if (t <= EPS)	t = m_zoom_params.m_fZoomRotateTime;
	if (t <= EPS)	t = 0.25f;

	const float step = dt / t;
	if (target > m_fAlterZoomFactor)	m_fAlterZoomFactor = _min(1.f, m_fAlterZoomFactor + step);
	else								m_fAlterZoomFactor = _max(0.f, m_fAlterZoomFactor - step);
}

// Cubic ease-in-out == cubic-bezier(0.42, 0, 0.58, 1): slow at both ends, fastest in the middle.
float CWeapon::AlterZoomBlend() const
{
	float t = m_fAlterZoomFactor;
	clamp(t, 0.f, 1.f);
	if (t <= 0.f)	return 0.f;
	if (t >= 1.f)	return 1.f;
	return (t < 0.5f)
			? (4.f * t * t * t)
			: (1.f - powf(-2.f * t + 2.f, 3.f) * 0.5f);
}

// Gunslinger use_scope_anims: while a scope is attached the weapon plays the "_scope" animation variants
// (anm_shoot_aim_scope, anm_idle_aim_scope, ...). Condition mirrors WeaponAnims.pas: scope attached AND the
// HUD section's aim_scope_anims (default true) AND the scope section's use_scope_anims (default false). We
// read use_scope_anims from the attached scope addon section first, else the weapon section.
bool CWeapon::UseScopeAnims() const
{
	if (!IsScopeAttached())	return false;
	shared_str hud = READ_IF_EXISTS(pSettings, r_string, cNameSect(), "hud", 0);
	if (hud.size() && !READ_IF_EXISTS(pSettings, r_bool, *hud, "aim_scope_anims", TRUE))
		return false;
	shared_str sc = GetCurrentScopeSection();
	if (sc.size() && pSettings->line_exist(*sc, "use_scope_anims"))
		return !!pSettings->r_bool(*sc, "use_scope_anims");
	return READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "use_scope_anims", FALSE);
}

// Gunslinger zoom_mouse_sense_koef: look-sensitivity multiplier while aiming through this scope (default 1.0,
// set lower for higher magnification -- GS PSO x10 = 0.2). Read from the attached scope addon section first,
// else the weapon section. Applied only for a PiP lensed scope (whose world FOV stays wide, so the engine's
// f_fov/g_fov reduction doesn't kick in); 2D-scope weapons already slow down via the FOV.
float CWeapon::ZoomMouseSenseKoef() const
{
	// GS (ActorUtils.pas CActor__IR_OnMouseMove_CorrectMouseSense): the SCOPE's koef only applies while
	// looking through the scope itself -- `and not IsAlterZoom(wpn)`. In the alter pose the eye is on the
	// backup 1x sight, so the slowdown that matches the magnification must not apply; fall back to the
	// weapon's own value (1.0 by default).
	shared_str sc = GetCurrentScopeSection();
	if (!IsAlterZoom() && sc.size() && pSettings->line_exist(*sc, "zoom_mouse_sense_koef"))
		return ScaleSenseByLensStep(pSettings->r_float(*sc, "zoom_mouse_sense_koef"));
	return ScaleSenseByLensStep(READ_IF_EXISTS(pSettings, r_float, cNameSect(), "zoom_mouse_sense_koef", 1.0f));
}

// A VARIABLE-magnification optic (min_lens_factor != max_lens_factor with lens_factor_levels_count steps --
// the ELCAN 1.8x <-> 10x) must slow the look down FURTHER as the magnifier steps up: the configured
// zoom_mouse_sense_koef belongs to the scope's BASE power, and sensitivity scales as 1/magnification, so the
// koef is multiplied by min/current. Fixed-power scopes (m_lens_steps == 0, or min == max like the PO 4x34)
// and the alter pose are untouched. `zoom_mouse_sense_lens_power` on the scope section softens the curve:
// 1.0 = fully proportional (default), 0 = the old behaviour.
float CWeapon::ScaleSenseByLensStep(float k) const
{
	if (m_lens_steps <= 0 || fsimilar(m_lens_min, m_lens_max) || m_lens_min <= 0.f)	return k;
	if (IsAlterZoom())																return k;
	int step = m_lens_step;
	clamp(step, 0, m_lens_steps);
	const float cur = m_lens_min + (m_lens_max - m_lens_min) * (float(step) / float(m_lens_steps));
	if (cur <= m_lens_min)															return k;
	shared_str sc = GetCurrentScopeSection();
	float p = 1.0f;
	if (sc.size() && pSettings->line_exist(*sc, "zoom_mouse_sense_lens_power"))
		p = pSettings->r_float(*sc, "zoom_mouse_sense_lens_power");
	if (p <= 0.f)																	return k;
	return k * powf(m_lens_min / cur, p);
}

// GS collimator.pas GetZoomLensVisibilityFactor + LensConditions: with a lensed scope installed the PiP
// lens is rendered only while the eye is BEHIND it -- switching to the alter pose (the ELCAN's backup 1x
// sight) turns the lens off, and the direct switch cross-fades it instead of cutting. Our alter-pose ramp
// (AlterZoomBlend, 0 = scope, 1 = alter) is exactly GS's mixup factor, so the visibility is its inverse.
// 0 => the lens quad is discarded (shader alpha = min(aim, this)) and the $user$scope capture is skipped.
float CWeapon::LensVisibility() const
{
	if (!IsLensedScope())	return 0.f;
	return 1.f - AlterZoomBlend();
}

// Gunslinger scope illumination (LoadNightBrightnessParamsFromSection): read the stepped brightness params
// from the attached scope addon section (else the weapon section), clamp the current step, recompute the
// value. GS divides the config brightness by 3.
void CWeapon::LoadScopeIllumParams()
{
	shared_str sc = GetCurrentScopeSection();
	LPCSTR sect = (sc.size() && pSettings->line_exist(*sc, "steps_brightness"))
					? *sc : cNameSect().c_str();
	m_scope_illum_max	= READ_IF_EXISTS(pSettings, r_float, sect, "max_night_brightness", 1.f) / 3.f;
	m_scope_illum_min	= READ_IF_EXISTS(pSettings, r_float, sect, "min_night_brightness", 1.f) / 3.f;
	m_scope_illum_steps	= READ_IF_EXISTS(pSettings, r_u32,   sect, "steps_brightness",    0);
	m_scope_illum_jitter= READ_IF_EXISTS(pSettings, r_float, sect, "jitter_brightness",   0.f);
	clamp(m_scope_illum_step, 0, m_scope_illum_steps);
	const int denom = (m_scope_illum_steps > 0) ? m_scope_illum_steps : 1;
	m_scope_illum_value = m_scope_illum_min + (m_scope_illum_max - m_scope_illum_min) * (float(m_scope_illum_step) / denom);
}

void CWeapon::ResetScopeIllumToDefault()
{
	// PER-SCOPE brightness: restore THIS scope's remembered step if the player set one before, else the scope's
	// default_brightness_step. (Index by the attached scope; single-scope weapons use slot 0.)
	const int idx = (m_cur_scope < 16) ? (int)m_cur_scope : 0;
	if (m_scope_illum_step_by_scope[idx] >= 0)
		m_scope_illum_step = m_scope_illum_step_by_scope[idx];
	else
	{
		shared_str sc = GetCurrentScopeSection();
		m_scope_illum_step = sc.size() ? (int)READ_IF_EXISTS(pSettings, r_u32, *sc, "default_brightness_step", 0) : 0;
	}
	LoadScopeIllumParams();
	ApplyScopeIllumUI();
}

// GS `switchable_zoom_wnd` (ui\scopes_16.xml): a scope's crosshair window stacks one static per
// BRIGHTNESS LEVEL at the same rect -- level 0 = <tex>_off, then the intermediate levels, last = _on --
// followed by the side fillers at their own rects. Show exactly the static matching the current step, so
// the 2D reticle lights up with the brightness keys (the PGO-7V/PSO reticle is drawn by the crosshair
// texture, not by model geometry). A window with a single level is left alone = unchanged behaviour.
void CWeapon::ApplyScopeIllumUI()
{
	if (!m_UIScope)	return;
	auto& lst = m_UIScope->GetChildWndList();
	if (lst.empty())	return;

	// the leading run of children sharing the first one's rect are the levels
	const Frect first = (*lst.begin())->GetWndRect();
	int levels = 0;
	for (auto it = lst.begin(); it != lst.end(); ++it)
	{
		Frect r = (*it)->GetWndRect();
		if (!fsimilar(r.x1, first.x1) || !fsimilar(r.y1, first.y1)
			|| !fsimilar(r.x2, first.x2) || !fsimilar(r.y2, first.y2))	break;
		++levels;
	}
	if (levels < 2)	return;			// nothing to switch

	int step = m_scope_illum_step;
	clamp(step, 0, levels - 1);
	int i = 0;
	for (auto it = lst.begin(); it != lst.end() && i < levels; ++it, ++i)
		(*it)->Show(i == step);
}

void CWeapon::ChangeScopeIllum(int delta)
{
	if (!IsScopeAttached())	return;
	LoadScopeIllumParams();					// pick up the current scope's range (scope may have changed)
	const int prev_step = m_scope_illum_step;
	m_scope_illum_step += delta;
	clamp(m_scope_illum_step, 0, m_scope_illum_steps);
	// Gunslinger scope-brightness click (snd_scope_brightness_plus/minus) -- only when the step
	// actually moved, so no sound at the min/max ends. Optional: silent if the weapon has no key.
	if (m_scope_illum_step != prev_step)
	{
		LPCSTR snd_alias = (delta > 0) ? "sndScopeBrightPlus" : "sndScopeBrightMinus";
		if (m_sounds.FindSoundItem(snd_alias, false))
			PlaySound(snd_alias, get_LastFP());
	}
	const int denom = (m_scope_illum_steps > 0) ? m_scope_illum_steps : 1;
	m_scope_illum_value = m_scope_illum_min + (m_scope_illum_max - m_scope_illum_min) * (float(m_scope_illum_step) / denom);
	// remember this brightness for THIS scope specifically
	const int idx = (m_cur_scope < 16) ? (int)m_cur_scope : 0;
	m_scope_illum_step_by_scope[idx] = m_scope_illum_step;
	ApplyScopeIllumUI();
}

void CWeapon::SwitchState(u32 S)
{
	if (OnClient()) return;

#ifndef MASTER_GOLD
	if ( bDebug )
	{
		Msg("---Server is going to send GE_WPN_STATE_CHANGE to [%d], weapon_section[%s], parent[%s]",
			S, cNameSect().c_str(), H_Parent() ? H_Parent()->cName().c_str() : "NULL Parent");
	}
#endif // #ifndef MASTER_GOLD

	SetNextState		( S );
	if (CHudItem::object().Local() && !CHudItem::object().getDestroy() && m_pInventory && OnServer())	
	{
		// !!! Just single entry for given state !!!
		NET_Packet		P;
		CHudItem::object().u_EventGen		(P,GE_WPN_STATE_CHANGE,CHudItem::object().ID());
		P.w_u8			(u8(S));
		P.w_u8			(u8(m_sub_state));
		P.w_u8			(u8(m_ammoType& 0xff));
		P.w_u8			(u8(iAmmoElapsed & 0xff));
		P.w_u8			(u8(m_set_next_ammoType_on_reload & 0xff));
		CHudItem::object().u_EventSend		(P, net_flags(TRUE, TRUE, FALSE, TRUE));
	}
}

void CWeapon::OnMagazineEmpty	()
{
	VERIFY((u32)iAmmoElapsed == m_magazine.size());
}


void CWeapon::reinit			()
{
	CShootingObject::reinit		();
	CHudItemObject::reinit			();
}

void CWeapon::reload			(LPCSTR section)
{
	CShootingObject::reload		(section);
	CHudItemObject::reload			(section);
	
	m_can_be_strapped			= true;
	m_strapped_mode				= false;
	
	if (pSettings->line_exist(section,"strap_bone0"))
		m_strap_bone0			= pSettings->r_string(section,"strap_bone0");
	else
		m_can_be_strapped		= false;
	
	if (pSettings->line_exist(section,"strap_bone1"))
		m_strap_bone1			= pSettings->r_string(section,"strap_bone1");
	else
		m_can_be_strapped		= false;

	if (m_eScopeStatus == ALife::eAddonAttachable) {
		m_addon_holder_range_modifier	= READ_IF_EXISTS(pSettings,r_float,m_sScopeName,"holder_range_modifier",m_holder_range_modifier);
		m_addon_holder_fov_modifier		= READ_IF_EXISTS(pSettings,r_float,m_sScopeName,"holder_fov_modifier",m_holder_fov_modifier);
	}
	else {
		m_addon_holder_range_modifier	= m_holder_range_modifier;
		m_addon_holder_fov_modifier		= m_holder_fov_modifier;
	}


	{
		Fvector				pos,ypr;
		pos					= pSettings->r_fvector3		(section,"position");
		ypr					= pSettings->r_fvector3		(section,"orientation");
		ypr.mul				(PI/180.f);

		m_Offset.setHPB			(ypr.x,ypr.y,ypr.z);
		m_Offset.translate_over	(pos);
	}

	m_StrapOffset			= m_Offset;
	if (pSettings->line_exist(section,"strap_position") && pSettings->line_exist(section,"strap_orientation")) {
		Fvector				pos,ypr;
		pos					= pSettings->r_fvector3		(section,"strap_position");
		ypr					= pSettings->r_fvector3		(section,"strap_orientation");
		ypr.mul				(PI/180.f);

		m_StrapOffset.setHPB			(ypr.x,ypr.y,ypr.z);
		m_StrapOffset.translate_over	(pos);
	}
	else
		m_can_be_strapped	= false;

	m_ef_main_weapon_type	= READ_IF_EXISTS(pSettings,r_u32,section,"ef_main_weapon_type",u32(-1));
	m_ef_weapon_type		= READ_IF_EXISTS(pSettings,r_u32,section,"ef_weapon_type",u32(-1));
}

void CWeapon::create_physic_shell()
{
	CPhysicsShellHolder::create_physic_shell();
}

void CWeapon::activate_physic_shell()
{
	UpdateXForm();
	CPhysicsShellHolder::activate_physic_shell();
}

void CWeapon::setup_physic_shell()
{
	CPhysicsShellHolder::setup_physic_shell();
}

int		g_iWeaponRemove = 1;

bool CWeapon::NeedToDestroyObject()	const
{
	if (GameID() == eGameIDSingle) return false;
	if (Remote()) return false;
	if (H_Parent()) return false;
	if (g_iWeaponRemove == -1) return false;
	if (g_iWeaponRemove == 0) return true;
	if (TimePassedAfterIndependant() > m_dwWeaponRemoveTime)
		return true;

	return false;
}

ALife::_TIME_ID	 CWeapon::TimePassedAfterIndependant()	const
{
	if(!H_Parent() && m_dwWeaponIndependencyTime != 0)
		return Level().timeServer() - m_dwWeaponIndependencyTime;
	else
		return 0;
}

bool CWeapon::can_kill	() const
{
	if (GetSuitableAmmoTotal(true) || m_ammoTypes.empty())
		return				(true);

	return					(false);
}

CInventoryItem *CWeapon::can_kill	(CInventory *inventory) const
{
	if (GetAmmoElapsed() || m_ammoTypes.empty())
		return				(const_cast<CWeapon*>(this));

	TIItemContainer::iterator I = inventory->m_all.begin();
	TIItemContainer::iterator E = inventory->m_all.end();
	for ( ; I != E; ++I) {
		CInventoryItem	*inventory_item = smart_cast<CInventoryItem*>(*I);
		if (!inventory_item)
			continue;
		
		xr_vector<shared_str>::const_iterator	i = std::find(m_ammoTypes.begin(),m_ammoTypes.end(),inventory_item->object().cNameSect());
		if (i != m_ammoTypes.end())
			return			(inventory_item);
	}

	return					(0);
}

const CInventoryItem *CWeapon::can_kill	(const xr_vector<const CGameObject*> &items) const
{
	if (m_ammoTypes.empty())
		return				(this);

	xr_vector<const CGameObject*>::const_iterator I = items.begin();
	xr_vector<const CGameObject*>::const_iterator E = items.end();
	for ( ; I != E; ++I) {
		const CInventoryItem	*inventory_item = smart_cast<const CInventoryItem*>(*I);
		if (!inventory_item)
			continue;

		xr_vector<shared_str>::const_iterator	i = std::find(m_ammoTypes.begin(),m_ammoTypes.end(),inventory_item->object().cNameSect());
		if (i != m_ammoTypes.end())
			return			(inventory_item);
	}

	return					(0);
}

bool CWeapon::ready_to_kill	() const
{
	return					(
		!IsMisfire() && 
		((GetState() == eIdle) || (GetState() == eFire) || (GetState() == eFire2)) && 
		GetAmmoElapsed()
	);
}


// Ease the HUD (weapon-render) FOV from hud_fov toward hud_fov_aim as the weapon
// rotates into aim (m_fZoomRotationFactor 0..1, over zoom_rotate_time) — Gunslinger's
// smooth hud_fov_zoom_factor. hud_fov_aim<hud_fov narrows the HUD FOV = the weapon/sight
// "zooms in" smoothly. When hud_fov_aim is unset (0) nothing changes.
extern float g_pda_hud_fov_aim;			// 3D PDA live tuning; see CHudItem::GetHudFov

float CWeapon::GetHudFov()
{
	float base = inherited::GetHudFov();
	// A lensed scope uses its own scope_hud_fov_aim (brings the sight close to the eye) so the weapon does NOT
	// come closer on plain iron-sight aiming (which has no scope attached) -- that would double up with the
	// iron-sight world-FOV zoom.
	// GS multi-scope: each optic has its own aim FOV (scope_hud_fov_aim). Prefer the active scope section's
	// value, else the HUD-section m_fHudFovAimScope, else the iron-sight aim FOV.
	float scope_fov = m_fHudFovAimScope;
	{
		shared_str sc = GetCurrentScopeSection();
		if (IsScopeAttached() && sc.size() && pSettings->line_exist(*sc, "scope_hud_fov_aim"))
			scope_fov = pSettings->r_float(*sc, "scope_hud_fov_aim");
	}
	// a collimator has no lens but still aims through the optic, so it uses the per-scope aim FOV too
	float aim_cfg = ((IsLensedScope() || IsCollimatorScope()) && scope_fov > 0.f) ? scope_fov : m_fHudFovAim;
	// GS alter zoom: the second aim pose has its own hud fov -- eased in/out with the same blend as the
	// offset, so the view glides between the two poses instead of jumping. Applied AFTER the pose above is
	// picked, so it works for a built-in optic too (no scope item -> the keys live in the HUD section).
	{
		const float ab = AlterZoomBlend();
		shared_str az = AlterZoomSection();
		if (ab > 0.f && aim_cfg > 0.f && az.size() && pSettings->section_exist(*az)
			&& pSettings->line_exist(*az, "scope_hud_fov_alter_aim"))
		{
			const float alt = pSettings->r_float(*az, "scope_hud_fov_alter_aim");
			aim_cfg = aim_cfg + (alt - aim_cfg) * ab;
		}
	}
	if(m_bPdaCursorAnims && g_pda_hud_fov_aim > 0.f)
		aim_cfg = g_pda_hud_fov_aim;
	if(aim_cfg <= 0.f)
		return base;

	float f = m_zoom_params.m_fZoomRotationFactor;
	clamp(f, 0.f, 1.f);
	if(f <= 0.f)
		return base;

	float aim = aim_cfg;
	clamp(aim, 0.1f, 1.0f);
	return base + (aim - base) * f;
}

// Gunslinger per-scope aim offset (collimator.pas SaveHudOffsets): an attached scope needs its own
// aim_hud_offset -- it sits higher/further back than the iron sights, so aiming with it must not reuse
// the weapon's iron-sight aim offset. Reads scope_aim_hud_offset_pos/rot (+_16x9) from the weapon HUD
// section; the _16x9 variant falls back to the base key, a missing key leaves the normal aim offset intact.
static bool read_scope_aim_offset(LPCSTR scope_sect, LPCSTR hud_sect, bool wide, Fvector& pos, Fvector& rot,
								  bool alter = false)
{
	// GS multi-scope: the attached scope's OWN aim_hud_offset_pos/rot ([+_16x9]) wins -- each optic sits at a
	// different height/depth. Falls back to the weapon HUD section's scope_aim_hud_offset_* (single-scope path).
	// GS alter zoom: while the second aim pose is active, its alter_aim_hud_offset_* replace those (the eye
	// moves to the ELCAN's other optic). Falls through to the normal offsets if the scope defines none.
	if (alter && scope_sect && scope_sect[0])
	{
		if (wide
			&& pSettings->line_exist(scope_sect, "alter_aim_hud_offset_pos_16x9")
			&& pSettings->line_exist(scope_sect, "alter_aim_hud_offset_rot_16x9"))
		{
			pos = pSettings->r_fvector3(scope_sect, "alter_aim_hud_offset_pos_16x9");
			rot = pSettings->r_fvector3(scope_sect, "alter_aim_hud_offset_rot_16x9");
			return true;
		}
		if (pSettings->line_exist(scope_sect, "alter_aim_hud_offset_pos")
			&& pSettings->line_exist(scope_sect, "alter_aim_hud_offset_rot"))
		{
			pos = pSettings->r_fvector3(scope_sect, "alter_aim_hud_offset_pos");
			rot = pSettings->r_fvector3(scope_sect, "alter_aim_hud_offset_rot");
			return true;
		}
	}
	if (scope_sect && scope_sect[0])
	{
		if (wide
			&& pSettings->line_exist(scope_sect, "aim_hud_offset_pos_16x9")
			&& pSettings->line_exist(scope_sect, "aim_hud_offset_rot_16x9"))
		{
			pos = pSettings->r_fvector3(scope_sect, "aim_hud_offset_pos_16x9");
			rot = pSettings->r_fvector3(scope_sect, "aim_hud_offset_rot_16x9");
			return true;
		}
		if (pSettings->line_exist(scope_sect, "aim_hud_offset_pos")
			&& pSettings->line_exist(scope_sect, "aim_hud_offset_rot"))
		{
			pos = pSettings->r_fvector3(scope_sect, "aim_hud_offset_pos");
			rot = pSettings->r_fvector3(scope_sect, "aim_hud_offset_rot");
			return true;
		}
	}
	if (wide
		&& pSettings->line_exist(hud_sect, "scope_aim_hud_offset_pos_16x9")
		&& pSettings->line_exist(hud_sect, "scope_aim_hud_offset_rot_16x9"))
	{
		pos = pSettings->r_fvector3(hud_sect, "scope_aim_hud_offset_pos_16x9");
		rot = pSettings->r_fvector3(hud_sect, "scope_aim_hud_offset_rot_16x9");
		return true;
	}
	if (pSettings->line_exist(hud_sect, "scope_aim_hud_offset_pos")
		&& pSettings->line_exist(hud_sect, "scope_aim_hud_offset_rot"))
	{
		pos = pSettings->r_fvector3(hud_sect, "scope_aim_hud_offset_pos");
		rot = pSettings->r_fvector3(hud_sect, "scope_aim_hud_offset_rot");
		return true;
	}
	return false;
}

void CWeapon::UpdateHudAdditonal		(Fmatrix& trans)
{
	CActor* pActor	= smart_cast<CActor*>(H_Parent());
	if(!pActor)		return;


	if(		(IsZoomed() && m_zoom_params.m_fZoomRotationFactor<=1.f) ||
			(!IsZoomed() && m_zoom_params.m_fZoomRotationFactor>0.f))
	{
		u8 idx = GetCurrentHudOffsetIdx();
//		if(idx==0)					return;

		attachable_hud_item*		hi = HudItemData();
		R_ASSERT					(hi);
		Fvector						curr_offs, curr_rot;
		curr_offs					= hi->m_measures.m_hands_offset[0][idx];//pos,aim
		curr_rot					= hi->m_measures.m_hands_offset[1][idx];//rot,aim

		// Gunslinger: aiming through an attached scope uses the scope's own aim offset, not the iron sights'.
		if (idx==1 && IsScopeAttached())
		{
			bool	wide = hi->m_measures.m_prop_flags.test(hud_item_measures::e_16x9_mode_now);
			Fvector	spos, srot;
			shared_str sc = GetCurrentScopeSection();
			// While the in-game HUD tuner is active (hud_adj_mode, Mixed build: Shift+Num1/2) let it drive the
			// aim offset directly -- otherwise the per-scope override swallows the tuner's changes.
			extern u32 hud_adj_mode;
			if (hud_adj_mode == 0 && read_scope_aim_offset(sc.size() ? *sc : nullptr, *hi->m_sect_name, wide, spos, srot))
			{
				curr_offs	= spos;
				curr_rot	= srot;
				// GS alter zoom: blend smoothly toward the second aim pose (eased), never snap.
				const float ab = AlterZoomBlend();
				if (ab > 0.f)
				{
					Fvector apos, arot;
					if (read_scope_aim_offset(sc.size() ? *sc : nullptr, *hi->m_sect_name, wide, apos, arot, true))
					{
						curr_offs.lerp(spos, apos, ab);
						curr_rot.lerp (srot, arot, ab);
					}
				}
			}
		}

		curr_offs.mul				(m_zoom_params.m_fZoomRotationFactor);
		curr_rot.mul				(m_zoom_params.m_fZoomRotationFactor);

		Fmatrix						hud_rotation;
		hud_rotation.identity		();
		hud_rotation.rotateX		(curr_rot.x);

		Fmatrix						hud_rotation_y;
		hud_rotation_y.identity		();
		hud_rotation_y.rotateY		(curr_rot.y);
		hud_rotation.mulA_43		(hud_rotation_y);

		hud_rotation_y.identity		();
		hud_rotation_y.rotateZ		(curr_rot.z);
		hud_rotation.mulA_43		(hud_rotation_y);

		hud_rotation.translate_over	(curr_offs);
		trans.mulB_43				(hud_rotation);

		if(pActor->IsZoomAimingMode())
			m_zoom_params.m_fZoomRotationFactor += Device.fTimeDelta/m_zoom_params.m_fZoomRotateTime;
		else
			m_zoom_params.m_fZoomRotationFactor -= Device.fTimeDelta/m_zoom_params.m_fZoomRotateTime;

		clamp(m_zoom_params.m_fZoomRotationFactor, 0.f, 1.f);
	}
}

void CWeapon::SetAmmoElapsed(int ammo_count)
{
	iAmmoElapsed				= ammo_count;

	u32 uAmmo					= u32(iAmmoElapsed);

	if (uAmmo != m_magazine.size())
	{
		if (uAmmo > m_magazine.size())
		{
			CCartridge			l_cartridge; 
			l_cartridge.Load	(*m_ammoTypes[m_ammoType], u8(m_ammoType));
			while (uAmmo > m_magazine.size())
				m_magazine.push_back(l_cartridge);
		}
		else
		{
			while (uAmmo < m_magazine.size())
				m_magazine.pop_back();
		};
	};
}

u32	CWeapon::ef_main_weapon_type	() const
{
	VERIFY	(m_ef_main_weapon_type != u32(-1));
	return	(m_ef_main_weapon_type);
}

u32	CWeapon::ef_weapon_type	() const
{
	VERIFY	(m_ef_weapon_type != u32(-1));
	return	(m_ef_weapon_type);
}

bool CWeapon::IsNecessaryItem	    (const shared_str& item_sect)
{
	return (std::find(m_ammoTypes.begin(), m_ammoTypes.end(), item_sect) != m_ammoTypes.end() );
}

void CWeapon::modify_holder_params		(float &range, float &fov) const
{
	if (!IsScopeAttached()) {
		inherited::modify_holder_params	(range,fov);
		return;
	}
	range	*= m_addon_holder_range_modifier;
	fov		*= m_addon_holder_fov_modifier;
}

bool CWeapon::render_item_ui_query()
{
	bool b_is_active_item = (m_pInventory->ActiveItem()==this);
	bool res = b_is_active_item && IsZoomed() && ZoomHideCrosshair() && ZoomTexture() && !IsRotatingToZoom();
	return res;
}

void CWeapon::render_item_ui()
{
	ZoomTexture()->Update	();
	ZoomTexture()->Draw		();
}

bool CWeapon::unlimited_ammo() 
{ 
	if (IsGameTypeSingle())
		return psActorFlags.test(AF_UNLIMITEDAMMO) && 
				m_DefaultCartridge.m_flags.test(CCartridge::cfCanBeUnlimited); 

	return ((GameID() != eGameIDArtefactHunt) && 
			(GameID() != eGameIDCaptureTheArtefact) &&
			m_DefaultCartridge.m_flags.test(CCartridge::cfCanBeUnlimited)); 
			
};

LPCSTR	CWeapon::GetCurrentAmmo_ShortName	()
{
	if (m_magazine.empty()) return ("");
	CCartridge &l_cartridge = m_magazine.back();
	return *(l_cartridge.m_InvShortName);
}

float CWeapon::Weight() const
{
	float res = CInventoryItemObject::Weight();
	if(IsGrenadeLauncherAttached()&&GetGrenadeLauncherName().size()){
		res += pSettings->r_float(GetGrenadeLauncherName(),"inv_weight");
	}
	if(IsScopeAttached()&&GetScopeName().size()){
		res += pSettings->r_float(GetScopeName(),"inv_weight");
	}
	if(IsSilencerAttached()&&GetSilencerName().size()){
		res += pSettings->r_float(GetSilencerName(),"inv_weight");
	}
	
	if(iAmmoElapsed)
	{
		float w		= pSettings->r_float(*m_ammoTypes[m_ammoType],"inv_weight");
		float bs	= pSettings->r_float(*m_ammoTypes[m_ammoType],"box_size");

		res			+= w*(iAmmoElapsed/bs);
	}
	return res;
}

u32 CWeapon::Cost() const
{
	u32 res = CInventoryItem::Cost();
	if (IsGrenadeLauncherAttached() && GetGrenadeLauncherName().size())
	{
		res += pSettings->r_u32(GetGrenadeLauncherName(), "cost");
	}
	if (IsScopeAttached() && GetScopeName().size())
	{
		res += pSettings->r_u32(GetScopeName(), "cost");
	}
	if (IsSilencerAttached() && GetSilencerName().size())
	{
		res += pSettings->r_u32(GetSilencerName(), "cost");
	}

	if (iAmmoElapsed)
	{
		float w = pSettings->r_float(m_ammoTypes[m_ammoType].c_str(), "cost");
		float bs = pSettings->r_float(m_ammoTypes[m_ammoType].c_str(), "box_size");

		res += iFloor(w * (iAmmoElapsed / bs));
	}
	return res;
}

bool CWeapon::show_crosshair()
{
	// GS: an enabled laser designator replaces the crosshair -- you aim with the dot, so hide it
	if (m_bLaserInstalled && m_bLaserEnabled)	return false;
	return !IsPending() && ( !IsZoomed() || !ZoomHideCrosshair() );
}

bool CWeapon::show_indicators()
{
	// GS drawingame_conditions (ActorUtils.pas:3262) opens with `if IsLensFrameNow() then result := false`:
	// the ingame HUD is not drawn while the PiP lens is up -- looking through the scope shows the scope,
	// not the indicators. The vanilla rule below only covers the 2D full-screen scope picture, which a
	// lensed scope deliberately does not use (UseScopeTexture -> ZoomTexture() == NULL), so it never fired
	// for a PiP optic. Includes the alter pose (backup 1x sight, lens faded out): the user wants the HUD
	// gone for the whole aim on such a scope, not just while the eye is behind the lens.
	if (IsZoomed() && IsLensedScope())	return false;
	return ! ( IsZoomed() && ZoomTexture() );
}

float CWeapon::GetConditionToShow	() const
{
	return	(GetCondition());//powf(GetCondition(),4.0f));
}

BOOL CWeapon::ParentMayHaveAimBullet	()
{
	CObject* O=H_Parent();
	CEntityAlive* EA=smart_cast<CEntityAlive*>(O);
	return EA->cast_actor()!=0;
}

BOOL CWeapon::ParentIsActor	()
{
	CObject* O=H_Parent();
	CEntityAlive* EA=smart_cast<CEntityAlive*>(O);
	return EA->cast_actor()!=0;
}

extern u32 hud_adj_mode;

void CWeapon::debug_draw_firedeps()
{
#ifdef DEBUG
	if(hud_adj_mode==5||hud_adj_mode==6||hud_adj_mode==7)
	{
		CDebugRenderer			&render = Level().debug_renderer();

		if(hud_adj_mode==5)
			render.draw_aabb(get_LastFP(), 0.005f, 0.005f, 0.005f, color_xrgb(255, 0, 0));

		if(hud_adj_mode==6)
			render.draw_aabb(get_LastFP2(), 0.005f, 0.005f, 0.005f, color_xrgb(0, 0, 255));

		if(hud_adj_mode==7)
			render.draw_aabb(get_LastSP(), 0.005f, 0.005f, 0.005f, color_xrgb(0, 255, 0));
	}
#endif // DEBUG
}

const float &CWeapon::hit_probability	() const
{
	VERIFY					((g_SingleGameDifficulty >= egdNovice) && (g_SingleGameDifficulty <= egdMaster)); 
	return					(m_hit_probability[egdNovice]);
}

void CWeapon::OnStateSwitch	(u32 S)
{
	inherited::OnStateSwitch(S);
	m_dwAmmoCurrentCalcFrame = 0;

	// A HOLSTERED weapon (still owned) stops being ticked by UpdateFlashlight/UpdateLaserDot (those run only for
	// the ACTIVE HUD weapon), so its mounted light + laser dot would otherwise FREEZE at the last position and
	// only refresh when drawn again -- kill both when it enters the hidden state. A DROPPED weapon (independent
	// world object, no parent) is the exception: the UpdateCL world-branch keeps ticking its flashlight, so KEEP
	// the flashlight lit if it was on (a dropped weapon with the flashlight on keeps shining). The laser dot is
	// actor-active-only, so always stop it.
	if (S == eHidden)
	{
		const bool dropped = (H_Parent() == nullptr);	// independent world object vs holstered (still owned)
		if (!(dropped && m_bFlashEnabled))	StopFlashlight();
		StopLaserDot	();
	}

	// The vanilla reload DOF lived here: a CEffectorDOF armed on entering eReload that undid itself
	// after a FIXED `reload_dof.w` seconds, so it drifted out of step with the animation it was
	// supposed to follow. GS instead arms the DOF from the animation itself and releases it a set
	// time before that animation ENDS -- see CHudItem::PlayHUDMotion / UpdateCL.
}

void CWeapon::OnAnimationEnd(u32 state) 
{
	inherited::OnAnimationEnd(state);
}

u8 CWeapon::GetCurrentHudOffsetIdx()
{
	CActor* pActor	= smart_cast<CActor*>(H_Parent());
	if(!pActor)		return 0;
	
	bool b_aiming		= 	((IsZoomed() && m_zoom_params.m_fZoomRotationFactor<=1.f) ||
							(!IsZoomed() && m_zoom_params.m_fZoomRotationFactor>0.f));

	if(!b_aiming)
		return		0;
	else
		return		1;
}

void CWeapon::render_hud_mode()
{
	RenderLight();
}

bool CWeapon::MovingAnimAllowedNow()
{
	return !IsZoomed();
}

bool CWeapon::IsHudModeNow()
{
	return (HudItemData()!=NULL);
}
