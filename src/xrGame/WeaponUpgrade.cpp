
#include "stdafx.h"
#include "Weapon.h"
#include "IXRayGameConstants.h"

static bool process_if_exists_deg2rad( LPCSTR section, LPCSTR name, float& value, bool test )
{
	if ( !pSettings->line_exist( section, name ) )
	{
		return false;
	}
	LPCSTR str = pSettings->r_string( section, name );
	if ( !str || !xr_strlen(str) )
	{
		return false;
	}

	if ( !test )
	{
		value += deg2rad( pSettings->r_float( section, name ) );
	}
	return true;
}

bool CWeapon::install_upgrade_impl( LPCSTR section, bool test )
{
	//inherited::install_upgrade( section );
	bool result = CInventoryItemObject::install_upgrade_impl( section, test );
	
	result |= install_upgrade_ammo_class( section, test );
	result |= install_upgrade_disp      ( section, test );
	result |= install_upgrade_hit       ( section, test );
	result |= install_upgrade_addon     ( section, test );

	// GS-style anim-set swap: an upgrade may repoint `hud` at an alternate HUD section (e.g. the _tac
	// variant, which :inherits the base hud and only overrides the anm_* motions -- same item_visual/model).
	// Set hud_sect so the next HUD draw plays the new motion set. Re-applied on save-load (upgrades restore
	// after Load(), which set the base hud first).
	if ( pSettings->line_exist( section, "hud" ) )
	{
		if ( !test )	hud_sect = pSettings->r_string( section, "hud" );
		result = true;
	}

	// ...and the WORLD model with it. The same node usually swaps both (the sig550 tactical foregrip
	// sets `hud` and `visual` together), but only `hud` was ever applied -- so an upgraded weapon wore
	// its tactical hands in first person and its stock model on the ground, in NPC hands and in the
	// third-person view. Re-applied on every net_Spawn like the rest, so it survives save-load.
	if ( pSettings->line_exist( section, "visual" ) )
	{
		if ( !test )
		{
			shared_str v = pSettings->r_string( section, "visual" );
			if ( v.size() && v != cNameVisual() )	cNameVisual_set( v );
		}
		result = true;
	}

	// laser designator install (GS LAM): the upgrade only marks the laser INSTALLED -- it starts OFF (the
	// player toggles it with kWPN_LASER). Do NOT force m_bLaserEnabled here: the ctor default is false, so a
	// fresh install is off, and NOT touching it lets the saved on/off state persist across a reload (install
	// re-runs on every net_Spawn, which would otherwise reset the toggle).
	if ( pSettings->line_exist( section, "laser_installed" ) )
	{
		if ( !test )	m_bLaserInstalled = !!pSettings->r_bool( section, "laser_installed" );
		result = true;
	}

	// GS bayonet upgrade: marks the weapon so the quick-kick key stabs with the weapon's own anm_kick
	// (ak74_bayonet) instead of the knife phantom. Visual (the `knife` bone) comes from show_bones.
	if ( pSettings->line_exist( section, "bayonet_installed" ) )
	{
		if ( !test )	m_bBayonetInstalled = !!pSettings->r_bool( section, "bayonet_installed" );
		result = true;
	}

	// weapon-mounted flashlight (GS torch_installed): same as the laser -- INSTALLED only, starts OFF (toggle
	// with kWPN_FLASHLIGHT). Don't force m_bFlashEnabled (ctor default false; keeps the saved toggle on reload).
	if ( pSettings->line_exist( section, "torch_installed" ) || pSettings->line_exist( section, "flashlight_installed" ) )
	{
		if ( !test )
		{
			LPCSTR k = pSettings->line_exist(section, "torch_installed") ? "torch_installed" : "flashlight_installed";
			m_bFlashInstalled = !!pSettings->r_bool( section, k );
		}
		result = true;
	}
	return result;
}

