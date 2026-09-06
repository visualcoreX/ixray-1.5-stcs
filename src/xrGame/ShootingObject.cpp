//////////////////////////////////////////////////////////////////////
// ShootingObject.cpp:  ��������� ��� ��������� ���������� �������� 
//						(������ � ���������� �������) 	
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "ShootingObject.h"

#include "ParticlesObject.h"
#include "WeaponAmmo.h"

#include "actor.h"
#include "spectator.h"
#include "game_cl_base.h"
#include "level.h"
#include "level_bullet_manager.h"
#include "game_cl_single.h"

#define HIT_POWER_EPSILON 0.05f
#define WALLMARK_SIZE 0.04f

CShootingObject::CShootingObject(void)
{
	fShotTimeCounter							= 0;
 	fOneShotTime						= 0;
	//fHitPower						= 0.0f;
	fvHitPower.set					(0.0f,0.0f,0.0f,0.0f);
	fvHitPowerCritical.set			(0.0f,0.0f,0.0f,0.0f);
	m_fStartBulletSpeed				= 1000.f;

	m_vCurrentShootDir.set			(0,0,0);
	m_vCurrentShootPos.set			(0,0,0);
	m_iCurrentParentID				= 0xFFFF;

	m_fPredBulletTime				= 0.0f;
	m_bUseAimBullet					= false;
	m_fTimeToAim					= 0.0f;

	//particles
	m_sFlameParticlesCurrent		= m_sFlameParticles = NULL;
	m_sSmokeParticlesCurrent		= m_sSmokeParticles = NULL;
	m_sShellParticles				= NULL;
	
	bWorking						= false;

	light_render					= 0;

	reinit();

}
CShootingObject::~CShootingObject(void)
{
}

void CShootingObject::reinit()
{
	m_pFlameParticles	= NULL;
}

void CShootingObject::Load	(LPCSTR section)
{
	if(pSettings->line_exist(section,"light_disabled"))
	{
		m_bLightShotEnabled		= !pSettings->r_bool(section,"light_disabled");
	}else
		m_bLightShotEnabled		= true;

	//����� ������������� �� �������
	fOneShotTime			= pSettings->r_float		(section,"rpm");
	VERIFY(fOneShotTime>0.f);
	fOneShotTime			= 60.f / fOneShotTime;

	LoadFireParams		(section);
	LoadLights			(section, "");
	LoadShellParticles	(section, "");
	LoadFlameParticles	(section, "");
}

void CShootingObject::Light_Create		()
{
	//lights
	light_render				=	::Render->light_create();
	if (::Render->get_generation()==IRender_interface::GENERATION_R2)	light_render->set_shadow	(true);
	else																light_render->set_shadow	(false);
	// A muzzle flash is a SHADOWING light sitting in the fire point -- that is, inside the shooter.
	// With the actor injected into shadow maps (r__actor_shadow) his own body lands in this light's
	// map and throws its silhouette across the very hands the flash is lighting: a shadow that
	// flickers onto the weapon with every shot. Same call, and the same reason, as the torch, the
	// handheld detector lamp and the barrel-mounted flashlight already make.
	light_render->set_actor_shadow	(false);
}

void CShootingObject::Light_Destroy		()
{
	light_render.destroy		();
}

