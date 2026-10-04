// DetailManager.cpp: implementation of the CDetailManager class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#include "DetailManager.h"
#include "cl_intersect.h"
#include "../../xrCore/hitch_trace.h"

#ifdef _EDITOR
#	include "ESceneClassList.h"
#	include "Scene.h"
#	include "SceneObject.h"
#	include "igame_persistent.h"
#	include "environment.h"
#else
#	include "../../xrEngine/igame_persistent.h"
#	include "../../xrEngine/environment.h"
#endif

const float dbgOffset			= 0.f;
const int	dbgItems			= 128;

//--------------------------------------------------- Decompression
static int magic4x4[4][4] =
{
 	{ 0, 14,  3, 13},
	{11,  5,  8,  6},
	{12,  2, 15,  1},
	{ 7,  9,  4, 10}
};

void bwdithermap	(int levels, int magic[16][16])
{
	/* Get size of each step */
    float N = 255.0f / (levels - 1);

	/*
	* Expand 4x4 dither pattern to 16x16.  4x4 leaves obvious patterning,
	* and doesn't give us full intensity range (only 17 sublevels).
	*
	* magicfact is (N - 1)/16 so that we get numbers in the matrix from 0 to
	* N - 1: mod N gives numbers in 0 to N - 1, don't ever want all
	* pixels incremented to the next level (this is reserved for the
	* pixel value with mod N == 0 at the next level).
	*/

    float	magicfact = (N - 1) / 16;
    for ( int i = 0; i < 4; i++ )
		for ( int j = 0; j < 4; j++ )
			for ( int k = 0; k < 4; k++ )
				for ( int l = 0; l < 4; l++ )
					magic[4*k+i][4*l+j] =
					(int)(0.5 + magic4x4[i][j] * magicfact +
					(magic4x4[k][l] / 16.) * magicfact);
}
//--------------------------------------------------- Decompression

void CDetailManager::SSwingValue::lerp(const SSwingValue& A, const SSwingValue& B, float f)
{
	float fi	= 1.f-f;
	amp1		= fi*A.amp1  + f*B.amp1;
	amp2		= fi*A.amp2  + f*B.amp2;
	rot1		= fi*A.rot1  + f*B.rot1;
	rot2		= fi*A.rot2  + f*B.rot2;
	speed		= fi*A.speed + f*B.speed;
}
//---------------------------------------------------

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CDetailManager::CDetailManager	()
{
	dtFS 		= 0;
	dtSlots		= 0;
	soft_Geom	= 0;
	hw_Geom		= 0;
	hw_BatchSize= 0;
	hw_VB		= 0;
	hw_IB		= 0;
	m_time_rot_1 = 0;
	m_time_rot_2 = 0;
	m_time_pos	= 0;
	m_global_time_old = 0;
}

CDetailManager::~CDetailManager	()
{

}
/*
*/
#ifndef _EDITOR