bool CWeapon::install_upgrade_ammo_class( LPCSTR section, bool test )
{
	LPCSTR str;

	bool result = process_if_exists( section, "ammo_mag_size", &CInifile::r_s32, iMagazineSize, test );

	//	ammo_class = ammo_5.45x39_fmj, ammo_5.45x39_ap  // name of the ltx-section of used ammo
	bool result2 = process_if_exists_set( section, "ammo_class", &CInifile::r_string, str, test );
	if ( result2 && !test )
	{
		m_ammoTypes.clear();
		string128 ammoItem;
		int count = _GetItemCount( str );
		for ( int i = 0; i < count; ++i )
		{
			_GetItem( str, i, ammoItem );
			m_ammoTypes.push_back( ammoItem );
		}
		m_ammoName = pSettings->r_string( *m_ammoTypes[0], "inv_name_short" );
		m_ammoType = 0;
	}
	result |= result2;

	return result;
}

bool CWeapon::install_upgrade_disp( LPCSTR section, bool test )
{
	bool result = process_if_exists( section, "fire_dispersion_condition_factor", &CInifile::r_float, fireDispersionConditionFactor, test );
	result     |= process_if_exists( section, "fire_distance",                    &CInifile::r_float, fireDistance,                  test );

	
	u8 rm = (cam_recoil.ReturnMode)? 1 : 0;
	result |= process_if_exists_set( section, "cam_return", &CInifile::r_u8, rm, test );
	cam_recoil.ReturnMode = (rm == 1);

	rm = (cam_recoil.StopReturn)? 1 : 0;
	result |= process_if_exists_set( section, "cam_return_stop", &CInifile::r_u8, rm, test );
	cam_recoil.StopReturn = (rm == 1);

	result |= process_if_exists_deg2rad( section, "fire_dispersion_base", fireDispersionBase, test );

	result |= process_if_exists_deg2rad( section, "cam_relax_speed",      cam_recoil.RelaxSpeed,      test );
	result |= process_if_exists_deg2rad( section, "cam_relax_speed_ai",   cam_recoil.RelaxSpeed_AI,   test );
	result |= process_if_exists_deg2rad( section, "cam_dispersion",       cam_recoil.Dispersion,      test );
	result |= process_if_exists_deg2rad( section, "cam_dispersion_inc",   cam_recoil.DispersionInc,   test );
	
	result |= process_if_exists(         section, "cam_dispersion_frac",  &CInifile::r_float, cam_recoil.DispersionFrac, test );

	result |= process_if_exists_deg2rad( section, "cam_max_angle",        cam_recoil.MaxAngleVert,  test );
	result |= process_if_exists_deg2rad( section, "cam_max_angle_horz",   cam_recoil.MaxAngleHorz,  test );
	result |= process_if_exists_deg2rad( section, "cam_step_angle_horz",  cam_recoil.StepAngleHorz, test );

	VERIFY( !fis_zero(cam_recoil.RelaxSpeed   ) );
	VERIFY( !fis_zero(cam_recoil.RelaxSpeed_AI) );
	VERIFY( !fis_zero(cam_recoil.MaxAngleVert ) );
	VERIFY( !fis_zero(cam_recoil.MaxAngleHorz ) );

	result |= process_if_exists_deg2rad( section, "zoom_cam_relax_speed",      zoom_cam_recoil.RelaxSpeed,      test );// zoom_ ...
	result |= process_if_exists_deg2rad( section, "zoom_cam_relax_speed_ai",   zoom_cam_recoil.RelaxSpeed_AI,   test );
	result |= process_if_exists_deg2rad( section, "zoom_cam_dispersion",       zoom_cam_recoil.Dispersion,      test );
	result |= process_if_exists_deg2rad( section, "zoom_cam_dispersion_inc",   zoom_cam_recoil.DispersionInc,   test );

	result |= process_if_exists(         section, "zoom_cam_dispersion_frac",  &CInifile::r_float, zoom_cam_recoil.DispersionFrac, test );

	result |= process_if_exists_deg2rad( section, "zoom_cam_max_angle",        zoom_cam_recoil.MaxAngleVert,  test );
	result |= process_if_exists_deg2rad( section, "zoom_cam_max_angle_horz",   zoom_cam_recoil.MaxAngleHorz,  test );
	result |= process_if_exists_deg2rad( section, "zoom_cam_step_angle_horz",  zoom_cam_recoil.StepAngleHorz, test );

	VERIFY( !fis_zero(zoom_cam_recoil.RelaxSpeed   ) );
	VERIFY( !fis_zero(zoom_cam_recoil.RelaxSpeed_AI) );
	VERIFY( !fis_zero(zoom_cam_recoil.MaxAngleVert ) );
	VERIFY( !fis_zero(zoom_cam_recoil.MaxAngleHorz ) );

	result |= process_if_exists( section, "PDM_disp_base",          &CInifile::r_float, m_pdm.m_fPDM_disp_base,          test );
	result |= process_if_exists( section, "PDM_disp_vel_factor",    &CInifile::r_float, m_pdm.m_fPDM_disp_vel_factor,    test );
	result |= process_if_exists( section, "PDM_disp_accel_factor",  &CInifile::r_float, m_pdm.m_fPDM_disp_accel_factor,  test );
	result |= process_if_exists( section, "PDM_disp_crouch",        &CInifile::r_float, m_pdm.m_fPDM_disp_crouch,        test );
	result |= process_if_exists( section, "PDM_disp_crouch_no_acc", &CInifile::r_float, m_pdm.m_fPDM_disp_crouch_no_acc, test );

	result |= process_if_exists( section, "misfire_probability", &CInifile::r_float, misfireProbability,       test );
	result |= process_if_exists( section, "misfire_condition_k", &CInifile::r_float, misfireConditionK,        test );
	result |= process_if_exists( section, "condition_shot_dec",  &CInifile::r_float, conditionDecreasePerShot, test );

	BOOL value = m_zoom_params.m_bZoomEnabled;
	bool result2 = process_if_exists_set( section, "zoom_enabled", &CInifile::r_bool, value, test );
	if ( result2 && !test )
	{
		m_zoom_params.m_bZoomEnabled = !!value;
	}
	result |= result2;
	
	return result;
}

