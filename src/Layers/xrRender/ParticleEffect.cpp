#include "stdafx.h"
#pragma hdrstop

#include "ParticleEffect.h"

#ifndef _EDITOR
#	include "light.h"
#	include "Blender_Particle.h"
#	include "ResourceManager.h"
#	include "dxRenderDeviceRender.h"
#	include "../../xrEngine/Environment.h"
#	include "../../xrEngine/igame_level.h"
#endif

using namespace PAPI;
using namespace PS;

const u32	PS::uDT_STEP 	= IParticleCustom::TICK_MS;	// was 33 (30 a second); the actions keep their 33ms look at any tick, see particle_actions_collection.cpp
const float	PS::fDT_STEP 	= float(uDT_STEP)/1000.f;

static void ApplyTexgen( const Fmatrix &mVP )
{
	Fmatrix mTexgen;

#ifdef	USE_DX10
	Fmatrix			mTexelAdjust		= 
	{
		0.5f,				0.0f,				0.0f,			0.0f,
		0.0f,				-0.5f,				0.0f,			0.0f,
		0.0f,				0.0f,				1.0f,			0.0f,
		0.5f,				0.5f,				0.0f,			1.0f
	};
#else	//	USE_DX10
	float	_w						= float(Device.dwWidth);
	float	_h						= float(Device.dwHeight);
	float	o_w						= (.5f / _w);
	float	o_h						= (.5f / _h);
	Fmatrix			mTexelAdjust		= 
	{
		0.5f,				0.0f,				0.0f,			0.0f,
		0.0f,				-0.5f,				0.0f,			0.0f,
		0.0f,				0.0f,				1.0f,			0.0f,
		0.5f + o_w,			0.5f + o_h,			0.0f,			1.0f
	};
#endif	//	USE_DX10

	mTexgen.mul(mTexelAdjust,mVP);
	RCache.set_c( "mVPTexgen", mTexgen );
}

void PS::OnEffectParticleBirth(void* owner, u32 , PAPI::Particle& m, u32 )
{
	CParticleEffect* PE = static_cast<CParticleEffect*>(owner); VERIFY(PE);
    CPEDef* PED			= PE->GetDefinition(); 
    if (PED){
        if (PED->m_Flags.is(CPEDef::dfRandomFrame))
            m.frame	= (u16)iFloor(Random.randI(PED->m_Frame.m_iFrameCount)*255.f);
        if (PED->m_Flags.is(CPEDef::dfAnimated)&&PED->m_Flags.is(CPEDef::dfRandomPlayback)&&Random.randI(2))
            m.flags.set(Particle::ANIMATE_CCW,TRUE);
    }
}
void PS::OnEffectParticleDead(void* , u32 , PAPI::Particle& , u32 )
{
//	CPEDef* PE = static_cast<CPEDef*>(owner);
}
//------------------------------------------------------------------------------
// class CParticleEffect
//------------------------------------------------------------------------------
CParticleEffect::CParticleEffect()
{
	m_HandleEffect 			= ParticleManager()->CreateEffect(1);		VERIFY(m_HandleEffect>=0);
	m_HandleActionList		= ParticleManager()->CreateActionList();	VERIFY(m_HandleActionList>=0);
	m_RT_Flags.zero			();
	m_Def					= 0;
	m_fElapsedLimit			= 0.f;
	m_MemDT					= 0;
	m_InitialPosition.set	(0,0,0);
	m_DestroyCallback		= 0;
	m_CollisionCallback		= 0;
	m_XFORM.identity		();
	m_ShadeSun				= 1.f;
	m_ShadeSky				= 1.f;
	m_ShadeNext				= 0;
	m_ShadeTarget.set		(1.f, 1.f);
}
CParticleEffect::~CParticleEffect()
{
	// Log					("--- destroy PE");
	OnDeviceDestroy			();
	ParticleManager()->DestroyEffect		(m_HandleEffect);
	ParticleManager()->DestroyActionList	(m_HandleActionList);
}

void CParticleEffect::Play()
{
	m_RT_Flags.set		(flRT_DefferedStop,FALSE);
	m_RT_Flags.set		(flRT_Playing,TRUE);
    ParticleManager()->PlayEffect(m_HandleEffect,m_HandleActionList);
}
void CParticleEffect::Stop(BOOL bDefferedStop)
{
    ParticleManager()->StopEffect(m_HandleEffect,m_HandleActionList,bDefferedStop);
	if (bDefferedStop){
		m_RT_Flags.set	(flRT_DefferedStop,TRUE);
	}else{
		m_RT_Flags.set	(flRT_Playing,FALSE);
	}
}
void CParticleEffect::RefreshShader()
{
	OnDeviceDestroy();
	OnDeviceCreate();
}

