#include "stdafx.h"
#include "dxRainRender.h"

#include "../../xrEngine/Rain.h"
#include "Light_DB.h"

//	Warning: duplicated in rain.cpp
static const int	max_desired_items	= 2500;
static const float	source_radius		= 12.5f;
static const float	source_offset		= 40.f;
static const float	max_distance		= source_offset*1.25f;
static const float	sink_offset			= -(max_distance-source_offset);
static const float	drop_length			= 5.f;
static const float	drop_width			= 0.30f;
static const float	drop_angle			= 3.0f;
static const float	drop_max_angle		= deg2rad(10.f);
static const float	drop_max_wind_vel	= 20.0f;
static const float	drop_speed_min		= 40.f;
static const float	drop_speed_max		= 80.f;

const int	max_particles		= 1000;
const int	particles_cache		= 400;
const float particles_time		= .3f;

// Dynamic lights on the rain. The drops and the splashes are lit by nothing but the weather's
// rain_color, so a lamp, a flashlight or a laser dot left them as grey as the open sky around. The
// few lights that reach the rain this frame are added in here, per vertex, through the vertex
// colour: the shaders stay as they are, and the streaks and whatever shader rain.dm names light up
// alike. `r__rain_lights` scales it, 0 turns it off.
static const u32	rain_lights_max	= 8;

struct rain_light_sel
{
	Fvector		position;
	float		range_sq;
	Fvector		color;
	float		cone_cos;			// cosine of a spot's half-angle; below -1 for an omni
	Fvector		direction;
	float		cone_full;			// cosine inside which the spot is at full strength
	float		score;
};

// The lights whose sphere reaches the drops' cylinder (source_radius around the eye, from
// sink_offset below it to source_offset above), the strongest rain_lights_max of them.
static u32 rain_lights_select(rain_light_sel* dst, const CLight_DB& db, const Fvector& eye)
{
	u32	count		= 0;
	for (u32 it=0; it<db.v_rain.size(); ++it)
	{
		const CLight_DB::rain_light& L	= db.v_rain[it];
		const float	R		= L.range*.95f;		// as the light passes have it
		const float	dx		= L.position.x-eye.x, dz = L.position.z-eye.z;
		if (_sqrt(dx*dx+dz*dz) > source_radius+.5f+R)	continue;
		if (L.position.y+R < eye.y+sink_offset)			continue;
		if (L.position.y-R > eye.y+source_offset)			continue;
		const float	lum		= (L.color.x+L.color.y+L.color.z)/3.f;
		if (lum < EPS_L)									continue;

		rain_light_sel	S;
		S.position.set		(L.position);
		S.range_sq			= R*R;
		S.color.set			(L.color);
		S.cone_cos			= L.cone_cos;
		S.direction.set		(L.direction);
		S.cone_full			= (L.cone_cos < -1.f) ? -2.f : _cos(acosf(L.cone_cos)*.7f);
		S.score				= lum*R/(R+L.position.distance_to(eye));

		// keep them sorted, strongest first; past rain_lights_max the weakest falls off
		u32	at			= count;
		while (at && dst[at-1].score < S.score)	--at;
		if (at >= rain_lights_max)					continue;
		for (u32 i=_min(count,rain_lights_max-1); i>at; --i)	dst[i] = dst[i-1];
		dst[at]			= S;
		if (count < rain_lights_max)				++count;
	}
	return	count;
}

// The weather's own light on the rain. rain_color is all the stock rain ever got -- in most weathers
// the same at noon and at midnight. Scale it by the light the weather gives everything else (ambient,
// hemisphere, sun), so the drops dim after dark like the world around them. The stock rainy daytime
// sky (hemisphere ~0.5) is the reference: under it rain_color stays as the weather was tuned.
// `r__rain_ambient` blends it in, 0 leaves rain_color as it is. Whatever would clip keeps its hue.
static const float	rain_weather_ref	= 0.5f;

static void rain_weather_light(Fvector3& color, const CEnvDescriptor& E)
{
	if (ps_r__rain_ambient < EPS_L)	return;
	const float	lum	= (E.ambient.x+E.ambient.y+E.ambient.z
					+ E.hemi_color.x+E.hemi_color.y+E.hemi_color.z
					+ E.sun_color.x+E.sun_color.y+E.sun_color.z)/3.f;
	float		k	= lum/rain_weather_ref;
	clamp		(k,0.f,2.f);
	color.mul	(1.f + (k-1.f)*ps_r__rain_ambient);
	const float	m	= _max(color.x,_max(color.y,color.z));
	if (m > 1.f)	color.div(m);
}

// Henyey-Greenstein, normalised to a mean of 1 over the sphere
IC float rain_hg(float g, float cos_a)
{
	const float	d	= 1.f+g*g-2.f*g*cos_a;
	return	(1.f-g*g)/(d*_sqrt(d));
}

