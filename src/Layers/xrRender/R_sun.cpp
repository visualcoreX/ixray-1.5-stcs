#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/irenderable.h"
#include "../xrRender/FBasicVisual.h"

#include "../xrRender/R_sun_support.h"

using namespace DirectX;

constexpr float tweak_COP_initial_offs = 1200.0f;

float OLES_SUN_LIMIT_27_01_07 = 100.0f;
 
//////////////////////////////////////////////////////////////////////////
// tables to calculate view-frustum bounds in world space
// note: D3D uses [0..1] range for Z
static Fvector3		corners [8]			= {
	{ -1, -1,  0 },		{ -1, -1, +1},
	{ -1, +1, +1 },		{ -1, +1,  0},
	{ +1, +1, +1 },		{ +1, +1,  0},
	{ +1, -1, +1},		{ +1, -1,  0}
};

static int			facetable[6][4]		= {
	{ 6, 7, 5, 4 },		{ 1, 0, 7, 6 },
	{ 1, 2, 3, 0 },		{ 3, 2, 4, 5 },		
	// near and far planes
	{ 0, 3, 5, 7 },		{  1, 6, 4, 2 },
};

Fvector3		wform	(Fmatrix& m, Fvector3 const& v)
{
	Fvector4	r;
	r.x			= v.x*m._11 + v.y*m._21 + v.z*m._31 + m._41;
	r.y			= v.x*m._12 + v.y*m._22 + v.z*m._32 + m._42;
	r.z			= v.x*m._13 + v.y*m._23 + v.z*m._33 + m._43;
	r.w			= v.x*m._14 + v.y*m._24 + v.z*m._34 + m._44;
	// VERIFY		(r.w>0.f);
	float invW = 1.0f/r.w;
	Fvector3	r3 = { r.x*invW, r.y*invW, r.z*invW };
	return		r3;
}

// Cascade layout. `size` is the side, in metres, of the light-space square the cascade covers;
// every cascade gets the same shadow-map resolution, so the ratio between neighbours is exactly how
// much the shadows coarsen when you step over the border -- that border is the hard-edged quad
// visible around the player on open ground. Console-driven (r2_sun_cascades, r2_sun_cascade0..2) so
// the seam can be pushed out at runtime; re-read every frame from render_sun_cascades().
void CRender::init_cacades()
{
	u32 cascade_count = (u32)clampr(ps_r2_sun_cascades, 2, 3);
	if (m_sun_cascades.size() != cascade_count)
		m_sun_cascades.resize(cascade_count);

	constexpr float fBias = -0.0000025f;

	// With two cascades the middle step is what gets dropped -- keep the far distance.
	float sizes[3] = { ps_r2_sun_cascade0, ps_r2_sun_cascade1, ps_r2_sun_cascade2 };
	if (2 == cascade_count)
		sizes[1] = sizes[2];

	m_sun_cascades[0].reset_chain = true;
	for (u32 i = 0; i < cascade_count; ++i)
	{
		// sizes must grow, otherwise the chained frustum rays walk backwards
		if (i > 0)
			sizes[i] = _max(sizes[i], sizes[i-1] * 1.05f);

		m_sun_cascades[i].size = sizes[i];
		m_sun_cascades[i].bias = m_sun_cascades[i].size*fBias;
	}
}

void CRender::render_sun_cascades ( )
{
	init_cacades();		// pick up console changes


	bool b_need_to_render_sunshafts = RImplementation.Target->need_to_render_sunshafts();
	bool last_cascade_chain_mode = m_sun_cascades.back().reset_chain;
	if ( b_need_to_render_sunshafts )
		m_sun_cascades[m_sun_cascades.size()-1].reset_chain = true;

	for( u32 i = 0; i < m_sun_cascades.size(); ++i )
		render_sun_cascade ( i );

	if ( b_need_to_render_sunshafts )
		m_sun_cascades[m_sun_cascades.size()-1].reset_chain = last_cascade_chain_mode;
}