void CParticleEffect::UpdateParent(const Fmatrix& m, const Fvector& velocity, BOOL bXFORM)
{
	m_RT_Flags.set			(flRT_XFORM, bXFORM);
	if (bXFORM)				m_XFORM.set	(m);
	else{
		m_InitialPosition	= m.c;
        ParticleManager()->Transform(m_HandleActionList,m,velocity);
	}
}

void CParticleEffect::OnFrame(u32 frame_dt)
{
	if (m_Def && m_RT_Flags.is(flRT_Playing)){
		m_MemDT			+= frame_dt;

		int	StepCount	= 0;
		if (m_MemDT>=(s32)uDT_STEP)	{
			// allow maximum of ~99ms worth of steps to avoid slowdown after loading
			// it will really skip updates at less than 10fps, which is unplayable
			// (the cap is in STEPS, so it has to follow the tick to keep the same catch-up window)
			StepCount	= m_MemDT/uDT_STEP;
			m_MemDT		= m_MemDT%uDT_STEP;
			clamp		(StepCount,0,int(99/uDT_STEP));
		}

		for (;StepCount; StepCount--)	{
			if (m_Def->m_Flags.is(CPEDef::dfTimeLimit)){ 
				if (!m_RT_Flags.is(flRT_DefferedStop)){
					m_fElapsedLimit -= fDT_STEP;
					if (m_fElapsedLimit<0.f){
						m_fElapsedLimit = m_Def->m_fTimeLimit;
						Stop		(true);
                        break;
					}
				}
			}
            ParticleManager()->Update(m_HandleEffect,m_HandleActionList,fDT_STEP);

            PAPI::Particle* particles;
            u32 p_cnt;
            ParticleManager()->GetParticles(m_HandleEffect,particles,p_cnt);
            
			// our actions
			if (m_Def->m_Flags.is(CPEDef::dfFramed|CPEDef::dfAnimated))	m_Def->ExecuteAnimate	(particles,p_cnt,fDT_STEP);
			if (m_Def->m_Flags.is(CPEDef::dfCollision)) 				m_Def->ExecuteCollision	(particles,p_cnt,fDT_STEP,this,m_CollisionCallback);

			//-move action
			if (p_cnt)	
			{
				vis.box.invalidate	();
				float p_size = 0.f;
				for(u32 i = 0; i < p_cnt; i++){
					Particle &m 	= particles[i]; 
					vis.box.modify((Fvector&)m.pos);
					if (m.size.x>p_size) p_size = m.size.x;
					if (m.size.y>p_size) p_size = m.size.y;
					if (m.size.z>p_size) p_size = m.size.z;
				}
				vis.box.grow		(p_size);
				vis.box.getsphere	(vis.sphere.P,vis.sphere.R);
			}
			if (m_RT_Flags.is(flRT_DefferedStop)&&(0==p_cnt)){
				m_RT_Flags.set		(flRT_Playing|flRT_DefferedStop,FALSE);
				break;
			}
		}
	} else {
		vis.box.set			(m_InitialPosition,m_InitialPosition);
		vis.box.grow		(EPS_L);
		vis.box.getsphere	(vis.sphere.P,vis.sphere.R);
	}
}

BOOL CParticleEffect::Compile(CPEDef* def)
{
	m_Def 						= def;
	if (m_Def){
		// refresh shader
		RefreshShader			();

		// append actions
		IReader F				(m_Def->m_Actions.pointer(),m_Def->m_Actions.size());
        ParticleManager()->LoadActions		(m_HandleActionList,F);
        ParticleManager()->SetMaxParticles	(m_HandleEffect,m_Def->m_MaxParticles);
        ParticleManager()->SetCallback		(m_HandleEffect,OnEffectParticleBirth,OnEffectParticleDead,this,0);
		// time limit
		if (m_Def->m_Flags.is(CPEDef::dfTimeLimit))
			m_fElapsedLimit 	= m_Def->m_fTimeLimit;
	}
	if (def)	shader			= def->m_CachedShader;
	return TRUE;
}

