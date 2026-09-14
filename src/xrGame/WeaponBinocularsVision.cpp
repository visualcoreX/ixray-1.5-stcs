#include "stdafx.h"
#include "WeaponBinocularsVision.h"
#include "WeaponBinoculars.h"
#include "ui\UIFrameWindow.h"
#include "entity_alive.h"
#include "visual_memory_manager.h"
#include "actor.h"
#include "actor_memory.h"
#include "relation_registry.h"
#include "object_broker.h"

#include "game_base_space.h"
#include "Level.h"
#include "game_cl_base.h"
#include "AI/Monsters/BaseMonster/base_monster.h"
#include "../xrEngine/igame_persistent.h"

#define RECT_SIZE	11

extern u32 C_ON_ENEMY;
extern u32 C_ON_NEUTRAL;
extern u32 C_ON_FRIEND;

struct FindVisObjByObject{
	const CObject*			O;
	FindVisObjByObject(const CObject* o):O(o){}
	bool operator () (const SBinocVisibleObj* vis){
		return (O==vis->m_object);
	}
};

void SBinocVisibleObj::create_default(u32 color)
{
	Frect r = {0,0,RECT_SIZE,RECT_SIZE};
	m_lt.InitTexture			("ui\\ui_enemy_frame");m_lt.SetWndRect(r);
	m_lb.InitTexture			("ui\\ui_enemy_frame");m_lb.SetWndRect(r);
	m_rt.InitTexture			("ui\\ui_enemy_frame");m_rt.SetWndRect(r);
	m_rb.InitTexture			("ui\\ui_enemy_frame");m_rb.SetWndRect(r);

	m_lt.SetOriginalRect		(Frect().set(0,				0,				RECT_SIZE,		RECT_SIZE)	);
	m_lb.SetOriginalRect		(Frect().set(0,				32-RECT_SIZE,	RECT_SIZE,		32)			);
	m_rt.SetOriginalRect		(Frect().set(32-RECT_SIZE,	0,				32,				RECT_SIZE)	);
	m_rb.SetOriginalRect		(Frect().set(32-RECT_SIZE,	32-RECT_SIZE,	32,				32)			);


	u32 clr			= subst_alpha(color,128);
	m_lt.SetColor	(clr);
	m_lb.SetColor	(clr);
	m_rt.SetColor	(clr);
	m_rb.SetColor	(clr);

	cur_rect.set	(0,0, UI_BASE_WIDTH,UI_BASE_HEIGHT);

	m_flags.zero	();

	m_flags.set	(flCornerLT|flCornerLB|flCornerRT|flCornerRB, TRUE);
}

// THE 2D EYEPIECE MAGNIFIES THE WORLD A SECOND TIME. The camera renders only its share of the optic's
// power (CWeapon::Scope2DCameraFOV) and the post-process stretches the picture inside the glass by the rest,
// bending the outer ring as well (postprocess.ps pp_zoom_uv: a displayed point d samples the world at
// s = d*k(d)/z about the screen centre, k = 1 - w*t^2 past PP_DIST_R0 of the glass radius). A frame
// projected with the camera alone therefore sat off its target by that factor. This is the inverse, in NDC:
// d = s*z/k(d), solved by a few fixed-point steps (k stays close to 1, so it settles at once).
// zc = pp_zoom_circle (xy = glass radii in screen uv, z = factor), w = pp_scope_shadow.w (the distortion).
static const float EYEPIECE_DIST_R0 = 0.45f;	// postprocess.ps PP_DIST_R0
static void eyepiece_map(Fvector2& p, const Fvector4& zc, float w)
{
	const float rx = 2.f * zc.x, ry = 2.f * zc.y;	// uv radii -> NDC
	const float z  = _max(zc.z, 1.f);
	Fvector2 d; d.set(p.x * z, p.y * z);
	for (int i = 0; i < 4; ++i)
	{
		const float rr	= _sqrt(_sqr(d.x / rx) + _sqr(d.y / ry));
		float t			= (rr - EYEPIECE_DIST_R0) / (1.f - EYEPIECE_DIST_R0);
		clamp			(t, 0.f, 1.f);
		float k			= 1.f - w * t * t;
		if (k < 0.05f)	k = 0.05f;
		d.set			(p.x * z / k, p.y * z / k);
	}
	p = d;
}

