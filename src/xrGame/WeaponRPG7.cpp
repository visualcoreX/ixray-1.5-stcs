#include "stdafx.h"
#include "weaponrpg7.h"
#include "xrserver_objects_alife_items.h"
#include "explosiverocket.h"
#include "entity.h"
#include "level.h"
#include "player_hud.h"
#include "HUDManager.h"

CWeaponRPG7::CWeaponRPG7()
{
	m_hud_missile_vis = -1;
}

CWeaponRPG7::~CWeaponRPG7() 
{
}

void CWeaponRPG7::Load	(LPCSTR section)
{
	inherited::Load						(section);
	CRocketLauncher::Load				(section);

	m_zoom_params.m_fScopeZoomFactor	= pSettings->r_float	(section,"max_zoom_factor");

	m_sRocketSection					= pSettings->r_string	(section,"rocket_class");
}

void CWeaponRPG7::FireTrace(const Fvector& P, const Fvector& D)
{
	inherited::FireTrace	(P, D);
	UpdateMissileVisibility	();
}

void CWeaponRPG7::UpdateMissileVisibility()
{
	bool vis_hud,vis_weap;
	vis_hud		= (!!iAmmoElapsed || GetState()==eReload);
	vis_weap	= !!iAmmoElapsed;

	if(GetHUDmode())
	{
		HudItemData()->set_bone_visible("grenade",vis_hud,TRUE);
		m_hud_missile_vis = vis_hud ? 1 : 0;
	}
	else
		m_hud_missile_vis = -1;		// our HUD model is not the attached one, so nothing was applied

	IKinematics* pWeaponVisual	= smart_cast<IKinematics*>(Visual());
	VERIFY						(pWeaponVisual);
	pWeaponVisual->LL_SetBoneVisible(pWeaponVisual->LL_BoneID("grenade"),vis_weap,TRUE);
}

// The rocket bone was only ever set from discrete events (OnStateSwitch, net_Import, fire, reload),
// and every one of them can run while GetHUDmode() is still FALSE -- it requires the HUD model to be
// attached, and the actor does that later, in its OWN UpdateCL. So the visibility set when the draw
// starts never reached the model.
// That alone was invisible, because the next event fixed it. What made it show is that
// `attachable_hud_item` is cached PER HUD SECTION: two RPG-7s share one HUD model. Fire one, holster
// it without reloading, draw a LOADED one -- the shared model still carried the empty tube from the
// first, and the draw animation played a loaded launcher with no rocket in it until eIdle came round.
// So re-assert it once the model really is ours. Cached, so this is a compare on all other frames.
void CWeaponRPG7::UpdateCL()
{
	inherited::UpdateCL();

	if(!GetHUDmode())		{ m_hud_missile_vis = -1; return; }
	const int want = (!!iAmmoElapsed || GetState()==eReload) ? 1 : 0;
	if(want != m_hud_missile_vis)
	{
		HudItemData()->set_bone_visible("grenade", !!want, TRUE);
		m_hud_missile_vis = want;
	}
}

BOOL CWeaponRPG7::net_Spawn(CSE_Abstract* DC) 
{
	BOOL l_res = inherited::net_Spawn(DC);

	UpdateMissileVisibility();
	if(iAmmoElapsed && !getCurrentRocket())
		CRocketLauncher::SpawnRocket(m_sRocketSection, this);

	return l_res;
}

void CWeaponRPG7::OnStateSwitch(u32 S) 
{
	inherited::OnStateSwitch(S);
	UpdateMissileVisibility();
}

void CWeaponRPG7::UnloadMagazine(bool spawn_ammo)
{
	inherited::UnloadMagazine	(spawn_ammo);
	UpdateMissileVisibility		();
}

void CWeaponRPG7::ReloadMagazine() 
{
	inherited::ReloadMagazine();

	if(iAmmoElapsed && !getRocketCount()) 
		CRocketLauncher::SpawnRocket(*m_sRocketSection, this);
}

void CWeaponRPG7::SwitchState(u32 S) 
{
	inherited::SwitchState(S);
}

void CWeaponRPG7::FireStart()
{
	if (SuicideBlocksFire())	return;		// GS OnShoot_CanShootNow, see CWeapon
	inherited::FireStart();
}

#include "inventory.h"
#include "inventoryOwner.h"
#include "level_bullet_manager.h"
#include "actor.h"
#include "../xrEngine/GameMtlLib.h"
extern void random_dir(Fvector& tgt_dir, const Fvector& src_dir, float dispersion);