void CParticleEffect::SetBirthDeadCB(PAPI::OnBirthParticleCB bc, PAPI::OnDeadParticleCB dc, void* owner, u32 p)
{
    ParticleManager()->SetCallback		(m_HandleEffect,bc,dc,owner,p);
}

u32 CParticleEffect::ParticlesCount()
{
	return ParticleManager()->GetParticlesCount(m_HandleEffect);
}

//------------------------------------------------------------------------------
// Render
//------------------------------------------------------------------------------
void CParticleEffect::Copy(dxRender_Visual* )
{
	FATAL	("Can't duplicate particle system - NOT IMPLEMENTED");
}

void CParticleEffect::OnDeviceCreate()
{
	if (m_Def){
		if (m_Def->m_Flags.is(CPEDef::dfSprite)){
			geom.create			(FVF::F_LIT, RCache.Vertex.Buffer(), RCache.QuadIB);
			if (m_Def) shader	= m_Def->m_CachedShader;
		}
	}
}

void CParticleEffect::OnDeviceDestroy()
{
	if (m_Def){
		if (m_Def->m_Flags.is(CPEDef::dfSprite)){
			geom.destroy		();
			shader.destroy		();
		}    
	}
}
//----------------------------------------------------
IC void FillSprite	(FVF::LIT*& pv, const Fvector& T, const Fvector& R, const Fvector& pos, const Fvector2& lt, const Fvector2& rb, float r1, float r2, u32 clr, float angle)
{
	float sa	= _sin(angle);  
	float ca	= _cos(angle);  
	Fvector Vr, Vt;
	Vr.x 		= T.x*r1*sa+R.x*r1*ca;
	Vr.y 		= T.y*r1*sa+R.y*r1*ca;
	Vr.z 		= T.z*r1*sa+R.z*r1*ca;
	Vt.x 		= T.x*r2*ca-R.x*r2*sa;
	Vt.y 		= T.y*r2*ca-R.y*r2*sa;
	Vt.z 		= T.z*r2*ca-R.z*r2*sa;

	Fvector 	a,b,c,d;
	a.sub		(Vt,Vr);
	b.add		(Vt,Vr);
	c.invert	(a);
	d.invert	(b);
	pv->set		(d.x+pos.x,d.y+pos.y,d.z+pos.z, clr, lt.x,rb.y);	pv++;
	pv->set		(a.x+pos.x,a.y+pos.y,a.z+pos.z, clr, lt.x,lt.y);	pv++;
	pv->set		(c.x+pos.x,c.y+pos.y,c.z+pos.z, clr, rb.x,rb.y);	pv++;
	pv->set		(b.x+pos.x,b.y+pos.y,b.z+pos.z,	clr, rb.x,lt.y);	pv++;
}

IC void FillSprite	(FVF::LIT*& pv, const Fvector& pos, const Fvector& dir, const Fvector2& lt, const Fvector2& rb, float r1, float r2, u32 clr, float angle)
{
	float sa	= _sin(angle);  
	float ca	= _cos(angle);  
	const Fvector& T 	= dir;
	Fvector R; 	R.crossproduct(T,Device.vCameraDirection).normalize_safe();
	Fvector Vr, Vt;
	Vr.x 		= T.x*r1*sa+R.x*r1*ca;
	Vr.y 		= T.y*r1*sa+R.y*r1*ca;
	Vr.z 		= T.z*r1*sa+R.z*r1*ca;
	Vt.x 		= T.x*r2*ca-R.x*r2*sa;
	Vt.y 		= T.y*r2*ca-R.y*r2*sa;
	Vt.z 		= T.z*r2*ca-R.z*r2*sa;

	Fvector 	a,b,c,d;
	a.sub		(Vt,Vr);
	b.add		(Vt,Vr);
	c.invert	(a);
	d.invert	(b);
	pv->set		(d.x+pos.x,d.y+pos.y,d.z+pos.z, clr, lt.x,rb.y);	pv++;
	pv->set		(a.x+pos.x,a.y+pos.y,a.z+pos.z, clr, lt.x,lt.y);	pv++;
	pv->set		(c.x+pos.x,c.y+pos.y,c.z+pos.z, clr, rb.x,rb.y);	pv++;
	pv->set		(b.x+pos.x,b.y+pos.y,b.z+pos.z,	clr, rb.x,lt.y);	pv++;
}

extern ENGINE_API float		psHUD_FOV;