// The rain colour at P with the selected lights added in. Water drops throw most light forward, so
// rain glows when seen against a lamp, and some of it straight back, so a flashlight's beam still
// picks the drops out in front of the player. Whatever would clip keeps its hue.
static u32 rain_lit_color(const Fvector& base, float alpha, const Fvector& P, const Fvector& eye, const rain_light_sel* lights, u32 count)
{
	Fvector	view;	view.sub(P,eye);	view.normalize_safe();
	Fvector	C;		C.set(0,0,0);
	for (u32 i=0; i<count; ++i)
	{
		const rain_light_sel&	LS	= lights[i];
		Fvector		to;		to.sub(LS.position,P);
		const float	d_sq	= to.square_magnitude();
		if (d_sq >= LS.range_sq)	continue;
		const float	d		= _sqrt(d_sq);
		if (d > EPS_S)	to.div(d);	else	to.invert(view);
		float		k		= 1.f - d_sq/LS.range_sq;		// the falloff of the light passes
		if (LS.cone_cos >= -1.f)
		{
			const float	c	= -to.dotproduct(LS.direction);
			if (c <= LS.cone_cos)	continue;
			float	t	= (c-LS.cone_cos)/_max(LS.cone_full-LS.cone_cos,EPS_S);
			clamp		(t,0.f,1.f);
			k			*= t*t*(3.f-2.f*t);
		}
		const float	cs		= view.dotproduct(to);				// 1: looking at the light through the drop
		k				*= .7f*rain_hg(.4f,cs) + .3f*rain_hg(-.4f,cs);
		C.mad			(LS.color,k);
	}
	C.mul				(ps_r__rain_lights);
	C.add				(base);
	const float	m		= _max(C.x,_max(C.y,C.z));
	if (m > 1.f)		C.div(m);
	return	color_rgba_f	(C.x,C.y,C.z,alpha);
}

dxRainRender::dxRainRender()
{
	IReader* F = FS.r_open("$game_meshes$","dm\\rain.dm"); 
	VERIFY3(F,"Can't open file.","dm\\rain.dm");

	DM_Drop	= ::RImplementation.model_CreateDM		(F);

	//
	SH_Rain.create("effects\\rain","fx\\fx_rain");
	hGeom_Rain.create(FVF::F_LIT, RCache.Vertex.Buffer(), RCache.QuadIB);
	hGeom_Drops.create(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, RCache.Vertex.Buffer(), RCache.Index.Buffer());
	
	FS.r_close(F);
}

dxRainRender::~dxRainRender()
{
	::RImplementation.model_Delete(DM_Drop);
}

void dxRainRender::Copy(IRainRender &_in)
{
	*this = *(dxRainRender*)&_in;
}

#include "../../xrEngine/iGame_persistent.h"