/*
void dump	(CDetailManager::vis_list& lst)
{
	for (int i=0; i<lst.size(); i++)
	{
		Msg("%8x / %8x / %8x",	lst[i]._M_start, lst[i]._M_finish, lst[i]._M_end_of_storage._M_data);
	}
}
*/
void CDetailManager::Load		()
{
	// Open file stream
	if (!FS.exist("$level$","level.details"))
	{
		dtFS	= NULL;
		return;
	}

	string_path			fn;
	FS.update_path		(fn,"$level$","level.details");
	dtFS				= FS.r_open(fn);

	// Header
	dtFS->r_chunk_safe	(0,&dtH,sizeof(dtH));
	R_ASSERT			(dtH.version == DETAIL_VERSION);
	u32 m_count			= dtH.object_count;

	// Models
	IReader* m_fs		= dtFS->open_chunk(1);
	for (u32 m_id = 0; m_id < m_count; m_id++)
	{
		CDetail*		dt	= xr_new<CDetail> ();
		IReader* S			= m_fs->open_chunk(m_id);
		dt->Load			(S);
		objects.push_back	(dt);
		S->close			();
	}
	m_fs->close		();

	// Get pointer to database (slots)
	IReader* m_slots	= dtFS->open_chunk(2);
	dtSlots				= (DetailSlot*)m_slots->pointer();
	m_slots->close		();

	// Initialize 'vis' and 'cache'
	for (u32 i=0; i<3; ++i)	m_visibles[i].resize(objects.size());
	for (u32 i=0; i<3; ++i)	m_visibles_shadow[i].resize(objects.size());
	for (u32 i=0; i<3; ++i)	m_visibles_shadow_fade[i].resize(objects.size());
	cache_Initialize	();

	// Make dither matrix
	bwdithermap		(2,dither);

	// Hardware specific optimizations
	if (UseVS())	hw_Load		();
	else			soft_Load	();

	// swing desc
	// normal
	swing_desc[0].amp1	= pSettings->r_float("details","swing_normal_amp1");
	swing_desc[0].amp2	= pSettings->r_float("details","swing_normal_amp2");
	swing_desc[0].rot1	= pSettings->r_float("details","swing_normal_rot1");
	swing_desc[0].rot2	= pSettings->r_float("details","swing_normal_rot2");
	swing_desc[0].speed	= pSettings->r_float("details","swing_normal_speed");
	// fast
	swing_desc[1].amp1	= pSettings->r_float("details","swing_fast_amp1");
	swing_desc[1].amp2	= pSettings->r_float("details","swing_fast_amp2");
	swing_desc[1].rot1	= pSettings->r_float("details","swing_fast_rot1");
	swing_desc[1].rot2	= pSettings->r_float("details","swing_fast_rot2");
	swing_desc[1].speed	= pSettings->r_float("details","swing_fast_speed");
}
#endif
void CDetailManager::Unload		()
{
	if (UseVS())	hw_Unload	();
	else			soft_Unload	();

	for (DetailIt it=objects.begin(); it!=objects.end(); it++){
		(*it)->Unload();
		xr_delete		(*it);
    }
	objects.clear		();
	m_visibles[0].clear	();
	m_visibles[1].clear	();
	m_visibles[2].clear	();
	m_visibles_shadow[0].clear	();
	m_visibles_shadow[1].clear	();
	m_visibles_shadow[2].clear	();
	m_visibles_shadow_fade[0].clear	();
	m_visibles_shadow_fade[1].clear	();
	m_visibles_shadow_fade[2].clear	();
	m_shadow_slots.clear		();
	FS.r_close			(dtFS);
}

extern ECORE_API float r_ssaDISCARD;