#ifndef _EDITOR
//////////////////////////////////////////////////////////////////////////////////////////
// Particle lighting.
//
// A particle carries no lighting of its own: vanilla X-Ray draws a puff of dust exactly as bright at
// midnight as at noon, and a torch shining through it changes nothing. Both halves are folded into
// the per-particle vertex colour here, on the CPU -- the sprite already has a colour to modulate, so
// this needs no extra vertex channel, no shader constant and no per-renderer shader work.
//
//  * ambient -- the environment's hemisphere colour, i.e. how bright the sky is at this moment,
//    normalised so that noon comes out at 1.0 and nothing changes in the daytime picture;
//  * local lights -- every dynamic point/spot light that reaches the particle: NPC and player
//    torches, muzzle flashes, lamps carried by the anomalies. Static lights are skipped, they are
//    baked into the level lightmaps. There is no shadowing here: a torch lights the dust it points
//    at, and a wall in between is not consulted -- their range is short enough for that to pass.
//  * shade -- the sun's part of the ambient only counts while the sun is in sight of the effect, and
//    the sky's part drops under a roof: one ray each from the centre of the effect through the static
//    level geometry (particle_shade), so dust kicked off a wall in the shade, or inside a building,
//    is no longer as bright as in the open sun. Same rays as dynamic objects use (CROS_impl).
//
// Only the BLEND pass is lit (oBlend==1: smoke, dust, steam). Additive effects -- fire, sparks, the
// muzzle flash itself -- are self-luminous and must stay untouched, MUL ones darken by design, and
// SET particles go through the deferred path where the engine lights them already.
static const float	PARTICLE_LIGHT_REACH	= 25.f;		// how far out lights are looked for, meters
static const float	PARTICLE_SUN_WEIGHT		= 0.7f;		// how much of the sun counts towards the ambient
static const float	PARTICLE_LIGHT_NOON		= 0.96f;	// the same sum at noon in the stock clear weather
static const float	PARTICLE_AMBIENT_FLOOR	= 0.13f;	// the night level, kept where it was tuned by eye:
															// the sun weight above must not drag it down with it
static const float	PARTICLE_ROOF_SKY		= 0.5f;		// the sky's share left under a roof (no sky straight up)
static const u32	PARTICLE_SHADE_PERIOD	= 100;		// ms between ray tests of one effect
static const float	PARTICLE_SHADE_SMOOTH	= 8.f;		// 1/s: how fast the shade follows a moving effect

// Is the sun / the sky in sight from C? Static level geometry only ("shadow from the statics"), the
// way dynamic objects ask it (CROS_impl::calc_sun_value / calc_sky_hemi_value). The ray starts a few cm
// out towards the light: dust from a hit sits right on the wall, and a wall that faces away from the
// sun must still shade it (the ray then runs into it), while one that faces the sun must not.
static void particle_shade(const Fvector& C, float& sun, float& sky)
{
	sun = 1.f;	sky = 1.f;
	if (!g_pGameLevel)						return;
#if RENDER==R_R1
	light*	L	= (light*)RImplementation.L_DB->sun_adapted._get();
#else
	light*	L	= (light*)RImplementation.Lights.sun_adapted._get();
#endif
	if (L)
	{
		Fvector	d;	d.set(L->direction).invert().normalize_safe();
		Fvector	p;	p.mad(C, d, 0.05f);
		sun		= g_pGameLevel->ObjectSpace.RayTest(p, d, 500.f, collide::rqtStatic, NULL, NULL) ? 0.f : 1.f;
	}
	Fvector	up;	up.set(0.f, 1.f, 0.f);
	Fvector	p;	p.mad(C, up, 0.05f);
	sky			= g_pGameLevel->ObjectSpace.RayTest(p, up, 50.f, collide::rqtStatic, NULL, NULL) ? 0.f : 1.f;
}

class particle_lighting
{
	struct	src	{
		Fvector		P;			// world position
		Fvector		D;			// spot direction (unit)
		float		range2;
		float		cos_half;	// spot: cosine of the half-angle; -2 for a point light
		float		r,g,b;
		float		weight;		// how much this one is worth at the centre of the effect
	};
	enum					{ max_lights = 6 };
	svector<src,max_lights>	m_lights;
	float					m_ambient;
public:
							particle_lighting	() : m_ambient(1.f)	{}
	void					begin				(const Fvector& C, float R, float sun_vis = 1.f, float sky_vis = 1.f);
	u32						apply				(u32 clr, const Fvector& P) const;
};