bool CWeapon::install_upgrade_hit( LPCSTR section, bool test )
{
	bool result = false;

	shared_str	s_sHitPower;
	bool result2 = process_if_exists_set( section, "hit_power", &CInifile::r_string_wb, s_sHitPower, test );
	if ( result2 && !test )
	{
		string32 buffer;
		fvHitPower[egdMaster] = (float)atof( _GetItem( *s_sHitPower, 0, buffer ) );
		fvHitPower[egdNovice] = fvHitPower[egdStalker] = fvHitPower[egdVeteran] = fvHitPower[egdMaster];

		int num_game_diff_param = _GetItemCount( *s_sHitPower );
		if ( num_game_diff_param > 1 ) { fvHitPower[egdVeteran]	= (float)atof( _GetItem( *s_sHitPower, 1, buffer ) ); }
		if ( num_game_diff_param > 2 ) { fvHitPower[egdStalker]	= (float)atof( _GetItem( *s_sHitPower, 2, buffer ) ); }
		if ( num_game_diff_param > 3 ) { fvHitPower[egdNovice]	= (float)atof( _GetItem( *s_sHitPower, 3, buffer ) ); }
	}
	result |= result2;

	shared_str	s_sHitPowerCritical;
	result2 = process_if_exists_set( section, "hit_power_critical", &CInifile::r_string_wb, s_sHitPower, test );
	if ( result2 && !test )
	{
		string32 buffer;
		fvHitPowerCritical[egdMaster] = (float)atof(_GetItem(*s_sHitPowerCritical,0,buffer));
		fvHitPowerCritical[egdNovice] = fvHitPowerCritical[egdStalker] = fvHitPowerCritical[egdVeteran] = fvHitPowerCritical[egdMaster];

		int num_game_diff_param = _GetItemCount(*s_sHitPowerCritical);
		if ( num_game_diff_param > 1 ) { fvHitPowerCritical[egdVeteran]	= (float)atof(_GetItem(*s_sHitPowerCritical,1,buffer)); }
		if ( num_game_diff_param > 2 ) { fvHitPowerCritical[egdStalker]	= (float)atof(_GetItem(*s_sHitPowerCritical,2,buffer)); }
		if ( num_game_diff_param > 3 ) { fvHitPowerCritical[egdNovice]	= (float)atof(_GetItem(*s_sHitPowerCritical,3,buffer)); }
	}
	result |= result2;

	result |= process_if_exists( section, "hit_impulse",  &CInifile::r_float, fHitImpulse,         test );
	result |= process_if_exists( section, "bullet_speed", &CInifile::r_float, m_fStartBulletSpeed, test );

	/*	
	silencer_hit_power           = 0.55, 0.55, 0.55, 0.55
	silencer_hit_impulse         = 120
	silencer_fire_distance       = 600
	silencer_bullet_speed        = 310
	*/

	result |= process_if_exists_set( section, "use_aim_bullet",  &CInifile::r_bool, m_bUseAimBullet, test );
	if ( m_bUseAimBullet ) // first super bullet
	{
		result |= process_if_exists( section, "time_to_aim",  &CInifile::r_float, m_fTimeToAim, test );
	}

	LPCSTR weapon_section = cNameSect().c_str(); 
	float rpm =	pSettings->r_float( weapon_section, "rpm" ); // fOneShotTime * 60.0f;
	result2 = process_if_exists( section, "rpm", &CInifile::r_float, rpm, test );
	if ( result2 && !test )
	{
		VERIFY( rpm > 0.0f );
		fOneShotTime = 60.0f / rpm;
	}
	result |= result2;

	return result;
}