// GS RPG7ReactiveHit (WeaponEvents.pas:2258): the BACKBLAST. Firing sprays `reactive_hit_buck` pellets
// straight out the BACK of the tube (that is why you must not shoot with a wall behind you); then, for
// each of them, a ray goes back and if it meets something inside reactive_hit_dist the blast bounces --
// `reactive_hit_reverse_buck` more pellets fly from that wall towards the shooter, their damage and reach
// scaled by how close the wall is. Actor-only, exactly like GS.
void CWeaponRPG7::ReactiveHit()
{
	if (H_Parent() && !ParentIsActor())	return;

	LPCSTR sect		= cNameSect().c_str();
	const float dist	= READ_IF_EXISTS(pSettings, r_float, sect, "reactive_hit_dist",    0.f);
	const float hit		= READ_IF_EXISTS(pSettings, r_float, sect, "reactive_hit_power",   0.f);
	const float impulse	= READ_IF_EXISTS(pSettings, r_float, sect, "reactive_hit_impulse", 0.f);
	const int   buck	= (int)READ_IF_EXISTS(pSettings, r_u32, sect, "reactive_hit_buck",  1);
	if (dist <= 0.f || hit <= 0.f || impulse <= 0.f || buck <= 0)	return;

	const int   rbuck	= (int)READ_IF_EXISTS(pSettings, r_u32,   sect, "reactive_hit_reverse_buck",  1);
	const float bdisp	= READ_IF_EXISTS(pSettings, r_float, sect, "reactive_hit_buck_disp",      1.f);
	const float rdisp	= READ_IF_EXISTS(pSettings, r_float, sect, "reactive_hit_reverse_disp",   0.1f);
	const float rdisp2	= READ_IF_EXISTS(pSettings, r_float, sect, "reactive_hit_reverse_disp2",  0.1f);
	const float rhit	= READ_IF_EXISTS(pSettings, r_float, sect, "reactive_hit_reverse_power",  hit);
	const float revk	= READ_IF_EXISTS(pSettings, r_float, sect, "reactive_hit_reverse_k",      1.f);
	const ALife::EHitType htype = (ALife::EHitType)READ_IF_EXISTS(pSettings, r_u32, sect, "reactive_hit_type",
																  (u32)ALife::eHitTypeExplosion);
	LPCSTR mtl			= READ_IF_EXISTS(pSettings, r_string, sect, "reactive_hit_bullet_material", "default");

	Fvector pos = get_LastFP();
	Fvector dir = get_LastFD();
	dir.mul		(-1.f);					// out the back of the tube

	CCartridge c;
	c.param_s.kDist = c.param_s.kHit = c.param_s.kCritical = c.param_s.kImpulse = c.param_s.kAP = 1.f;
	c.bullet_material_idx = GMLib.GetMaterialIdx(mtl);
	c.m_flags.set(CCartridge::cfTracer, FALSE);

	const u16 parent_id = H_Parent() ? H_Parent()->ID() : ID();

	for (int i = 0; i < buck; ++i)
	{
		Fvector tgt;
		random_dir(tgt, dir, bdisp);
		Level().BulletManager().AddBullet(pos, tgt, 330.f, hit, 0.f, impulse, parent_id, ID(),
										  htype, dist, c, true);

		// the reverse wave: what does the blast hit behind us, and how close is it?
		random_dir(tgt, dir, rdisp);
		collide::rq_result RQ;
		if (!Level().ObjectSpace.RayPick(pos, tgt, dist, collide::rqtStatic, RQ, H_Parent()))
			continue;

		for (int j = 0; j < rbuck; ++j)
		{
			Fvector point = pos, d2 = tgt;
			d2.mul		(RQ.range * 0.9f);
			point.add	(d2);				// just in front of the wall
			d2.sub		(pos, point);
			d2.normalize_safe();
			Fvector tgt2;
			random_dir	(tgt2, d2, rdisp2);

			float rest = dist - RQ.range;	// the closer the wall, the more comes back
			if (rest < 0.f)	rest = 0.f;
			const float rhit_cur = rhit * rest / dist;
			const float rdist    = dist * revk * (rest / dist) * (0.9f + ::Random.randF(0.15f));
			Level().BulletManager().AddBullet(point, tgt2, 330.f, rhit_cur, 0.f, impulse, ID(), ID(),
											  htype, rdist, c, true);
		}
	}
}

// GS CheckRLHasActiveRocket (WeaponUpdate.pas:748): a worn launcher can set the rocket off IN THE TUBE.
// Probability ramps between rocket_misfunc_start/end_condition; on a hit the rocket detonates at the
// weapon instead of flying, and the tube is emptied. Actor-only, like GS.
bool CWeaponRPG7::RocketMisfunction()
{
	if (!ParentIsActor())	return false;

	LPCSTR sect		 = cNameSect().c_str();
	const float st_c = READ_IF_EXISTS(pSettings, r_float, sect, "rocket_misfunc_start_condition",   0.f);
	const float en_c = READ_IF_EXISTS(pSettings, r_float, sect, "rocket_misfunc_end_condition",     0.f);
	const float st_p = READ_IF_EXISTS(pSettings, r_float, sect, "rocket_misfunc_start_probability", 0.f);
	const float en_p = READ_IF_EXISTS(pSettings, r_float, sect, "rocket_misfunc_end_probability",   0.f);
	const float cond = GetCondition();
	if (cond > st_c || fsimilar(st_c, en_c))	return false;

	const float prob = (cond < en_c) ? en_p : st_p + (en_p - st_p) * (st_c - cond) / (st_c - en_c);
	if (prob <= 0.f || ::Random.randF(1.f) >= prob)	return false;

	CExplosiveRocket* r = smart_cast<CExplosiveRocket*>(getCurrentRocket());
	if (!r)	return false;

	Fvector p = Position(), n;
	n.set(0.f, 1.f, 0.f);
	r->SetInitiator		(H_Parent()->ID());
	DetachRocket		(r->ID(), true);
	r->Contact			(p, n);
	UnloadMagazine		(false);
	return true;
}