void CRender::render_sun_cascade ( u32 cascade_ind )
{
	light*			fuckingsun			= (light*)Lights.sun_adapted._get()	;

	// calculate view-frustum bounds in world space
	Fmatrix	ex_project, ex_full, ex_full_inverse;
	{
		ex_project = Device.mProject;
		ex_full.mul					(ex_project,Device.mView);
		XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&ex_full_inverse),
			XMMatrixInverse(nullptr, XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&ex_full))));
	}

	// Compute volume(s) - something like a frustum for infinite directional light
	// Also compute virtual light position and sector it is inside
	CFrustum					cull_frustum;
	xr_vector<Fplane>			cull_planes;
	Fvector3					cull_COP;
	CSector*					cull_sector;
	Fmatrix						cull_xform;
	{
		FPU::m64r					();
		// Lets begin from base frustum
		Fmatrix		fullxform_inv	= ex_full_inverse;
#ifdef	_DEBUG
		typedef		DumbConvexVolume<true>	t_volume;
#else
		typedef		DumbConvexVolume<false>	t_volume;
#endif

		//******************************* Need to be placed after cuboid built **************************
		// Search for default sector - assume "default" or "outdoor" sector is the largest one
		//. hack: need to know real outdoor sector
		CSector*	largest_sector		= 0;
		float		largest_sector_vol	= 0;
		for		(u32 s=0; s<Sectors.size(); s++)
		{
			CSector*			S		= (CSector*)Sectors[s]	;
			dxRender_Visual*		V		= S->root()				;
			float				vol		= V->vis.box.getvolume();
			if (vol>largest_sector_vol)	{
				largest_sector_vol		= vol;
				largest_sector			= S;
			}
		}
		cull_sector	= largest_sector;

		// COP - 100 km away
		cull_COP.mad				(Device.vCameraPosition, fuckingsun->direction, -tweak_COP_initial_offs	);

		// Create approximate ortho-xform
		// view: auto find 'up' and 'right' vectors
		Fmatrix						mdir_View, mdir_Project;
		Fvector						L_dir,L_up,L_right,L_pos;
		L_pos.set					(fuckingsun->position);
		L_dir.set					(fuckingsun->direction).normalize	();
		L_right.set					(1,0,0);					if (_abs(L_right.dotproduct(L_dir))>.99f)	L_right.set(0,0,1);
		L_up.crossproduct			(L_dir,L_right).normalize	();
		L_right.crossproduct		(L_up,L_dir).normalize		();
		mdir_View.build_camera_dir	(L_pos,L_dir,L_up);



		//////////////////////////////////////////////////////////////////////////
#ifdef	_DEBUG
		typedef		FixedConvexVolume<true>		t_cuboid;
#else
		typedef		FixedConvexVolume<false>	t_cuboid;
#endif

		t_cuboid light_cuboid;
		{
			// Initialize the first cascade rays, then each cascade will initialize rays for next one.
			if( cascade_ind == 0 || m_sun_cascades[cascade_ind].reset_chain )
			{
				Fvector3				near_p, edge_vec;
				for	(int p=0; p < 4; p++)	
				{
// 					Fvector asd = Device.vCameraDirection;
// 					asd.mul(-2);
// 					asd.add(Device.vCameraPosition);
// 					near_p		= Device.vCameraPosition;//wform		(fullxform_inv,asd); //
					near_p		= wform		(fullxform_inv,corners[facetable[4][p]]);

					edge_vec	= wform		(fullxform_inv,corners[facetable[5][p]]);
					edge_vec.sub(near_p);
					edge_vec.normalize();

					light_cuboid.view_frustum_rays.push_back	( sun::ray(near_p,edge_vec) );
				}
			}
			else
				light_cuboid.view_frustum_rays = m_sun_cascades[cascade_ind].rays;

			light_cuboid.view_ray.P		= Device.vCameraPosition;
			light_cuboid.view_ray.D		= Device.vCameraDirection;
			light_cuboid.light_ray.P	= L_pos;
			light_cuboid.light_ray.D	= L_dir;
		}

		// THIS NEED TO BE A CONSTATNT
		Fplane light_top_plane;
		light_top_plane.build_unit_normal( L_pos, L_dir );
		float dist = light_top_plane.classify( Device.vCameraPosition );

		float map_size = m_sun_cascades[cascade_ind].size;

	#ifndef USE_DX10
		XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&mdir_Project),
			XMMatrixOrthographicOffCenterLH(-map_size * 0.5f, map_size * 0.5f, -map_size * 0.5f, map_size * 0.5f, 0.1, dist + map_size));
	#else
		XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&mdir_Project),
			XMMatrixOrthographicOffCenterLH(-map_size * 0.5f, map_size * 0.5f, -map_size * 0.5f, map_size * 0.5f, 0.1, dist + 1.41421f * map_size));
	#endif // USE_DX10

		// build viewport xform
		float	view_dim			= float(RImplementation.o.smapsize);
		Fmatrix	m_viewport			= {
			view_dim/2.f,	0.0f,				0.0f,		0.0f,
			0.0f,			-view_dim/2.f,		0.0f,		0.0f,
			0.0f,			0.0f,				1.0f,		0.0f,
			view_dim/2.f,	view_dim/2.f,		0.0f,		1.0f
		};
		Fmatrix m_viewport_inv{};
		XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&m_viewport_inv),
			XMMatrixInverse(nullptr, XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&m_viewport))));

		// snap view-position to pixel
		cull_xform.mul		(mdir_Project,mdir_View	);
		Fmatrix	cull_xform_inv; cull_xform_inv.invert(cull_xform);


		//		light_cuboid.light_cuboid_points.reserve		(9);
		for	(int p=0; p < 8; p++)	{
			Fvector3				xf	= wform		(cull_xform_inv,corners[p]);
			light_cuboid.light_cuboid_points[p] = xf;
		}

		// only side planes
		for (int plane=0; plane < 4; plane++)	
			for (int pt=0; pt < 4; pt++)	
			{
				int asd = facetable[plane][pt];
				light_cuboid.light_cuboid_polys[plane].points[pt] = asd;
			}


			Fvector lightXZshift;
			light_cuboid.compute_caster_model_fixed( cull_planes, lightXZshift, m_sun_cascades[cascade_ind].size,  m_sun_cascades[cascade_ind].reset_chain );
			Fvector proj_view = Device.vCameraDirection;
			proj_view.y = 0;
			proj_view.normalize();