void CDetailManager::UpdateVisibleM()
{
	Fvector		EYE				= Device.vCameraPosition_saved;
	
	CFrustum	View;
	View.CreateFromMatrix		(Device.mFullTransform_saved, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
	
	CFrustum View_old;
	Fmatrix Viewm_old = Device.mFullTransform;
	View_old.CreateFromMatrix(Viewm_old, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
	
	// The cache reaches dm_fade metres; the console knob only ever pulls that in, never past it.
	float fade_limit			= _min(dm_fade, float(ps_r__Detail_radius));
	fade_limit					= fade_limit*fade_limit;
	float fade_start			= 1.f;		fade_start=fade_start*fade_start;
	float fade_range			= fade_limit-fade_start;
	// The size thresholds are FOV-dependent (r_ssaDISCARD = ssaDISCARD^2 / (screen * (90/fov)^2)) and the
	// renderer recomputes that global EVERY frame. With the 3D lens the frames alternate between the lens fov
	// (a few degrees) and the base view (~75), while each slot re-picks its items only every 15-30 frames
	// (S.frame below) -- on whichever of the two it happens to land. So the far, faded clumps were thrown
	// away on one refresh and kept on the next, and in the lens they visibly popped in and out. The slot
	// selection now uses ONE fov: the lens one while a lensed scope is aimed (recorded in Render on the
	// lens frames), the frame's own otherwise -- which is exactly what the global gave before.
	const float	ssa_fov			= (m_ssa_fov > 1.f) ? m_ssa_fov : Device.fFOV;
	IRender_Target* ssa_T		= RImplementation.getTarget();
	const float	ssa_screen		= float(ssa_T->get_width()*ssa_T->get_height()) * _sqr(90.f/ssa_fov) * (EPS_S+ps_r__LOD);
	const float	ssa_discard		= _sqr(ps_r__ssaDISCARD) / ssa_screen;
	float		r_ssaCHEAP		= 16*ssa_discard;

	// Grass shadows: a blade's shadow is lost in the cascade texels (and the fog) long before the grass itself
	// fades out, so the sun cascades only get the slots within this radius -- see RenderShadow.
	float shadow_limit			= _min(dm_fade, float(ps_r2_sun_details_radius));
	shadow_limit				= shadow_limit*shadow_limit;
	m_shadow_slots.clear		();

	for (u8 i = 0; i != 3; i++) {
		auto& list = m_visibles[i];
		for (u32 j = 0; j != list.size(); j++) {
			list[j].clear();
		}
	}

	// Initialize 'vis' and 'cache'
	// Collect objects for rendering
	Device.Statistic->RenderDUMP_DT_VIS.Begin	();
	for (int _mz=0; _mz<dm_cache1_line; _mz++){
		for (int _mx=0; _mx<dm_cache1_line; _mx++){
			CacheSlot1& MS		= cache_level1[_mz][_mx];
			if (MS.empty)		continue;
			u32 mask			= 0xff;
			u32 res				= View.testSAABB		(MS.vis.sphere.P,MS.vis.sphere.R,MS.vis.box.data(),mask);
			if (fcvNone==res)						 	continue;	// invisible-view frustum
			// test slots
			for (int _i=0; _i<dm_cache1_count*dm_cache1_count; _i++){
				Slot*	PS		= *MS.slots[_i];
				Slot& 	S 		= *PS;

				// if slot empty - continue
				if (S.empty)	continue;

				// if upper test = fcvPartial - test inner slots
				if (fcvPartial==res){
					u32 _mask	= mask;
					u32 _res	= View.testSAABB			(S.vis.sphere.P,S.vis.sphere.R,S.vis.box.data(),_mask);
					if (fcvNone==_res)						continue;	// invisible-view frustum
				}
#ifndef _EDITOR
				if (!RImplementation.HOM.visible(S.vis))	continue;	// invisible-occlusion
#endif
				// Add to visibility structures
				if (Device.dwFrame>S.frame){
					// Calc fade factor	(per slot)
					float	dist_sq		= EYE.distance_to_sqr	(S.vis.sphere.P);
					if		(dist_sq>fade_limit)				continue;
					float	alpha		= (dist_sq<fade_start)?0.f:(dist_sq-fade_start)/fade_range;
					float	alpha_i		= 1.f - alpha;
					float	dist_sq_rcp	= 1.f / dist_sq;

					S.frame			= Device.dwFrame+Random.randI(15,30);
					for (int sp_id=0; sp_id<dm_obj_in_slot; sp_id++){
						SlotPart&			sp	= S.G		[sp_id];
						if (sp.id==DetailSlot::ID_Empty)	continue;

						sp.r_items[0].clear();
						sp.r_items[1].clear();
						sp.r_items[2].clear();

						float				R		= objects	[sp.id]->bv_sphere.R;
						float				Rq_drcp	= R*R*dist_sq_rcp;	// reordered expression for 'ssa' calc

						SlotItem			**siIT=&(*sp.items.begin()), **siEND=&(*sp.items.end());
						for (; siIT!=siEND; siIT++){
							SlotItem& Item			= *(*siIT);
							float   scale			= Item.scale_calculated	= Item.scale*alpha_i;
							float	ssa				= scale*scale*Rq_drcp;
							if (ssa < ssa_discard) continue;
							u32		vis_id			= 0;
							if (ssa > r_ssaCHEAP)	vis_id = Item.vis_ID;
							
							sp.r_items[vis_id].push_back	(*siIT);

//2							visible[vis_id][sp.id].push_back(&Item);
						}
					}
				}
				for (int sp_id=0; sp_id<dm_obj_in_slot; sp_id++){
					SlotPart&			sp	= S.G		[sp_id];
					if (sp.id==DetailSlot::ID_Empty)	continue;
					if (!sp.r_items[0].empty()) m_visibles[0][sp.id].push_back(&sp.r_items[0]);
					if (!sp.r_items[1].empty()) m_visibles[1][sp.id].push_back(&sp.r_items[1]);
					if (!sp.r_items[2].empty()) m_visibles[2][sp.id].push_back(&sp.r_items[2]);
				}
				if (EYE.distance_to_sqr(S.vis.sphere.P) <= shadow_limit)
					m_shadow_slots.push_back(PS);
			}
		}
	}
	Device.Statistic->RenderDUMP_DT_VIS.End	();
}

void CDetailManager::Render	()
{
#ifndef _EDITOR
	if (0==dtFS)						return;
	if (!psDeviceFlags.is(rsDetails))	return;
#endif

#ifndef _EDITOR
	// the fov UpdateVisibleM sizes the grass at: a lens frame's own, and kept through the normal frames in
	// between while the lens is aimed (they would otherwise flip it back every other frame)
	if (!g_pGamePersistent->m_bLensAimActive || g_pGamePersistent->m_bLensFrameNow)
		m_ssa_fov			= Device.fFOV;
#else
	m_ssa_fov				= Device.fFOV;
#endif

	// MT
	MT_SYNC					();

	Device.Statistic->RenderDUMP_DT_Render.Begin	();

#ifndef _EDITOR
	float factor			= g_pGamePersistent->Environment().wind_strength_factor;
#else
	float factor			= 0.3f;
#endif
	// The wind factor picks the sway between the calm and the windy set, and anything that makes it step
	// (a weather ambient effect handing the wind back, say) moved every blade to a new pose in one frame.
	// Follow it with a short lag instead (~0.3 s) -- a real change of wind still comes through at once
	// to the eye, a step no longer does.
	{
		static float s_factor	= -1.f;
		if (s_factor < 0.f)		s_factor = factor;
		else					s_factor += (factor - s_factor) * _min(1.f, Device.fTimeDelta * 3.f);
		factor					= s_factor;
	}
	swing_current.lerp		(swing_desc[0],swing_desc[1],factor);

	RCache.set_CullMode		(CULL_NONE);
	RCache.set_xform_world	(Fidentity);
	if (UseVS())			hw_Render	();
	else					soft_Render	();
	RCache.set_CullMode		(CULL_CCW);
	Device.Statistic->RenderDUMP_DT_Render.End	();
	m_frame_rendered		= Device.dwFrame;
}

// A sun cascade's grass: the near slots UpdateVisibleM kept for shadows, minus the ones outside this cascade.
// Every slot used to go into every cascade -- the whole camera view, out to the grass radius, three times a
// frame, each time rebuilding the instance constants and sending batch after batch the cascade then clipped.
void CDetailManager::RenderShadow(const CFrustum& cascade)
{
#ifndef _EDITOR
	if (0==dtFS)						return;
	if (!psDeviceFlags.is(rsDetails))	return;
#endif

	MT_SYNC					();

	for (u8 i = 0; i != 3; i++) {
		auto& list = m_visibles_shadow[i];
		auto& fade = m_visibles_shadow_fade[i];
		for (u32 j = 0; j != list.size(); j++) {
			list[j].clear();
			fade[j].clear();
		}
	}

	// A hard radius made the shadows switch on a whole 2 m slot at a time as you walk. Across the outer band the
	// blades are shrunk towards their roots instead (shadow pass only), so a shadow grows in rather than appears.
	const Fvector&	EYE		= Device.vCameraPosition_saved;
	const float		R		= _min(dm_fade, float(ps_r2_sun_details_radius));
	const float		band	= _max(4.f, R*0.3f);
	const float		inner	= R - band;

	bool any				= false;
	for (Slot* PS : m_shadow_slots) {
		Slot& S				= *PS;
		const float	dist	= EYE.distance_to(S.vis.sphere.P);
		if (dist >= R)		continue;
		float k				= (dist <= inner) ? 1.f : (R - dist)/band;
		k					= k*k*(3.f - 2.f*k);
		u32 mask			= 0xff;
		if (fcvNone == cascade.testSAABB(S.vis.sphere.P, S.vis.sphere.R, S.vis.box.data(), mask))
			continue;
		for (int sp_id=0; sp_id<dm_obj_in_slot; sp_id++){
			SlotPart&		sp	= S.G[sp_id];
			if (sp.id==DetailSlot::ID_Empty)	continue;
			for (u32 v = 0; v != 3; v++) {
				if (sp.r_items[v].empty())		continue;
				m_visibles_shadow[v][sp.id].push_back(&sp.r_items[v]);
				m_visibles_shadow_fade[v][sp.id].push_back(k);
				any			= true;
			}
		}
	}
	if (!any)				return;

	m_render_visibles		= m_visibles_shadow;
	Render					();
	m_render_visibles		= m_visibles;
}

void __stdcall	CDetailManager::MT_CALC		()
{
#ifndef _EDITOR
	if (0==RImplementation.Details)		return;	// possibly deleted
	if (0==dtFS)						return;
	if (!psDeviceFlags.is(rsDetails))	return;
#endif    

	MT.Enter					();
	if (m_frame_calc!=Device.dwFrame)	
		if ((m_frame_rendered+1)==Device.dwFrame) //already rendered
		{
			Fvector		EYE				= Device.vCameraPosition_saved;
			int s_x	= iFloor			(EYE.x/dm_slot_size+.5f);
			int s_z	= iFloor			(EYE.z/dm_slot_size+.5f);

			hitch::zone					hz("details/cache_and_visible");
			{
				hitch::zone				hz_cache("details/cache_update");
				Device.Statistic->RenderDUMP_DT_Cache.Begin	();
				cache_Update			(s_x,s_z,EYE,dm_max_decompress);
				Device.Statistic->RenderDUMP_DT_Cache.End	();
			}

			{
				// split by the lens: aiming a lensed scope sizes the grass at the lens fov (see UpdateVisibleM)
				hitch::zone				hz_vis(g_pGamePersistent->m_bLensAimActive ? "details/visible_lens_aim" : "details/visible");
				UpdateVisibleM			();
			}
			m_frame_calc				= Device.dwFrame;
		}
	MT.Leave					        ();
}
