// WeaponFire.cpp: implementation of the CWeapon class.
// function responsible for firing with CWeapon
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Weapon.h"
//.#include "WeaponHUD.h"
#include "ParticlesObject.h"
#include "HUDManager.h"
#include "entity.h"
#include "actor.h"

#include "actoreffector.h"
#include "effectorshot.h"

#include "level_bullet_manager.h"

#define FLAME_TIME 0.05f


float _nrand(float sigma)
{
#define ONE_OVER_SIGMA_EXP (1.0f / 0.7975f)

	if(sigma == 0) return 0;

	float y;
	do{
		y = -logf(Random.randF());
	}while(Random.randF() > expf(-_sqr(y - 1.0f)*0.5f));
	if(rand() & 0x1)	return y * sigma * ONE_OVER_SIGMA_EXP;
	else				return -y * sigma * ONE_OVER_SIGMA_EXP;
}

void random_dir(Fvector& tgt_dir, const Fvector& src_dir, float dispersion)
{
	float sigma			= dispersion/3.f;
	float alpha			= clampr		(_nrand(sigma),-dispersion,dispersion);
	float theta			= Random.randF	(0,PI);
	float r 			= tan			(alpha);
	Fvector 			U,V,T;
	Fvector::generate_orthonormal_basis	(src_dir,U,V);
	U.mul				(r*_sin(theta));
	V.mul				(r*_cos(theta));
	T.add				(U,V);
	tgt_dir.add			(src_dir,T).normalize();
}

float CWeapon::GetWeaponDeterioration	()
{
	return conditionDecreasePerShot;
};

void CWeapon::FireTrace		(const Fvector& P, const Fvector& D)
{
	VERIFY		(m_magazine.size());

	CCartridge &l_cartridge = m_magazine.back();
//	Msg("ammo - %s", l_cartridge.m_ammoSect.c_str());
	VERIFY		(u16(-1) != l_cartridge.bullet_material_idx);
	//-------------------------------------------------------------	
	l_cartridge.m_flags.set				(CCartridge::cfTracer,(m_bHasTracers & !!l_cartridge.m_flags.test(CCartridge::cfTracer)));
	if (m_u8TracerColorID != u8(-1))
		l_cartridge.param_s.u8ColorID	= m_u8TracerColorID;
	//-------------------------------------------------------------
	//�������� ������������ ������ � ������ ������� ����������� �������
//	float Deterioration = GetWeaponDeterioration();
//	Msg("Deterioration = %f", Deterioration);
	ChangeCondition(-GetWeaponDeterioration()*l_cartridge.param_s.impair);

	
	float fire_disp = 0.f;
	CActor* tmp_actor = NULL;
	if (!IsGameTypeSingle())
	{
		tmp_actor = smart_cast<CActor*>(Level().CurrentControlEntity());
		if (tmp_actor)
		{
			CEntity::SEntityState state;
			tmp_actor->g_State(state);
			if (m_first_bullet_controller.is_bullet_first(state.fVelocity))
			{
				fire_disp = m_first_bullet_controller.get_fire_dispertion();
				m_first_bullet_controller.make_shot();
			}
		}
	}
	if (fsimilar(fire_disp, 0.f))
	{
		//CActor* tmp_actor = smart_cast<CActor*>(Level().CurrentControlEntity());
		if (UseBaseFireDispersion())
		{
			// AN-94 hyperburst: the rounds fired inside the fast part of the queue ignore the
			// shooter's accumulated (recoil) dispersion and use the barrel's own cone, so they
			// land in one hole. Vanilla SoC did this through CWeaponMagazined::GetFireDispersion.
			fire_disp = GetBaseDispersion(l_cartridge.param_s.kDisp);
		} else
		if (H_Parent() && (H_Parent() == tmp_actor))
		{
			fire_disp = tmp_actor->GetFireDispertion();
		} else
		{
			fire_disp = GetFireDispersion(true);
		}
	}
	

	bool SendHit = SendHitAllowed(H_Parent());
	//���������� ���� (� ������ ��������� �������� ������)
	for(int i = 0; i < l_cartridge.param_s.buckShot; ++i) 
	{
		FireBullet(P, D, fire_disp, l_cartridge, H_Parent()->ID(), ID(), SendHit);
	}

	StartShotParticles		();
	
	if(m_bLightShotEnabled) 
		Light_Start			();

	
	// Ammo
	m_magazine.pop_back	();
	--iAmmoElapsed;

	VERIFY((u32)iAmmoElapsed == m_magazine.size());
}

void CWeapon::StopShooting()
{
//	SetPending			(TRUE);

	//������������� ������������� ����������� ��������
	if(m_pFlameParticles && m_pFlameParticles->IsLooped())
		StopFlameParticles	();	

	SwitchState(eIdle);

	bWorking = false;
}

void CWeapon::FireEnd()
{
	CShootingObject::FireEnd();
	StopShotEffector();
}

// GS OnShoot_CanShootNow (WeaponAdditionalBuffer.pas:1006): once a controller has the actor in a
// suicide scene, his own trigger does nothing -- GS returns IsSuicideInreversible() there, so the
// only shot allowed through is the scene's own, which flags itself irreversible before it fires.
// Everyone else (NPCs, and the actor outside a scene) is untouched.
bool CWeapon::SuicideBlocksFire() const
{
	CActor* act = smart_cast<CActor*>(const_cast<CWeapon*>(this)->H_Parent());
	if (!act || act != Actor())							return false;
	if (act->IsSuicideIrreversible())					return false;	// the scene's own shot
	return act->IsSuicideInProgress();
}

// The victim is holding the weapon to his own head: the pose belongs to the scene until it resolves,
// so nothing may replace it with an idle. GS gets this for free -- IsActionProcessing is true for the
// whole scene, so no idle can be started.
bool CWeapon::SuicideHoldsPose() const
{
	CActor* act = smart_cast<CActor*>(const_cast<CWeapon*>(this)->H_Parent());
	return act && act == Actor() && act->SuicideHoldsWeaponPose();
}

// GS CanAimNow (WeaponAdditionalBuffer.pas:900) is wider than OnShoot_CanShootNow: it also refuses
// while the psi attack is merely WINDING UP (IsControllerPreparing), before the grab lands.
bool CWeapon::SuicideBlocksAim() const
{
	if (SuicideBlocksFire())							return true;
	CActor* act = smart_cast<CActor*>(const_cast<CWeapon*>(this)->H_Parent());
	return act && act == Actor() && act->IsControllerPreparing();
}


void CWeapon::StartFlameParticles2	()
{
	CShootingObject::StartParticles (m_pFlameParticles2, *m_sFlameParticles2, get_LastFP2());
}
void CWeapon::StopFlameParticles2	()
{
	CShootingObject::StopParticles (m_pFlameParticles2);
}
void CWeapon::UpdateFlameParticles2	()
{
	if (m_pFlameParticles2)			CShootingObject::UpdateParticles (m_pFlameParticles2, get_LastFP2());
}