void	particle_lighting::begin	(const Fvector& C, float R, float sun_vis, float sky_vis)
{
	m_lights.clear	();

	// The hemisphere colour alone does not separate an overcast day from a sunny one -- the weather
	// files hold a HIGHER hemisphere in the clouds (0.45 against 0.39 at noon), which is honest: an
	// overcast sky is one big soft lamp. What the rain takes away is the sun, so the sun colour is
	// weighed in alongside it. Normalised by the clear-noon sum, so full daylight still comes out at
	// 1.0 and the daytime picture does not change.
	CEnvDescriptor&	E	= *g_pGamePersistent->Environment().CurrentEnv;
	float	hemi		= (E.hemi_color.x + E.hemi_color.y + E.hemi_color.z) / 3.f;
	float	sun			= (E.sun_color.x  + E.sun_color.y  + E.sun_color.z ) / 3.f;
	// the shade: no sun where the level blocks it, and under a roof only part of the sky
	float	k			= (hemi*(PARTICLE_ROOF_SKY + (1.f-PARTICLE_ROOF_SKY)*sky_vis) + PARTICLE_SUN_WEIGHT*sun*sun_vis) / PARTICLE_LIGHT_NOON;
	clamp				(k, 0.f, 1.f);
	m_ambient			= _max(k, PARTICLE_AMBIENT_FLOOR);

	static xr_vector<ISpatial*>	q;
	q.clear			();
	g_SpatialSpace->q_sphere	(q, 0, STYPE_LIGHTSOURCE, C, R + PARTICLE_LIGHT_REACH);
	for (u32 it=0; it<q.size(); it++)
	{
		light*	L	= (light*)(q[it]->dcast_Light());
		if (0==L)													continue;
		if (!L->flags.bActive || L->range<EPS_L)					continue;
		if (IRender_Light::SPOT!=L->flags.type && IRender_Light::POINT!=L->flags.type)	continue;
		// Static lights are kept: they are baked into the level lightmaps, and a particle has no
		// lightmap at all -- without them a puff of dust under a lamp or over a campfire stays black.
		float	d	= C.distance_to(L->position);
		if (d > L->range + R)										continue;

		src		s;
		s.P		= L->position;
		s.range2= L->range * L->range;
		s.r		= L->color.r;	s.g = L->color.g;	s.b = L->color.b;
		if (IRender_Light::SPOT==L->flags.type)
		{
			// NOTE: normalize_safe(v) ASSIGNS v -- it is not a fallback argument.
			s.D		= L->direction;
			if (s.D.square_magnitude() < EPS_S)	s.D.set(0.f,-1.f,0.f);
			else								s.D.normalize();
			s.cos_half	= _cos(_min(L->cone, PI-EPS_S) * 0.5f);
		}
		else
		{
			s.D.set		(0.f,-1.f,0.f);
			s.cos_half	= -2.f;			// no cone test for a point light
		}

		// Keep the six that matter most: brightness at the centre of the effect, so a lamp right
		// next to the smoke wins over a torch at the far end of the query sphere.
		float	att		= _max(0.f, 1.f - (d*d)/s.range2);
		s.weight		= att * (s.r + s.g + s.b);
		if (s.weight < EPS_S)										continue;
		if (m_lights.size() < max_lights)	m_lights.push_back(s);
		else
		{
			u32	worst	= 0;
			for (u32 k=1; k<m_lights.size(); k++)
				if (m_lights[k].weight < m_lights[worst].weight)	worst = k;
			if (m_lights[worst].weight < s.weight)	m_lights[worst] = s;
		}
	}
}

u32		particle_lighting::apply	(u32 clr, const Fvector& P) const
{
	float	sr = m_ambient, sg = m_ambient, sb = m_ambient;
	for (u32 it=0; it<m_lights.size(); it++)
	{
		const src&	s	= m_lights[it];
		Fvector		D;	D.sub	(P, s.P);
		float		d2	= D.square_magnitude();
		if (d2 >= s.range2)											continue;
		float		att	= 1.f - d2 / s.range2;						// same falloff the deferred lights use
		if (s.cos_half > -1.f)										// spot: fade out towards the cone edge
		{
			float	d	= _sqrt(d2);
			if (d > EPS_S)
			{
				D.div		(d);
				float	c	= D.dotproduct(s.D);
				if (c <= s.cos_half)								continue;
				att			*= (c - s.cos_half) / (1.f - s.cos_half);
			}
		}
		sr += s.r*att;	sg += s.g*att;	sb += s.b*att;
	}
	clamp	(sr, 0.f, 1.f);	clamp (sg, 0.f, 1.f);	clamp (sb, 0.f, 1.f);
	return	color_rgba	(iFloor(color_get_R(clr)*sr), iFloor(color_get_G(clr)*sg),
						 iFloor(color_get_B(clr)*sb), color_get_A(clr));
}