void dxRainRender::Render(CEffect_Rain &owner)
{
	float	factor				= g_pGamePersistent->Environment().CurrentEnv->rain_density;
	if (factor<EPS_L)			return;

	u32 desired_items			= iFloor	(0.5f*(1.f+factor)*float(max_desired_items));
	// visual
	float		factor_visual	= factor/2.f+.5f;
	Fvector3	f_rain_color	= g_pGamePersistent->Environment().CurrentEnv->rain_color;
	rain_weather_light			(f_rain_color,*g_pGamePersistent->Environment().CurrentEnv);
	u32			u_rain_color	= color_rgba_f(f_rain_color.x,f_rain_color.y,f_rain_color.z,factor_visual);

	// the lights that reach the rain this frame; with none, the flat colour above as it always was
#if RENDER==R_R1
	const CLight_DB&	light_db	= *RImplementation.L_DB;
#else
	const CLight_DB&	light_db	= RImplementation.Lights;
#endif
	rain_light_sel	lights[rain_lights_max];
	const u32		lights_count	= (ps_r__rain_lights > EPS_L) ? rain_lights_select(lights,light_db,Device.vCameraPosition) : 0;

	// born _new_ if needed
	float	b_radius_wrap_sqr	= _sqr((source_radius+.5f));
	if (owner.items.size()<desired_items)	{
		// owner.items.reserve		(desired_items);
		while (owner.items.size()<desired_items)	{
			CEffect_Rain::Item				one;
			owner.Born				(one,source_radius);
			owner.items.push_back		(one);
		}
	}

	// build source plane
	Fplane src_plane;
	Fvector norm	={0.f,-1.f,0.f};
	Fvector upper; 	upper.set(Device.vCameraPosition.x,Device.vCameraPosition.y+source_offset,Device.vCameraPosition.z);
	src_plane.build(upper,norm);

	// perform update
	u32			vOffset;
	FVF::LIT	*verts		= (FVF::LIT	*) RCache.Vertex.Lock(desired_items*4,hGeom_Rain->vb_stride,vOffset);
	FVF::LIT	*start		= verts;
#if RENDER==R_R3
	// and a copy of them for the refraction, which is drawn later (phase_rain_distort); NULL when off
	FVF::LIT*	keep		= RImplementation.Target->rain_distort_begin(_max(desired_items,u32(owner.items.size()))*4);
	FVF::LIT*	keep_start	= keep;
#endif
	const Fvector&	vEye	= Device.vCameraPosition;
	for (u32 I=0; I<owner.items.size(); I++){
		// physics and time control
		CEffect_Rain::Item&	one		=	owner.items[I];

		if (one.dwTime_Hit<Device.dwTimeGlobal)		owner.Hit (one.Phit);
		if (one.dwTime_Life<Device.dwTimeGlobal)	owner.Born(one,source_radius);

		// последн€€ дельта ??
		//.		float xdt		= float(one.dwTime_Hit-Device.dwTimeGlobal)/1000.f;
		//.		float dt		= Device.fTimeDelta;//xdt<Device.fTimeDelta?xdt:Device.fTimeDelta;
		float dt		= Device.fTimeDelta;
		one.P.mad		(one.D,one.fSpeed*dt);

		Device.Statistic->TEST1.Begin();
		Fvector	wdir;	wdir.set(one.P.x-vEye.x,0,one.P.z-vEye.z);
		float	wlen	= wdir.square_magnitude();
		if (wlen>b_radius_wrap_sqr)	{
			wlen		= _sqrt(wlen);
			//.			Device.Statistic->TEST3.Begin();
			if ((one.P.y-vEye.y)<sink_offset){
				// need born
				one.invalidate();
			}else{
				Fvector		inv_dir, src_p;
				inv_dir.invert(one.D);
				wdir.div	(wlen);
				one.P.mad	(one.P, wdir, -(wlen+source_radius));
				if (src_plane.intersectRayPoint(one.P,inv_dir,src_p)){
					float dist_sqr	= one.P.distance_to_sqr(src_p);
					float height	= max_distance;
					if (owner.RayPick(src_p,one.D,height,collide::rqtBoth)){	
						if (_sqr(height)<=dist_sqr){ 
							one.invalidate	();								// need born
							//							Log("1");
						}else{	
							owner.RenewItem	(one,height-_sqrt(dist_sqr),TRUE);		// fly to point
							//							Log("2",height-dist);
						}
					}else{
						owner.RenewItem		(one,max_distance-_sqrt(dist_sqr),FALSE);		// fly ...
						//						Log("3",1.5f*b_height-dist);
					}
				}else{
					// need born
					one.invalidate();
					//					Log("4");
				}
			}
			//.			Device.Statistic->TEST3.End();
		}
		Device.Statistic->TEST1.End();

		// Build line
		Fvector&	pos_head	= one.P;
		Fvector		pos_trail;	pos_trail.mad	(pos_head,one.D,-drop_length*factor_visual);

		// Culling
		Fvector sC,lineD;	float sR; 
		sC.sub			(pos_head,pos_trail);
		lineD.normalize	(sC);
		sC.mul			(.5f);
		sR				= sC.magnitude();
		sC.add			(pos_trail);
		if (!::Render->ViewBase.testSphere_dirty(sC,sR))	continue;

		static Fvector2 UV[2][4]={
			{{0,1},{0,0},{1,1},{1,0}},
			{{1,0},{1,1},{0,0},{0,1}}
		};

		// Everything OK - build vertices
		Fvector	P,lineTop,camDir;
		camDir.sub			(sC,vEye);
		camDir.normalize	();
		lineTop.crossproduct(camDir,lineD);
		float w = drop_width;
		u32 s	= one.uv_set;
		u32 c_trail	= u_rain_color, c_head = u_rain_color;
		if (lights_count)	{
			c_trail	= rain_lit_color(f_rain_color,factor_visual,pos_trail,vEye,lights,lights_count);
			c_head	= rain_lit_color(f_rain_color,factor_visual,pos_head, vEye,lights,lights_count);
		}
		// built here, then copied: the vertex stream is write-only memory, no copy can be read back out of it
		FVF::LIT	q[4];
		P.mad(pos_trail,lineTop,-w);	q[0].set(P,c_trail,UV[s][0].x,UV[s][0].y);
		P.mad(pos_trail,lineTop,w);		q[1].set(P,c_trail,UV[s][1].x,UV[s][1].y);
		P.mad(pos_head, lineTop,-w);	q[2].set(P,c_head,UV[s][2].x,UV[s][2].y);
		P.mad(pos_head, lineTop,w);		q[3].set(P,c_head,UV[s][3].x,UV[s][3].y);
		CopyMemory			(verts,q,sizeof(q));	verts	+= 4;
#if RENDER==R_R3
		if (keep)			{ CopyMemory(keep,q,sizeof(q));	keep	+= 4; }
#endif
	}
	u32 vCount					= (u32)(verts-start);
#if RENDER==R_R3
	if (keep_start)				RImplementation.Target->rain_distort_end(u32(keep-keep_start));
#endif
	RCache.Vertex.Unlock		(vCount,hGeom_Rain->vb_stride);

	// Render if needed
	if (vCount)	{
		//HW.pDevice->SetRenderState	(D3DRS_CULLMODE,D3DCULL_NONE);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_xform_world		(Fidentity);
		RCache.set_Shader			(SH_Rain);
		RCache.set_Geometry			(hGeom_Rain);
		RCache.Render				(D3DPT_TRIANGLELIST,vOffset,0,vCount,0,vCount/2);
#if RENDER==R_R3
		// laser beams light up the drops that cross them: same vertices, drawn into their density buffer
		// now, while they are still in the vertex stream (drawing the rain again would move it twice)
		RImplementation.Target->phase_laser_density_rain(hGeom_Rain, vOffset, vCount);
		RCache.set_CullMode(CULL_NONE);
#endif
		//HW.pDevice->SetRenderState	(D3DRS_CULLMODE,D3DCULL_CCW);
		RCache.set_CullMode(CULL_CCW);
	}

	// Particles
	CEffect_Rain::Particle*	P		= owner.particle_active;
	if (0==P)			return;

	{
		float	dt				= Device.fTimeDelta;
		_IndexStream& _IS		= RCache.Index;
		RCache.set_Shader		(DM_Drop->shader);

		Fmatrix					mXform,mScale;
		int						pcount  = 0;
		u32						v_offset,i_offset;
		u32						vCount_Lock		= particles_cache*DM_Drop->number_vertices;
		u32						iCount_Lock		= particles_cache*DM_Drop->number_indices;
		IRender_DetailModel::fvfVertexOut* v_ptr= (IRender_DetailModel::fvfVertexOut*) RCache.Vertex.Lock	(vCount_Lock, hGeom_Drops->vb_stride, v_offset);
		u16*					i_ptr			= _IS.Lock													(iCount_Lock, i_offset);
		while (P)	{
			CEffect_Rain::Particle*	next	=	P->next;

			// Update
			// P can be zero sometimes and it crashes
			P->time				-=	dt;
			if (P->time<0)	{
				owner.p_free			(P);
				P				=	next;
				continue;
			}

			// Render
			if (::Render->ViewBase.testSphere_dirty(P->bounds.P, P->bounds.R))
			{
				// Build matrix
				float scale			=	P->time / particles_time;
				mScale.scale		(scale,scale,scale);
				mXform.mul_43		(P->mXForm,mScale);

				// XForm verts
				const u32 c_drop	= lights_count ? rain_lit_color(f_rain_color,factor_visual,P->bounds.P,Device.vCameraPosition,lights,lights_count) : u_rain_color;
				DM_Drop->transfer	(mXform,v_ptr,c_drop,i_ptr,pcount*DM_Drop->number_vertices);
				v_ptr			+=	DM_Drop->number_vertices;
				i_ptr			+=	DM_Drop->number_indices;
				pcount			++;

				if (pcount >= particles_cache) {
					// flush
					u32	dwNumPrimitives		= iCount_Lock/3;
					RCache.Vertex.Unlock	(vCount_Lock,hGeom_Drops->vb_stride);
					_IS.Unlock				(iCount_Lock);
					RCache.set_Geometry		(hGeom_Drops);
					RCache.Render			(D3DPT_TRIANGLELIST,v_offset, 0,vCount_Lock,i_offset,dwNumPrimitives);

					v_ptr					= (IRender_DetailModel::fvfVertexOut*)			RCache.Vertex.Lock	(vCount_Lock, hGeom_Drops->vb_stride, v_offset);
					i_ptr					= _IS.Lock										(iCount_Lock, i_offset);

					pcount	= 0;
				}
			}

			P = next;
		}

		// Flush if needed
		vCount_Lock						= pcount*DM_Drop->number_vertices;
		iCount_Lock						= pcount*DM_Drop->number_indices;
		u32	dwNumPrimitives				= iCount_Lock/3;
		RCache.Vertex.Unlock			(vCount_Lock,hGeom_Drops->vb_stride);
		_IS.Unlock						(iCount_Lock);
		if (pcount)	{
			RCache.set_Geometry		(hGeom_Drops);
			RCache.Render			(D3DPT_TRIANGLELIST,v_offset,0,vCount_Lock,i_offset,dwNumPrimitives);
		}
	}
}

const Fsphere& dxRainRender::GetDropBounds() const
{
	return DM_Drop->bv_sphere;
}