void CShootingObject::LoadFireParams( LPCSTR section )
{
	string32	buffer;
	shared_str	s_sHitPower;
	shared_str	s_sHitPowerCritical;

	//������� ��������� ������
	fireDispersionBase	= deg2rad( pSettings->r_float	(section,"fire_dispersion_base"	) );

	//���� �������� � ��� ���������
	s_sHitPower			= pSettings->r_string_wb(section, "hit_power" );//������ ������ ���� ���� ���� ������
	s_sHitPowerCritical	= pSettings->r_string_wb(section, "hit_power_critical" );
	fvHitPower[egdMaster]			= (float)atof(_GetItem(*s_sHitPower,0,buffer));//������ �������� - ��� ��� ��� ������ ���� ������
	fvHitPowerCritical[egdMaster]	= (float)atof(_GetItem(*s_sHitPowerCritical,0,buffer));//������ �������� - ��� ��� ��� ������ ���� ������

	fvHitPower[egdNovice] = fvHitPower[egdStalker] = fvHitPower[egdVeteran] = fvHitPower[egdMaster];//���������� ��������� ��� ������ ������� ��������� ����� ��
	fvHitPowerCritical[egdNovice] = fvHitPowerCritical[egdStalker] = fvHitPowerCritical[egdVeteran] = fvHitPowerCritical[egdMaster];//���������� ��������� ��� ������ ������� ��������� ����� ��

	int num_game_diff_param=_GetItemCount(*s_sHitPower);//����� ����������� ���������� ��� �����
	if (num_game_diff_param>1)//���� ����� ������ �������� ����
	{
		fvHitPower[egdVeteran]	= (float)atof(_GetItem(*s_sHitPower,1,buffer));//�� ���������� ��� ��� ������ ��������
	}
	if (num_game_diff_param>2)//���� ����� ������ �������� ����
	{
		fvHitPower[egdStalker]	= (float)atof(_GetItem(*s_sHitPower,2,buffer));//�� ���������� ��� ��� ������ ��������
	}
	if (num_game_diff_param>3)//���� ����� �������� �������� ����
	{
		fvHitPower[egdNovice]	= (float)atof(_GetItem(*s_sHitPower,3,buffer));//�� ���������� ��� ��� ������ �������
	}

	num_game_diff_param=_GetItemCount(*s_sHitPowerCritical);//����� ����������� ����������
	if (num_game_diff_param>1)//���� ����� ������ �������� ����
	{
		fvHitPowerCritical[egdVeteran]	= (float)atof(_GetItem(*s_sHitPowerCritical,1,buffer));//�� ���������� ��� ��� ������ ��������
	}
	if (num_game_diff_param>2)//���� ����� ������ �������� ����
	{
		fvHitPowerCritical[egdStalker]	= (float)atof(_GetItem(*s_sHitPowerCritical,2,buffer));//�� ���������� ��� ��� ������ ��������
	}
	if (num_game_diff_param>3)//���� ����� �������� �������� ����
	{
		fvHitPowerCritical[egdNovice]	= (float)atof(_GetItem(*s_sHitPowerCritical,3,buffer));//�� ���������� ��� ��� ������ �������
	}

	fHitImpulse			= pSettings->r_float	(section, "hit_impulse" );
	//������������ ���������� ������ ����
	fireDistance		= pSettings->r_float	(section, "fire_distance" );
	//��������� �������� ����
	m_fStartBulletSpeed = pSettings->r_float	(section, "bullet_speed" );
	m_bUseAimBullet		= pSettings->r_bool		(section, "use_aim_bullet" );
	if (m_bUseAimBullet)
	{
		m_fTimeToAim		= pSettings->r_float	(section, "time_to_aim" );
	}
}

void CShootingObject::LoadLights		(LPCSTR section, LPCSTR prefix)
{
	string256				full_name;
	// light
	if(m_bLightShotEnabled) 
	{
		Fvector clr			= pSettings->r_fvector3		(section, strconcat(sizeof(full_name),full_name, prefix, "light_color"));
		light_base_color.set(clr.x,clr.y,clr.z,1);
		light_base_range	= pSettings->r_float		(section, strconcat(sizeof(full_name),full_name, prefix, "light_range")		);
		light_var_color		= pSettings->r_float		(section, strconcat(sizeof(full_name),full_name, prefix, "light_var_color")	);
		light_var_range		= pSettings->r_float		(section, strconcat(sizeof(full_name),full_name, prefix, "light_var_range")	);
		light_lifetime		= pSettings->r_float		(section, strconcat(sizeof(full_name),full_name, prefix, "light_time")		);
		light_time			= -1.f;
	}
}

void CShootingObject::Light_Start	()
{
	if(!light_render)		Light_Create();

	// Mark it HERE as well as in Light_Render: this runs on every shot, whereas Light_Render only
	// runs off the world model's render path, which the player's own weapon does not go through
	// while it is drawn as the hud -- so the mark never reached the light that was actually lighting
	// (and self-shadowing) the hands. See phase_hud_shadow.
	light_render->set_inside_hud(!!ParentIsActor());

	if (Device.dwFrame	!= light_frame)
	{
		light_frame					= Device.dwFrame;
		light_time					= light_lifetime;
		
		light_build_color.set		(Random.randFs(light_var_color,light_base_color.r),Random.randFs(light_var_color,light_base_color.g),Random.randFs(light_var_color,light_base_color.b),1);
		light_build_range			= Random.randFs(light_var_range,light_base_range);
	}
}