// Resolved once per particle definition: is this effect drawn with the BLEND pass?
IC BOOL		particle_is_lit		(CPEDef* def)
{
	if (0==def)								return FALSE;
	if (def->m_LitBlend < 0)
	{
		def->m_LitBlend		= 0;
		IBlender* B			= DEV->_FindBlender(def->m_ShaderName.c_str());
		CBlender_Particle* P= dynamic_cast<CBlender_Particle*>(B);
		if (P && 1==P->getBlendMode())	def->m_LitBlend = 1;
	}
	return	def->m_LitBlend > 0;
}

//////////////////////////////////////////////////////////////////////////////////////////
// A world effect drawn in the hud's depth slice (flRT_HUDworld).
//
// The hud is drawn into a slice of its own at the front of the depth range (rmNear), so any pixel of the
// weapon or the hands wins the depth test against anything in the world, however near. Smoke that has
// left the first-person weapon -- out of the muzzle, the ejection port, the opened barrels -- is a world
// effect (it stays where it came out), and drawn with the world it is covered by the whole weapon: only
// what sticks out past its outline is seen, and the cloud reads as sitting behind the gun.
// So while such an effect is near the eye it is drawn with the hud's projection, in the hud's slice, and
// through a world matrix that turns the world into the hud's space. The game put it there from a hud
// point by squeezing the depth along the view by tan(hud/2) / tan(world/2) and keeping the part across it
// (CShootingObject::HudFxPointToWorld); this stretches the depth back by the inverse:
//		x' = cam + A (x - cam),		A = I + (K - 1) d d^T,		K = tan(world/2) / tan(hud/2)
// The hud projection then shows every point exactly where, and as large as, the world one would (the
// screen offset is across / (along * tan(fov/2)), and along grows by K as tan shrinks by it), so the
// picture does not change -- only the depth does, and it is the hud's own: the smoke at the muzzle is at
// the muzzle's depth, in front of what is behind it and behind what is in front of it.
// The price: in the hud's slice the world does not cover it either. So only near the eye, where nothing
// else is, as a rule; further out it is drawn with the world, and the hand-over is not seen, the two
// pictures being the same. The matrix is built from this frame's camera, so it is right whatever the
// camera has done since the effect was let out.
static const float	PARTICLE_HUD_WORLD_REACH	= 1.5f;		// meters from the eye to the centre of the effect

BOOL	CParticleEffect::HudWorldNow	(Fmatrix& W)
{
	if (!m_RT_Flags.is(flRT_HUDworld) || m_RT_Flags.is(flRT_HUDmode))	return FALSE;

	Fvector	C	= vis.sphere.P;
	if (m_RT_Flags.is(flRT_XFORM))	m_XFORM.transform_tiny(C);
	if (Device.vCameraPosition.distance_to_sqr(C) > PARTICLE_HUD_WORLD_REACH*PARTICLE_HUD_WORLD_REACH)	return FALSE;

	const float	t_hud	= tanf(deg2rad(0.5f * psHUD_FOV * Device.fFOV_HUD));
	const float	t_wld	= tanf(deg2rad(0.5f * Device.fFOV));
	if (t_hud <= EPS_L || t_wld <= EPS_L)							return FALSE;
	const float	k1		= t_wld / t_hud - 1.f;

	const Fvector&	d	= Device.vCameraDirection;
	const Fvector&	cam	= Device.vCameraPosition;
	W.identity	();
	W.i.set		(1.f + k1 * d.x * d.x,	k1 * d.x * d.y,			k1 * d.x * d.z);
	W.j.set		(k1 * d.y * d.x,		1.f + k1 * d.y * d.y,	k1 * d.y * d.z);
	W.k.set		(k1 * d.z * d.x,		k1 * d.z * d.y,			1.f + k1 * d.z * d.z);
	Fvector	a;	W.transform_dir	(a, cam);
	W.c.sub		(cam, a);
	return		TRUE;
}
#endif	// _EDITOR