//			lightXZshift.mad(proj_view, 20);

			// Focusing. compute_caster_model_fixed() slides the box forward along the view so that
			// it hugs this cascade's slice of the frustum -- the shadow map is then spent only on
			// what you can actually see. The price is that the box position depends on where you
			// LOOK, and it does so discontinuously: the shift is built from whichever one or two
			// side planes happen to face the camera, so the set changes as you turn and the box
			// jumps. That is the cascade border visibly re-laying itself out while you rotate, and
			// looking down it slides out from under your feet. r2_sun_cascade_focus scales the
			// shift: 1 is the stock fit, 0 pins the box to the camera and the border then does not
			// move at all when you turn (at the cost of half the box being spent behind you).
			// Safe to scale here: the caster cull planes computed above stay a superset, and the
			// cross-fade weights carry any gap that opens up (see below).
			lightXZshift.mul( clampr(ps_r2_sun_cascade_focus, 0.f, 1.f) );

			// Initialize rays for the next cascade
			if( cascade_ind < m_sun_cascades.size()-1 )
				m_sun_cascades[cascade_ind+1].rays =  light_cuboid.view_frustum_rays;

#ifdef	_DEBUG
			static bool draw_debug = false;
			if( draw_debug && cascade_ind == 0 )
				for (u32 it=0; it<cull_planes.size(); it++)
					RImplementation.Target->dbg_addplane(cull_planes[it],it*0xFFF);