void CShootingObject::Light_Render	(const Fvector& P)
{
	float light_scale			= light_time/light_lifetime;
	R_ASSERT(light_render);

	light_render->set_position	(P);
	// The player's own muzzle flash sits in the fire point, i.e. INSIDE the hud models -- it must
	// not drive their screen-space contact shadow (see phase_hud_shadow, it would black the weapon
	// out). An NPC firing across the road is an ordinary world light and keeps lighting them.
	// Test the OWNER, not GetHUDmode(): the hud flag is only true while the hud is actually being
	// drawn for this item, and the flash fires on frames where that is not yet the case, so the
	// mark slipped through and the shot still darkened the weapon.
	light_render->set_inside_hud(!!ParentIsActor());
	light_render->set_color		(light_build_color.r*light_scale,light_build_color.g*light_scale,light_build_color.b*light_scale);
	light_render->set_range		(light_build_range*light_scale);

	if(	!light_render->get_active() )
	{
		light_render->set_active	(true);
	}
}


//////////////////////////////////////////////////////////////////////////
// Particles
//////////////////////////////////////////////////////////////////////////

void CShootingObject::StartParticles (CParticlesObject*& pParticles, LPCSTR particles_name, 
									 const Fvector& pos, const  Fvector& vel, bool auto_remove_flag, bool force_world)
{
	if(!particles_name) return;

	if(pParticles != NULL) 
	{
		UpdateParticles(pParticles, pos, vel);
		return;
	}

	pParticles = CParticlesObject::Create(particles_name,(BOOL)auto_remove_flag);
	
	UpdateParticles(pParticles, pos, vel);
	CSpectator* tmp_spectr = smart_cast<CSpectator*>(Level().CurrentControlEntity());
	bool in_hud_mode = !force_world && IsHudModeNow();
	if (in_hud_mode && tmp_spectr &&
		(tmp_spectr->GetActiveCam() != CSpectator::eacFirstEye))
	{
		in_hud_mode = false;
	}
	pParticles->Play(in_hud_mode);
}
void CShootingObject::StopParticles (CParticlesObject*&	pParticles)
{
	if(pParticles == NULL) return;

	pParticles->Stop		();
	CParticlesObject::Destroy(pParticles);
}

void CShootingObject::UpdateParticles (CParticlesObject*& pParticles, 
							   const Fvector& pos, const Fvector& vel)
{
	if(!pParticles)		return;

	Fmatrix particles_pos; 
	particles_pos.set	(get_ParticlesXFORM());
	particles_pos.c.set	(pos);
	
	pParticles->SetXFORM(particles_pos);

	if(!pParticles->IsAutoRemove() && !pParticles->IsLooped() 
		&& !pParticles->PSI_alive())
	{
		pParticles->Stop		();
		CParticlesObject::Destroy(pParticles);
	}
}


void CShootingObject::LoadShellParticles (LPCSTR section, LPCSTR prefix)
{
	string256 full_name;
	strconcat(sizeof(full_name),full_name, prefix, "shell_particles");

	if(pSettings->line_exist(section,full_name)) 
	{
		m_sShellParticles	= pSettings->r_string	(section,full_name);
		vLoadedShellPoint	= pSettings->r_fvector3	(section,strconcat(sizeof(full_name),full_name, prefix, "shell_point"));
	}
}

void CShootingObject::LoadFlameParticles (LPCSTR section, LPCSTR prefix)
{
	string256 full_name;

	// flames
	strconcat(sizeof(full_name),full_name, prefix, "flame_particles");
	if(pSettings->line_exist(section, full_name))
		m_sFlameParticles	= pSettings->r_string (section, full_name);

	strconcat(sizeof(full_name),full_name, prefix, "smoke_particles");
	if(pSettings->line_exist(section, full_name))
		m_sSmokeParticles = pSettings->r_string (section, full_name);

	strconcat(sizeof(full_name),full_name, prefix, "shot_particles");
	if(pSettings->line_exist(section, full_name))
		m_sShotParticles = pSettings->r_string (section, full_name);


	//������� ��������
	m_sFlameParticlesCurrent = m_sFlameParticles;
	m_sSmokeParticlesCurrent = m_sSmokeParticles;
}