void CParticleEffect::Render(float )
{
	u32			dwOffset,dwCount;
	// Get a pointer to the particles in gp memory
    PAPI::Particle* particles;
    u32 			p_cnt;
    ParticleManager()->GetParticles(m_HandleEffect,particles,p_cnt);

	if(p_cnt>0){
		if (m_Def&&m_Def->m_Flags.is(CPEDef::dfSprite)){
			FVF::LIT* pv_start	= (FVF::LIT*)RCache.Vertex.Lock(p_cnt*4*4,geom->vb_stride,dwOffset);
			FVF::LIT* pv		= pv_start;

#ifndef _EDITOR
			// ambient + every dynamic light that reaches this effect, gathered once for the whole puff
			particle_lighting	PL;
			BOOL				bLit	= particle_is_lit(m_Def);
			if (bLit)
			{
				Fvector	C	= vis.sphere.P;
				if (m_RT_Flags.is(flRT_XFORM))	m_XFORM.transform_tiny(C);
				// the shade: ray-tested every PARTICLE_SHADE_PERIOD, eased in between; the first test
				// is taken as it is -- a hit puff lives half a second and must not start out sunlit
				if (Device.dwTimeGlobal >= m_ShadeNext)
				{
					float	sun, sky;
					particle_shade	(C, sun, sky);
					if (0==m_ShadeNext)	{ m_ShadeSun = sun; m_ShadeSky = sky; }
					m_ShadeTarget.set	(sun, sky);
					m_ShadeNext		= Device.dwTimeGlobal + PARTICLE_SHADE_PERIOD;
				}
				float	f	= _min(1.f, Device.fTimeDelta * PARTICLE_SHADE_SMOOTH);
				m_ShadeSun	+= (m_ShadeTarget.x - m_ShadeSun) * f;
				m_ShadeSky	+= (m_ShadeTarget.y - m_ShadeSky) * f;
				PL.begin	(C, vis.sphere.R, m_ShadeSun, m_ShadeSky);
			}
#endif

			for(u32 i = 0; i < p_cnt; i++){
				PAPI::Particle &m = particles[i];

				u32		p_clr	= m.color;
#ifndef _EDITOR
				if (bLit)
				{
					Fvector	wp;
					if (m_RT_Flags.is(flRT_XFORM))	m_XFORM.transform_tiny	(wp, (Fvector&)m.pos);
					else							wp.set					((Fvector&)m.pos);
					p_clr	= PL.apply	(m.color, wp);
				}
#endif

				Fvector2 lt,rb;
				lt.set			(0.f,0.f);
				rb.set			(1.f,1.f);
				if (m_Def->m_Flags.is(CPEDef::dfFramed)) m_Def->m_Frame.CalculateTC(iFloor(float(m.frame)/255.f),lt,rb);
				float r_x		= m.size.x*0.5f;
				float r_y		= m.size.y*0.5f;
				if (m_Def->m_Flags.is(CPEDef::dfVelocityScale)){
					float speed	= m.vel.magnitude();
					r_x			+= speed*m_Def->m_VelocityScale.x;
					r_y			+= speed*m_Def->m_VelocityScale.y;
				}
				if (m_Def->m_Flags.is(CPEDef::dfAlignToPath)){
					float speed	= m.vel.magnitude();
                    if ((speed<EPS_S)&&m_Def->m_Flags.is(CPEDef::dfWorldAlign)){
                    	Fmatrix	M;  	
                        M.setXYZ			(m_Def->m_APDefaultRotation);
                        if (m_RT_Flags.is(flRT_XFORM)){
                            Fvector p;
                            m_XFORM.transform_tiny(p,m.pos);
	                        M.mulA_43		(m_XFORM);
                            FillSprite		(pv,M.k,M.i,p,lt,rb,r_x,r_y,p_clr,m.rot.x);
                        }else{
                            FillSprite		(pv,M.k,M.i,m.pos,lt,rb,r_x,r_y,p_clr,m.rot.x);
                        }
                    }else if ((speed>=EPS_S)&&m_Def->m_Flags.is(CPEDef::dfFaceAlign)){
                    	Fmatrix	M;  		M.identity();
                        M.k.div				(m.vel,speed);            
                        M.j.set 			(0,1,0);	if (_abs(M.j.dotproduct(M.k))>.99f)  M.j.set(0,0,1);
                        M.i.crossproduct	(M.j,M.k);	M.i.normalize	();
                        M.j.crossproduct   	(M.k,M.i);	M.j.normalize  ();
                        if (m_RT_Flags.is(flRT_XFORM)){
                            Fvector p;
                            m_XFORM.transform_tiny(p,m.pos);
	                        M.mulA_43		(m_XFORM);
                            FillSprite		(pv,M.j,M.i,p,lt,rb,r_x,r_y,p_clr,m.rot.x);
                        }else{
                            FillSprite		(pv,M.j,M.i,m.pos,lt,rb,r_x,r_y,p_clr,m.rot.x);
                        }
                    }else{
						Fvector 			dir;
                        if (speed>=EPS_S)	dir.div	(m.vel,speed);
                        else				dir.setHP(-m_Def->m_APDefaultRotation.y,-m_Def->m_APDefaultRotation.x);
                        if (m_RT_Flags.is(flRT_XFORM)){
                            Fvector p,d;
                            m_XFORM.transform_tiny	(p,m.pos);
                            m_XFORM.transform_dir	(d,dir);
                            FillSprite	(pv,p,d,lt,rb,r_x,r_y,p_clr,m.rot.x);
                        }else{
                            FillSprite	(pv,m.pos,dir,lt,rb,r_x,r_y,p_clr,m.rot.x);
                        }
                    }
				}else{
					if (m_RT_Flags.is(flRT_XFORM)){
						Fvector p;
						m_XFORM.transform_tiny	(p,m.pos);
						FillSprite	(pv,Device.vCameraTop,Device.vCameraRight,p,lt,rb,r_x,r_y,p_clr,m.rot.x);
					}else{
						FillSprite	(pv,Device.vCameraTop,Device.vCameraRight,m.pos,lt,rb,r_x,r_y,p_clr,m.rot.x);
					}
				}
			}
			dwCount 			= u32(pv-pv_start);
			RCache.Vertex.Unlock(dwCount,geom->vb_stride);
			if (dwCount)    
			{
#ifndef _EDITOR
				Fmatrix Pold						= Device.mProject;
				Fmatrix FTold						= Device.mFullTransform;
				Fmatrix W;
				const BOOL bHudWorld				= HudWorldNow(W);
				if(GetHudMode() || bHudWorld)
				{
					Device.mProject.build_projection(	deg2rad(psHUD_FOV*Device.fFOV_HUD), 
														Device.fASPECT, 
														VIEWPORT_NEAR, 
														g_pGamePersistent->Environment().CurrentEnv->far_plane);

					Device.mFullTransform.mul	(Device.mProject, Device.mView);
					RCache.set_xform_project	(Device.mProject);
					RImplementation.rmNear		();
					// The soft-particle shaders (particle_add, particle_distort...) look the scene's position
					// buffer up through this texgen, at the pixel the particle covers. In the hud's slice the
					// particle is drawn through W as well (m_WVP), so the texgen must have it too: without it the
					// lookup landed K times further from the middle of the screen than the sprite, and the sprite
					// faded out wherever the weapon happened to be at that other pixel, as if behind it.
					if (bHudWorld)
					{
						Fmatrix	FTW;	FTW.mul		(Device.mFullTransform, W);
						ApplyTexgen		(FTW);
					}
					else
						ApplyTexgen		(Device.mFullTransform);
				}
#endif

#ifndef _EDITOR
				RCache.set_xform_world	(bHudWorld ? W : Fidentity);
				// in the hud's slice no soft fade (particle_add.ps): it is ~0.8 m deep, and at the scale of the
				// weapon in hand it ate every sprite lying on the gun. Set on every draw, both ways: a shader
				// constant keeps whatever the last draw left in it.
				RCache.set_c			("particle_hud_slice", (GetHudMode() || bHudWorld) ? 1.f : 0.f, 0.f, 0.f, 0.f);
#else
				RCache.set_xform_world	(Fidentity);
#endif
				RCache.set_Geometry		(geom);

                RCache.set_CullMode		(m_Def->m_Flags.is(CPEDef::dfCulling)?(m_Def->m_Flags.is(CPEDef::dfCullCCW)?CULL_CCW:CULL_CW):CULL_NONE);
				RCache.Render	   		(D3DPT_TRIANGLELIST,dwOffset,0,dwCount,0,dwCount/2);
                RCache.set_CullMode		(CULL_CCW	); 
#ifndef _EDITOR
				if(GetHudMode() || bHudWorld)
				{
					RImplementation.rmNormal	();
					Device.mProject				= Pold;
					Device.mFullTransform		= FTold;
					RCache.set_xform_project	(Device.mProject);
					ApplyTexgen(Device.mFullTransform);
				}
#endif
			}
		}
	}
}