#endif

			Fvector cam_shifted = L_pos;
			cam_shifted.add(lightXZshift);

			// rebuild the view transform with the shift.
			mdir_View.identity();
			mdir_View.build_camera_dir	( cam_shifted, L_dir, L_up );
			cull_xform.identity();
			cull_xform.mul		( mdir_Project, mdir_View );
			cull_xform_inv.invert(cull_xform);


			// Create frustum for query
			cull_frustum._clear			();
			for (u32 p=0; p<cull_planes.size(); p++)
				cull_frustum._add		(cull_planes[p]);

			{
				Fvector cam_proj = Device.vCameraPosition;
				constexpr float		align_aim_step_coef = 4.f;
				cam_proj.set(floorf(cam_proj.x/align_aim_step_coef)+align_aim_step_coef/2, floorf(cam_proj.y/align_aim_step_coef)+align_aim_step_coef/2, floorf(cam_proj.z/align_aim_step_coef)+align_aim_step_coef/2);
				cam_proj.mul(align_aim_step_coef);
				Fvector	cam_pixel	= wform		(cull_xform, cam_proj );
				cam_pixel			= wform		(m_viewport, cam_pixel );
				Fvector shift_proj	= lightXZshift;
				cull_xform.transform_dir( shift_proj );
				m_viewport.transform_dir( shift_proj );

				constexpr float	align_granularity = 4.f;
				shift_proj.x = shift_proj.x > 0 ? align_granularity : -align_granularity;
				shift_proj.y = shift_proj.y > 0 ? align_granularity : -align_granularity;
				shift_proj.z = 0;

				cam_pixel.x	= cam_pixel.x/align_granularity-floorf	(cam_pixel.x/align_granularity);
				cam_pixel.y	= cam_pixel.y/align_granularity-floorf	(cam_pixel.y/align_granularity);
				cam_pixel.x *= align_granularity;
				cam_pixel.y *= align_granularity;
				cam_pixel.z = 0;

				cam_pixel.sub		( shift_proj );

				m_viewport_inv.transform_dir	(cam_pixel);
				cull_xform_inv.transform_dir	(cam_pixel);
				Fvector diff		= cam_pixel;
				static float sign_test = -1.f;
				diff.mul			(sign_test);
				Fmatrix adjust;		adjust.translate(diff);
				cull_xform.mulB_44	(adjust);
			}

			m_sun_cascades[cascade_ind].xform = cull_xform;

			s32		limit					= RImplementation.o.smapsize-1;
			fuckingsun->X.D.minX			= 0;
			fuckingsun->X.D.maxX			= limit;
			fuckingsun->X.D.minY			= 0;
			fuckingsun->X.D.maxY			= limit;

		// full-xform
		FPU::m24r			();
	}

	// Begin SMAP-render
	{
		bool	bSpecialFull					= mapNormalPasses[1][0].size() || mapMatrixPasses[1][0].size() || mapSorted.size();
		VERIFY									(!bSpecialFull);
		HOM.Disable								();
		phase									= PHASE_SMAP;
		if (RImplementation.o.Tshadows)	r_pmask	(true,true	);
		else							r_pmask	(true,false	);
		//		fuckingsun->svis.begin					();
	}

	// Fill the database
	r_dsgraph_render_subspace				(cull_sector, &cull_frustum, cull_xform, cull_COP, TRUE);

	// Finalize & Cleanup
	fuckingsun->X.D.combine					= cull_xform;	//*((Fmatrix*)&m_LightViewProj);

	// Render shadow-map
	//. !!! We should clip based on shrinked frustum (again)
	{
		bool	bNormal							= mapNormalPasses[0][0].size() || mapMatrixPasses[0][0].size();
		bool	bSpecial						= mapNormalPasses[1][0].size() || mapMatrixPasses[1][0].size() || mapSorted.size();
		if ( bNormal || bSpecial)	{
			Target->phase_smap_direct			(fuckingsun	, SE_SUN_FAR	);
			RCache.set_xform_world				(Fidentity					);
			RCache.set_xform_view				(Fidentity					);
			RCache.set_xform_project			(fuckingsun->X.D.combine	);	
			r_dsgraph_render_graph				(0)	;
			if (ps_r2_ls_flags.test(R2FLAG_SUN_DETAILS))	
				Details->Render					()	;
			fuckingsun->X.D.transluent			= FALSE;
			if (bSpecial)						{
				fuckingsun->X.D.transluent			= TRUE;
				Target->phase_smap_direct_tsh		(fuckingsun, SE_SUN_FAR);
				r_dsgraph_render_graph				(1);			// normal level, secondary priority
				r_dsgraph_render_sorted				( );			// strict-sorted geoms
			}
		}
	}

	// End SMAP-render
	{
		//		fuckingsun->svis.end					();
		r_pmask									(true,false);
	}

	// Accumulate
	Target->phase_accumulator	();