void CShootingObject::OnShellDrop	(const Fvector& play_pos,
									 const Fvector& parent_vel)
{
	if(!m_sShellParticles) return;
	if( Device.vCameraPosition.distance_to_sqr(play_pos)>2*2 ) return;

	CParticlesObject* pShellParticles	= CParticlesObject::Create(*m_sShellParticles,TRUE);

	Fmatrix particles_pos; 
	particles_pos.set		(get_ParticlesXFORM());
	particles_pos.c.set		(play_pos);

	pShellParticles->UpdateParent		(particles_pos, parent_vel);
	CSpectator* tmp_spectr = smart_cast<CSpectator*>(Level().CurrentControlEntity());
	bool in_hud_mode = IsHudModeNow();
	if (in_hud_mode && tmp_spectr &&
		(tmp_spectr->GetActiveCam() != CSpectator::eacFirstEye))
	{
		in_hud_mode = false;
	}
	pShellParticles->Play(in_hud_mode);
}


//�������� ����
extern ENGINE_API float psHUD_FOV;	// hud fov as a fraction of the world fov

// The muzzle FLAME is a hud particle and the SMOKE is a world one (see StartSmokeParticles), so the
// same muzzle position goes through two different projections and the two effects come apart on
// screen -- the smoke reading as sitting out in front of the barrel.
// Remap the point so that, drawn in the WORLD projection, it appears where the HUD projection draws
// it. A point's screen offset is perp / (along * tan(fov/2)), so the depth along the view axis is
// kept (occlusion and sorting unchanged) and only the camera-perpendicular part is scaled by
// tan(world/2) / tan(hud/2). The world fov is the wider one, so the point moves further off axis.
// Same correction as LaserCorrectPointWorldToHud in Weapon.cpp, which exists for the same reason.
// NOTE scaling the camera->muzzle DISTANCE instead does nothing for this: it leaves the direction
// from the camera untouched, so the point keeps the very screen position that was wrong.
static void SmokePointWorldToHud(Fvector& p)
{
	const float t_hud = tanf(deg2rad(0.5f * psHUD_FOV * Device.fFOV));
	const float t_wld = tanf(deg2rad(0.5f * Device.fFOV));
	if (t_hud <= EPS_L || t_wld <= EPS_L)	return;

	const Fvector& cpos = Device.vCameraPosition;
	const Fvector& cdir = Device.vCameraDirection;
	Fvector v;		v.sub(p, cpos);
	const float along = v.dotproduct(cdir);
	if (along <= EPS_L)	return;				// behind the camera: leave it alone

	Fvector par;	par.mul(cdir, along);	// depth component, preserved
	Fvector perp;	perp.sub(v, par);		// screen-plane component, rescaled
	perp.mul		(t_wld / t_hud);
	p.add			(cpos, par);
	p.add			(perp);
}

void CShootingObject::StartSmokeParticles	(const Fvector& play_pos,
											const Fvector& parent_vel)
{
	CParticlesObject* pSmokeParticles = NULL;
	// World effect, not a hud one: powder smoke hangs in the air where the shot happened. As a hud
	// particle it was drawn in the hud viewport -- scaled by hud_fov and swinging with every camera
	// move, as if the cloud were glued to the screen.
	Fvector pos = play_pos;
	// Only the player's own weapon is drawn as a hud, and only there do the two projections differ.
	// An NPC's weapon, a car, a helicopter and a mounted gun are world-rendered throughout.
	if (IsHudModeNow())	SmokePointWorldToHud(pos);
	StartParticles(pSmokeParticles, *m_sSmokeParticlesCurrent, pos, parent_vel, true, true);
}


void CShootingObject::StartFlameParticles	()
{
	if(0==m_sFlameParticlesCurrent.size()) return;

	//���� �������� �����������
	if(m_pFlameParticles && m_pFlameParticles->IsLooped() && 
		m_pFlameParticles->IsPlaying()) 
	{
		UpdateFlameParticles();
		return;
	}

	StopFlameParticles();
	m_pFlameParticles = CParticlesObject::Create(*m_sFlameParticlesCurrent,FALSE);
	UpdateFlameParticles();
	
	
	CSpectator* tmp_spectr = smart_cast<CSpectator*>(Level().CurrentControlEntity());
	bool in_hud_mode = IsHudModeNow();
	if (in_hud_mode && tmp_spectr &&
		(tmp_spectr->GetActiveCam() != CSpectator::eacFirstEye))
	{
		in_hud_mode = false;
	}
	m_pFlameParticles->Play(in_hud_mode);
		

}
void CShootingObject::StopFlameParticles	()
{
	if(0==m_sFlameParticlesCurrent.size()) return;
	if(m_pFlameParticles == NULL) return;

	m_pFlameParticles->SetAutoRemove(true);
	m_pFlameParticles->Stop();
	m_pFlameParticles = NULL;
}