static bool eyepiece_inside(const Fvector2& ndc, const Fvector4& zc)
{
	return _sqr(ndc.x / (2.f * zc.x)) + _sqr(ndc.y / (2.f * zc.y)) <= 1.f;
}

static Fvector2 ui_to_ndc(const Fvector2& ui)
{
	return Fvector2().set(ui.x / UI_BASE_WIDTH * 2.f - 1.f, 1.f - ui.y / UI_BASE_HEIGHT * 2.f);
}

void SBinocVisibleObj::Draw()
{
	if(m_flags.test(flVisObjNotValid)) return;

	if (m_flags.test(flCornerLT))	m_lt.Draw();
	if (m_flags.test(flCornerLB))	m_lb.Draw();
	if (m_flags.test(flCornerRT))	m_rt.Draw();
	if (m_flags.test(flCornerRB))	m_rb.Draw();
}

void SBinocVisibleObj::Update(bool eyepiece)
{
	m_flags.set		(	flVisObjNotValid,TRUE);

	if (!m_object->Visual())	return;		// GS parity: an object without a visual has no box to frame

	// the eyepiece is only there while the scope publishes its circle (a lens frame or an optic that is not
	// up yet clears it) -- without one the frames project exactly as before
	Fvector4	zc;		zc.set(0.f, 0.f, 1.f, 0.f);
	float		zw		= 0.f;
	if (eyepiece && g_pGamePersistent && g_pGamePersistent->pp_zoom_circle.x > 0.f)
	{
		zc	= g_pGamePersistent->pp_zoom_circle;
		zw	= g_pGamePersistent->pp_scope_shadow.w;
	}
	const bool mapped = zc.x > 0.f && zc.y > 0.f;

	Fbox		b		= m_object->Visual()->getVisData().box;

	Fmatrix				xform;
	xform.mul			(Device.mFullTransform,m_object->XFORM());
	Fvector2	mn		={flt_max,flt_max},mx={flt_min,flt_min};

	for (u32 k=0; k<8; ++k){
		Fvector p;
		b.getpoint		(k,p);
		xform.transform	(p);
		Fvector2 q;		q.set(p.x, p.y);
		if (mapped)		eyepiece_map(q, zc, zw);
		mn.x			= _min(mn.x,q.x);
		mn.y			= _min(mn.y,q.y);
		mx.x			= _max(mx.x,q.x);
		mx.y			= _max(mx.y,q.y);
	}
	static Frect screen_rect={-1.0f, -1.0f, 1.0f, 1.0f};

	Frect				new_rect;
	new_rect.lt			= mn;
	new_rect.rb			= mx;

	if( FALSE == screen_rect.intersected(new_rect) ) return;
	if( new_rect.in(screen_rect.lt) && new_rect.in(screen_rect.rb) ) return;
	// through an eyepiece only what is IN the glass is seen: a target whose frame centre lies on the scope
	// body is not framed at all
	if (mapped && !eyepiece_inside(Fvector2().set((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f), zc))	return;
	
	std::swap	(mn.y,mx.y);
	mn.x		= (1.f + mn.x)/2.f * UI_BASE_WIDTH;
	mx.x		= (1.f + mx.x)/2.f * UI_BASE_WIDTH;
	mn.y		= (1.f - mn.y)/2.f * UI_BASE_HEIGHT;
	mx.y		= (1.f - mx.y)/2.f * UI_BASE_HEIGHT;

	if(mx.x-mn.x<RECT_SIZE)
		mx.x = mn.x+RECT_SIZE;

	if(mx.y-mn.y<RECT_SIZE)
		mx.y = mn.y+RECT_SIZE;

	if (m_flags.is(flTargetLocked)){
		cur_rect.lt.set	(mn);
		cur_rect.rb.set	(mx);
	}else{
		cur_rect.lt.x	+= (mn.x-cur_rect.lt.x)*m_upd_speed*Device.fTimeDelta;
		cur_rect.lt.y	+= (mn.y-cur_rect.lt.y)*m_upd_speed*Device.fTimeDelta;
		cur_rect.rb.x	+= (mx.x-cur_rect.rb.x)*m_upd_speed*Device.fTimeDelta;
		cur_rect.rb.y	+= (mx.y-cur_rect.rb.y)*m_upd_speed*Device.fTimeDelta;
		if (mn.similar(cur_rect.lt,2.f)&&mx.similar(cur_rect.rb,2.f)){ 
			// target locked
			m_flags.set(flTargetLocked,TRUE);
			u32 clr	= subst_alpha(m_lt.GetColor(),255);

			//-----------------------------------------------------
			CActor* pActor = NULL;
			if (IsGameTypeSingle()) pActor = Actor();
			else
			{
				if (Level().CurrentViewEntity())
				{
					pActor = smart_cast<CActor*> (Level().CurrentViewEntity());
				}
			}
			if (pActor) 
			{
				//-----------------------------------------------------

				CInventoryOwner* our_inv_owner		= smart_cast<CInventoryOwner*>(pActor);
				CInventoryOwner* others_inv_owner	= smart_cast<CInventoryOwner*>(m_object);
				CBaseMonster	*monster			= smart_cast<CBaseMonster*>(m_object);

				if(our_inv_owner && others_inv_owner && !monster){
					if (IsGameTypeSingle())
					{
						switch(RELATION_REGISTRY().GetRelationType(others_inv_owner, our_inv_owner))
						{
						case ALife::eRelationTypeEnemy:
							clr = C_ON_ENEMY; break;
						case ALife::eRelationTypeNeutral:
							clr = C_ON_NEUTRAL; break;
						case ALife::eRelationTypeFriend:
							clr = C_ON_FRIEND; break;
						}
					}
					else
					{
						CEntityAlive* our_ealive		= smart_cast<CEntityAlive*>(pActor);
						CEntityAlive* others_ealive		= smart_cast<CEntityAlive*>(m_object);
						if (our_ealive && others_ealive)
						{
							if (Game().IsEnemy(our_ealive, others_ealive))
								clr = C_ON_ENEMY;
							else
								clr = C_ON_FRIEND;
						}
					}
				}
			}

			m_lt.SetColor	(clr);
			m_lb.SetColor	(clr);
			m_rt.SetColor	(clr);
			m_rb.SetColor	(clr);
		}
	}

	m_lt.SetWndPos		( Fvector2().set((cur_rect.lt.x)+2,	(cur_rect.lt.y)+2) );
	m_lb.SetWndPos		( Fvector2().set((cur_rect.lt.x)+2,	(cur_rect.rb.y)-14) );
	m_rt.SetWndPos		( Fvector2().set((cur_rect.rb.x)-14,	(cur_rect.lt.y)+2) );
	m_rb.SetWndPos		( Fvector2().set((cur_rect.rb.x)-14,	(cur_rect.rb.y)-14) );

	// ...and of a framed target, only the corners that fall inside the glass are drawn
	m_flags.set			(flCornerLT, !mapped || eyepiece_inside(ui_to_ndc(cur_rect.lt), zc));
	m_flags.set			(flCornerLB, !mapped || eyepiece_inside(ui_to_ndc(Fvector2().set(cur_rect.lt.x, cur_rect.rb.y)), zc));
	m_flags.set			(flCornerRT, !mapped || eyepiece_inside(ui_to_ndc(Fvector2().set(cur_rect.rb.x, cur_rect.lt.y)), zc));
	m_flags.set			(flCornerRB, !mapped || eyepiece_inside(ui_to_ndc(cur_rect.rb), zc));

	m_flags.set			(flVisObjNotValid, FALSE);
}


CBinocularsVision::CBinocularsVision(const shared_str& sect)
{
	Load							(sect);
}

CBinocularsVision::~CBinocularsVision()
{
	m_snd_found.destroy	();
	delete_data			(m_active_objects);
}

void CBinocularsVision::Update()
{
	if (g_dedicated_server)
		return;
	//-----------------------------------------------------
	const CActor* pActor = NULL;
	if (IsGameTypeSingle()) pActor = Actor();
	else
	{
		if (Level().CurrentViewEntity())
		{
			pActor = smart_cast<const CActor*> (Level().CurrentViewEntity());
		}
	}
	if (!pActor) return;
	//-----------------------------------------------------
	const CVisualMemoryManager::VISIBLES& vVisibles = pActor->memory().visual().objects();

	VIS_OBJECTS_IT	it = m_active_objects.begin();
	for(;it!=m_active_objects.end();++it)
		(*it)->m_flags.set					(flVisObjNotValid, TRUE) ;


	CVisualMemoryManager::VISIBLES::const_iterator v_it = vVisibles.begin();
	for (; v_it!=vVisibles.end(); ++v_it)
	{
		const CObject*	_object_			= (*v_it).m_object;
		// GS/CoP use visible_RIGHT_now: `visible_now` also answers true for anything seen within
		// still_visible_time, so a frame kept hanging on a target that had already gone out of sight.
		if (!pActor->memory().visual().visible_right_now(smart_cast<const CGameObject*>(_object_)))
			continue;

		CObject* object_ = const_cast<CObject*>(_object_);
		

		CEntityAlive*	EA = smart_cast<CEntityAlive*>(object_);
		if(!EA || !EA->g_Alive())						continue;
		

		FindVisObjByObject	f				(object_);
		VIS_OBJECTS_IT found;
		found = std::find_if				(m_active_objects.begin(),m_active_objects.end(),f);

		if( found != m_active_objects.end() ){
			(*found)->m_flags.set			(flVisObjNotValid,FALSE);
		}else{
			m_active_objects.push_back		(xr_new<SBinocVisibleObj>() );
			SBinocVisibleObj* new_vis_obj	= m_active_objects.back();
			new_vis_obj->m_flags.set			(flVisObjNotValid,FALSE);
			new_vis_obj->m_object			= object_;
			new_vis_obj->create_default		(m_frame_color.get());
			new_vis_obj->m_upd_speed			= m_rotating_speed;
			// through an eyepiece the target may be outside the glass: announce it only once its frame first shows
			// up inside (below). The binoculars keep announcing on sight.
			if (m_bEyepiece)
				new_vis_obj->m_flags.set	(flFoundSndPending, TRUE);
			else if(NULL==m_snd_found._feedback())
				m_snd_found.play_at_pos			(0,Fvector().set(0,0,0),sm_2D);
		}
	}
	std::sort								(m_active_objects.begin(), m_active_objects.end());

	while(m_active_objects.size() && m_active_objects.back()->m_flags.test(flVisObjNotValid)){
		xr_delete							(m_active_objects.back());
		m_active_objects.pop_back			();
	}

	it = m_active_objects.begin();
	for(;it!=m_active_objects.end();++it)
	{
		// GS/CoP: the frame crawls onto the target and then LOCKS -- that transition is what catch_snd
		// announces (and what turns the corners opaque + relation-coloured, see SBinocVisibleObj::Update).
		const bool was_locked = !!(*it)->m_flags.test(flTargetLocked);
		(*it)->Update						(m_bEyepiece);
		if ((*it)->m_flags.test(flFoundSndPending) && !(*it)->m_flags.test(flVisObjNotValid))
		{
			(*it)->m_flags.set				(flFoundSndPending, FALSE);
			if (NULL==m_snd_found._feedback())
				m_snd_found.play_at_pos		(0,Fvector().set(0,0,0),sm_2D);
		}
		if (!was_locked && (*it)->m_flags.test(flTargetLocked) && m_snd_catch._handle())
			m_snd_catch.play_at_pos			(0, Fvector().set(0,0,0), sm_2D);
	}
}

void CBinocularsVision::Draw()
{
	VIS_OBJECTS_IT	it = m_active_objects.begin();
	for(;it!=m_active_objects.end();++it)
		(*it)->Draw							();
}

void CBinocularsVision::Load(const shared_str& section)
{
	m_rotating_speed	= pSettings->r_float(section,"vis_frame_speed");
	m_frame_color		= pSettings->r_fcolor(section,"vis_frame_color");
	m_snd_found.create	(pSettings->r_string(section,"found_snd"),st_Effect,sg_SourceType);
	// GS/CoP parity: the lock sound. Optional, so a params section that only has found_snd still loads.
	if (pSettings->line_exist(section,"catch_snd"))
		m_snd_catch.create	(pSettings->r_string(section,"catch_snd"),st_Effect,sg_SourceType);
}

void CBinocularsVision::remove_links(CObject *object)
{
	VIS_OBJECTS::iterator	I = std::find_if(m_active_objects.begin(),m_active_objects.end(),FindVisObjByObject(object));
	if (I == m_active_objects.end())
		return;

	m_active_objects.erase	(I);
}