#ifdef USE_DX10
	if ( Target->use_minmax_sm_this_frame()	)
	{
		PIX_EVENT(SE_SUN_NEAR_MINMAX_GENERATE);
		Target->create_minmax_SM();
	}

	PIX_EVENT(SE_SUN_NEAR);
#endif // USE_DX10

	// Cross-fade weights, handed to the accumulation shader. Cascade i contributes
	//     w_i    = a_i * PROD(j<i) (1 - a_j)      and the last one takes the whole remainder,
	//     w_last =       PROD(j<last) (1 - a_j),
	// where a_j is 1 deep inside cascade j's box and slides to 0 over the last
	// ps_r2_sun_cascade_blend metres of it. The product form sums to exactly 1 for ANY layout of
	// the boxes, and that matters here: the boxes are NOT nested. compute_caster_model_fixed()
	// slides each one forward along the view to hug its slice of the frustum, so cascade 0 sticks
	// out of cascade 1 behind the player. A simpler "subtract only the previous cascade" formula
	// assumes nesting and pays for it with up to 2x sunlight exactly there.
	// Widths go in normalised to each box (shadow tc spans the whole side), hence the sizes of the
	// two preceding cascades.
	Fvector4	blend;
	{
		const float	band		= _max(ps_r2_sun_cascade_blend, 0.001f);
		const float	size_cur	= m_sun_cascades[cascade_ind].size;
		const float	size_prev	= cascade_ind > 0 ? m_sun_cascades[cascade_ind-1].size : size_cur;
		blend.set	(band/size_cur, band/size_prev, cascade_ind > 0 ? 1.f : 0.f,
					 cascade_ind > 1 ? band/m_sun_cascades[cascade_ind-2].size : 0.f);
	}
	Fmatrix&	xform_prev	= m_sun_cascades[cascade_ind > 0 ? cascade_ind-1 : cascade_ind].xform;
	Fmatrix&	xform_prev2	= m_sun_cascades[cascade_ind > 1 ? cascade_ind-2 : cascade_ind].xform;

	if( cascade_ind == 0 )
		Target->accum_direct_cascade		(SE_SUN_NEAR, m_sun_cascades[cascade_ind].xform, xform_prev, xform_prev2, m_sun_cascades[cascade_ind].bias, blend );
	else
		if( cascade_ind < m_sun_cascades.size()-1 )
			Target->accum_direct_cascade		(SE_SUN_MIDDLE, m_sun_cascades[cascade_ind].xform, xform_prev, xform_prev2, m_sun_cascades[cascade_ind].bias, blend);
		else
			Target->accum_direct_cascade		(SE_SUN_FAR, m_sun_cascades[cascade_ind].xform, xform_prev, xform_prev2, m_sun_cascades[cascade_ind].bias, blend);

	// Restore XForms
	RCache.set_xform_world		(Fidentity			);
	RCache.set_xform_view		(Device.mView		);
	RCache.set_xform_project	(Device.mProject	);
}