void CShootingObject::UpdateFlameParticles	()
{
	if(0==m_sFlameParticlesCurrent.size())		return;
	if(!m_pFlameParticles)				return;

	Fmatrix		pos; 
	pos.set		(get_ParticlesXFORM()	); 
	pos.c.set	(get_CurrentFirePoint()	);

	VERIFY(_valid(pos));

	m_pFlameParticles->SetXFORM			(pos);

	if(!m_pFlameParticles->IsLooped() && 
		!m_pFlameParticles->IsPlaying() &&
		!m_pFlameParticles->PSI_alive())
	{
		m_pFlameParticles->Stop();
		CParticlesObject::Destroy(m_pFlameParticles);
	}
}

//��������� �� ��������
void CShootingObject::UpdateLight()
{
	if (light_render && light_time>0)		
	{
		light_time -= Device.fTimeDelta;
		if (light_time<=0) StopLight();
	}
}

void CShootingObject::StopLight			()
{
	if(light_render){
		light_render->set_active(false);
	}
}

void CShootingObject::RenderLight()
{
	if ( light_render && light_time>0 ) 
	{
		Light_Render(get_CurrentFirePoint());
	}
}

bool CShootingObject::SendHitAllowed		(CObject* pUser)
{
	if (Game().IsServerControlHits())
		return OnServer();

	if (OnServer())
	{
		if (smart_cast<CActor*>(pUser))
		{
			if (Level().CurrentControlEntity() != pUser)
			{
				return false;
			}
		}
		return true;
	}
	else
	{
		if (smart_cast<CActor*>(pUser))
		{
			if (Level().CurrentControlEntity() == pUser)
			{
				return true;
			}
		}
		return false;
	}
};

extern void random_dir(Fvector& tgt_dir, const Fvector& src_dir, float dispersion);

void CShootingObject::FireBullet(const Fvector& pos, 
								 const Fvector& shot_dir, 
								 float fire_disp,
								 const CCartridge& cartridge,
								 u16 parent_id,
								 u16 weapon_id,
								 bool send_hit)
{
	Fvector dir;
	random_dir(dir,shot_dir,fire_disp);

	m_vCurrentShootDir = dir;
	m_vCurrentShootPos = pos;
	m_iCurrentParentID = parent_id;
	
	bool aim_bullet;
	if (m_bUseAimBullet)
	{
		if (ParentMayHaveAimBullet())
		{
			if (m_fPredBulletTime==0.0)
			{
				aim_bullet=true;
			}
			else
			{
				if ((Device.fTimeGlobal-m_fPredBulletTime)>=m_fTimeToAim)
				{
					aim_bullet=true;
				}
				else
				{
					aim_bullet=false;
				}
			}
		}
		else
		{
			aim_bullet=false;
		}
	}
	else
	{
		aim_bullet=false;
	}
	m_fPredBulletTime = Device.fTimeGlobal;

	float l_fHitPower = 0.0f;
	float l_fHitPowerCritical = 0.0f;
	if (ParentIsActor())//���� �� ������ �������� ����(�����)
	{
		if (GameID() == eGameIDSingle)
		{
			l_fHitPower			= fvHitPower[g_SingleGameDifficulty];
			l_fHitPowerCritical = fvHitPowerCritical[g_SingleGameDifficulty];
		}
		else
		{
			l_fHitPower			= fvHitPower[egdMaster];
			l_fHitPowerCritical = fvHitPowerCritical[egdMaster];
		}
	}
	else
	{
		l_fHitPower			= fvHitPower[egdMaster];
		l_fHitPowerCritical = fvHitPowerCritical[egdMaster];
	}

	Level().BulletManager().AddBullet( pos, dir,
		m_fStartBulletSpeed * cur_silencer_koef.bullet_speed,
		l_fHitPower * cur_silencer_koef.hit_power,
		l_fHitPowerCritical,
		fHitImpulse * cur_silencer_koef.hit_impulse,
		parent_id, weapon_id,
		ALife::eHitTypeFireWound, fireDistance, cartridge, send_hit, aim_bullet);
}

void CShootingObject::FireStart	()
{
	bWorking=true;	
}
void CShootingObject::FireEnd	()				
{ 
	bWorking=false;	
}

void CShootingObject::StartShotParticles	()
{
	CParticlesObject* pSmokeParticles = NULL;
	StartParticles(pSmokeParticles, *m_sShotParticles, 
					m_vCurrentShootPos, m_vCurrentShootDir, true);
}