void CWeaponRPG7::switch2_Fire()
{
	m_iShotNum			= 0;
	m_bFireSingleShot	= true;
	bWorking			= false;

	if(GetState()==eFire && getRocketCount()) 
	{
		Fvector p1, d1, p; 
		Fvector p2, d2, d; 
		p1.set								(get_LastFP()); 
		d1.set								(get_LastFD());
		p = p1;
		d = d1;
		// The muzzle point is only trustworthy while the HUD model drives it: with no HUD frame
		// UpdateFireDependencies falls back to the weapon's WORLD transform, which for a weapon in hand
		// can be stale (a rocket then departs from wherever that transform last was). Sanity-check it
		// against the holder before trusting it, and fall back to the camera params otherwise.
		Fvector ref = H_Parent() ? H_Parent()->Position() : Position();
		const bool muzzle_ok = GetHUDmode() && HudItemData() && p1.distance_to(ref) < 3.0f;

		CEntity* E = smart_cast<CEntity*>	(H_Parent());
		if(E)
		{
			// GS CWeaponRPG7__FireStart_need_skip_g_fireParams (WeaponEvents.pas:2211): for the ACTOR the
			// launch params come from the camera ONLY while aiming. From the hip the rocket leaves the tube
			// and flies where the tube POINTS -- GS skips g_fireParams entirely there, so there is no
			// convergence onto the crosshair. NPCs keep the engine params: their aim comes from the AI.
			// A controller suicide is ALWAYS a hip shot in this sense: the rocket has to leave where the
			// tube points, which is what the hud suicide offset has just spent seconds arranging. The
			// plain hip test is not enough -- the scene starts with OnZoomOut, so IsRotatingToZoom is
			// still true and the launch fell back to the camera (log: hip=0 muzzle_ok=1 zoom=0, the
			// rocket flew along the look direction instead of the tube).
			const bool suicide_shot = ParentIsActor() && Actor() && Actor()->IsSuicideInProgress() && muzzle_ok;
			const bool actor_hip = suicide_shot ||
								   (ParentIsActor() && !IsZoomed() && !IsRotatingToZoom() && muzzle_ok);
			if (!actor_hip)
			{
				E->g_fireParams				(this, p2,d2);
				d = d2;						// aiming: fly along the sight line...
				p = (ParentIsActor() && muzzle_ok) ? p1 : p2;	// ...but out of the tube when it is sane
			}
		}

		Fmatrix								launch_matrix;
		launch_matrix.identity				();
		launch_matrix.k.set					(d);
		Fvector::generate_orthonormal_basis(launch_matrix.k,
											launch_matrix.j, launch_matrix.i);
		launch_matrix.c.set					(p);

		d.normalize							();
		d.mul								(m_fLaunchSpeed);

		ReactiveHit							();		// GS: the backblast goes off with the shot

		if (RocketMisfunction())			// a worn launcher may detonate the rocket in the tube instead
		{
			m_bFireSingleShot	= false;
			bWorking			= false;
			SwitchState			(eIdle);
			return;
		}

		CRocketLauncher::LaunchRocket		(launch_matrix, d, zero_vel);

		CExplosiveRocket* pGrenade			= smart_cast<CExplosiveRocket*>(getCurrentRocket());
		VERIFY								(pGrenade);
		pGrenade->SetInitiator				(H_Parent()->ID());

		if (OnServer())
		{
			NET_Packet						P;
			u_EventGen						(P,GE_LAUNCH_ROCKET,ID());
			P.w_u16							(u16(getCurrentRocket()->ID()));
			u_EventSend						(P);
		}
	}
}

void CWeaponRPG7::OnEvent(NET_Packet& P, u16 type) 
{
	inherited::OnEvent(P,type);
	u16 id;
	switch (type) {
		case GE_OWNERSHIP_TAKE : {
			P.r_u16(id);
			CRocketLauncher::AttachRocket(id, this);
		} break;
		case GE_OWNERSHIP_REJECT:
		case GE_LAUNCH_ROCKET	: 
			{
			bool bLaunch = (type==GE_LAUNCH_ROCKET);
			P.r_u16(id);
			CRocketLauncher::DetachRocket(id, bLaunch);
			if(bLaunch)
				UpdateMissileVisibility();
		} break;
	}
}

void CWeaponRPG7::net_Import( NET_Packet& P)
{
	inherited::net_Import		(P);
	UpdateMissileVisibility		();
}