bool CWeapon::install_upgrade_addon( LPCSTR section, bool test )
{
	bool result = false;
	//LPCSTR weapon_section = cNameSect().c_str(); 

	// 0 - no addon // 1 - permanent // 2 - attachable
	int temp_int = (int)m_eScopeStatus;
	bool result2 = process_if_exists_set( section, "scope_status", &CInifile::r_s32, temp_int, test );
	if ( result2 && !test )
	{
		m_eScopeStatus = (ALife::EWeaponAddonStatus)temp_int;
		if ( m_eScopeStatus == ALife::eAddonAttachable || m_eScopeStatus == ALife::eAddonPermanent )
		{
			result |= process_if_exists( section, "holder_range_modifier", &CInifile::r_float, m_addon_holder_range_modifier, test );
			result |= process_if_exists( section, "holder_fov_modifier",   &CInifile::r_float, m_addon_holder_fov_modifier,   test );

			m_sScopeName	= pSettings->r_string( section, "scope_name" );

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

	}
	result |= result2;

	// GS foregrip_rail ("крепление для прицела") upgrade: adds extra scope section(s) -- e.g. scope_rakurs_ak74m --
	// that become mountable ONLY after this upgrade. The base weapon's scopes_sect lists every scope EXCEPT rakurs;
	// installing the rail appends rakurs. (Do it on the real install, not the test pass; dedup so repeated
	// installs/loads don't grow the list.)
	if ( !test && pSettings->line_exist( section, "scopes_sect" ) )
	{
		LPCSTR list = pSettings->r_string( section, "scopes_sect" );
		string256 one;
		for ( int i = 0, n = _GetItemCount(list); i < n; ++i )
		{
			_GetItem( list, i, one );
			if ( one[0] && std::find( m_scopes.begin(), m_scopes.end(), shared_str(one) ) == m_scopes.end() )
				m_scopes.push_back( one );
		}
		result = true;
	}

	temp_int = (int)m_eSilencerStatus;
	result2 = process_if_exists_set( section, "silencer_status", &CInifile::r_s32, temp_int, test );
	if ( result2 && !test )
	{
		m_eSilencerStatus = (ALife::EWeaponAddonStatus)temp_int;
		if ( m_eSilencerStatus == ALife::eAddonAttachable || m_eSilencerStatus == ALife::eAddonPermanent )
		{
			m_sSilencerName	= pSettings->r_string( section, "silencer_name" );

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
	}
	result |= result2;

	temp_int = (int)m_eGrenadeLauncherStatus;
	result2 = process_if_exists_set( section, "grenade_launcher_status", &CInifile::r_s32, temp_int, test );
	if ( result2 && !test )
	{
		m_eGrenadeLauncherStatus = (ALife::EWeaponAddonStatus)temp_int;
		if ( m_eGrenadeLauncherStatus == ALife::eAddonAttachable || m_eGrenadeLauncherStatus == ALife::eAddonPermanent )
		{
			m_sGrenadeLauncherName	= pSettings->r_string( section, "grenade_launcher_name" );

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
	}
	result |= result2;
	InitAddons();

	return result;
}
