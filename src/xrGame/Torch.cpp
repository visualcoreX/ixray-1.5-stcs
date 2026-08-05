#include "stdafx.h"
#include "torch.h"
#include "entity.h"
#include "actor.h"
#include "../xrEngine/LightAnimLibrary.h"
#include "PhysicsShell.h"
#include "xrserver_objects_alife_items.h"
#include "ai_sounds.h"

#include "HUDManager.h"
#include "level.h"
#include "../Include/xrRender/Kinematics.h"
#include "../xrEngine/camerabase.h"
#include "../xrengine/xr_collide_form.h"
#include "inventory.h"
#include "game_base_space.h"

#include "UIGameCustom.h"
#include "actorEffector.h"
#include "CustomOutfit.h"

static const float		TIME_2_HIDE					= 5.f;
static const float		TORCH_INERTION_CLAMP		= PI_DIV_6;
static const float		TORCH_INERTION_SPEED_MAX	= 7.5f;
static const float		TORCH_INERTION_SPEED_MIN	= 0.5f;
static const Fvector	TORCH_OFFSET				= {-0.2f,+0.1f,-0.3f};
static const Fvector	OMNI_OFFSET					= {-0.2f,+0.1f,-0.1f};
static const float		OPTIMIZATION_DISTANCE		= 100.f;

static bool stalker_use_dynamic_lights	= false;

CTorch::CTorch(void) 
{
	light_render				= ::Render->light_create();
	light_render->set_type		(IRender_Light::SPOT);
	light_render->set_shadow	(true);
	// the headlamp is ON the actor's head, so his own third-person body must stay out of its shadow
	// map -- otherwise the self-shadow feature throws a huge silhouette cast from inside him
	light_render->set_actor_shadow(false);
	// hud-mode twin so the headlamp also lights the first-person hands/weapon (a world spot never reaches HUD
	// geometry). No shadow -- avoids self-shadow artifacts on the hands and the extra smap cost.
	light_render_hud			= ::Render->light_create();
	light_render_hud->set_type	(IRender_Light::SPOT);
	light_render_hud->set_shadow(false);
	light_render_hud->set_hud_mode(true);
	// hud-mode point light = the GS "light sphere" giving the specular blik on the hands
	light_omni_hud				= ::Render->light_create();
	light_omni_hud->set_type	(IRender_Light::POINT);
	light_omni_hud->set_shadow	(false);
	light_omni_hud->set_hud_mode(true);
	m_fTorchHudOmniRange		= 1.0f;
	light_omni					= ::Render->light_create();
	light_omni->set_type		(IRender_Light::POINT);
	light_omni->set_shadow		(false);

	m_switched_on				= false;
	m_vTorchOffset				= TORCH_OFFSET;		// config may override in net_Spawn (torch_hands_offset)
	m_vTorchHudOffset.set		(0.0f, 0.12f, 0.0f);	// hud twin default: above the eye (headlamp) -> specular blik from above
	m_fTorchHudCone				= -1.0f;				// <=0: mirror the world spot's cone
	glow_render					= ::Render->glow_create();
	lanim						= 0;
	time2hide					= 0;
	fBrightness					= 1.f;

	/*m_NightVisionRechargeTime	= 6.f;
	m_NightVisionRechargeTimeMin= 2.f;
	m_NightVisionDischargeTime	= 10.f;
	m_NightVisionChargeTime		= 0.f;*/

	m_prev_hp.set				(0,0);
	m_delta_h					= 0;
}

CTorch::~CTorch(void) 
{
	light_render.destroy	();
	light_render_hud.destroy();
	light_omni_hud.destroy	();
	light_omni.destroy		();
	glow_render.destroy		();
}

inline bool CTorch::can_use_dynamic_lights	()
{
	if (!H_Parent())
		return				(true);

	CInventoryOwner			*owner = smart_cast<CInventoryOwner*>(H_Parent());
	if (!owner)
		return				(true);

	return					(owner->can_use_dynamic_lights());
}

void CTorch::Load(LPCSTR section) 
{
	inherited::Load			(section);
	light_trace_bone		= pSettings->r_string(section,"light_trace_bone");


	m_bNightVisionEnabled = !!pSettings->r_bool(section,"night_vision");
	if(m_bNightVisionEnabled)
	{
		m_sounds.LoadSound(section,"snd_night_vision_on", "NightVisionOnSnd", SOUND_TYPE_ITEM_USING);
		m_sounds.LoadSound(section,"snd_night_vision_off", "NightVisionOffSnd", SOUND_TYPE_ITEM_USING);
		m_sounds.LoadSound(section,"snd_night_vision_idle", "NightVisionIdleSnd", SOUND_TYPE_ITEM_USING);
		m_sounds.LoadSound(section,"snd_night_vision_broken", "NightVisionBrokenSnd", SOUND_TYPE_ITEM_USING);
	}
}

// Gunslinger's night-vision look isn't in the ppe or in a shader (pnv.h with its scanlines/vignette
// only ever reaches the 3D scope lens): the vignette and the horizontal lines are a full-screen UI
// overlay it adds while NV is on -- gunsl_nv_screen_mask.script doing AddCustomStatic. Same here,
// hung off the effector so the mask can't outlive the effect. CUI::Render only draws custom statics
// when GameIndicatorsShown(), so the mask hides itself behind menus and the raised PDA for free --
// which is what GS's own level.is_ui_shown() check is for.
static void gwr_nv_screen_mask(bool on)
{
	if (!HUD().GetUI() || !HUD().GetUI()->UIGame())	return;
	CUIGameCustom* g = HUD().GetUI()->UIGame();
	if (on)
	{
		if (!g->GetCustomStatic("gwr_nv_screen_mask"))
			g->AddCustomStatic("gwr_nv_screen_mask", true);
	}
	else
		g->RemoveCustomStatic("gwr_nv_screen_mask");
}

void CTorch::SwitchNightVision()
{
	if (OnClient()) return;
	SwitchNightVision(!m_bNightVisionOn);	
}

void CTorch::SwitchNightVision(bool vision_on)
{
	if(!m_bNightVisionEnabled) return;
	
	if(vision_on /*&& (m_NightVisionChargeTime > m_NightVisionRechargeTimeMin || OnClient())*/)
	{
		//m_NightVisionChargeTime = m_NightVisionDischargeTime*m_NightVisionChargeTime/m_NightVisionRechargeTime;
		m_bNightVisionOn = true;
	}
	else
	{
		m_bNightVisionOn = false;
	}

	CActor *pA = smart_cast<CActor *>(H_Parent());

	if(!pA)					return;
	bool bPlaySoundFirstPerson = (pA == Level().CurrentViewEntity());

	LPCSTR disabled_names	= pSettings->r_string(cNameSect(),"disabled_maps");
	LPCSTR curr_map			= *Level().name();
	u32 cnt					= _GetItemCount(disabled_names);
	bool b_allow			= true;
	string512				tmp;
	for(u32 i=0; i<cnt;++i){
		_GetItem(disabled_names, i, tmp);
		if(0==_stricmp(tmp, curr_map)){
			b_allow = false;
			break;
		}
	}

	CCustomOutfit* pCO=pA->GetOutfit();
	if(pCO&&pCO->m_NightVisionSect.size()&&!b_allow){
		m_sounds.PlaySound("NightVisionBrokenSnd", pA->Position(), pA, bPlaySoundFirstPerson);
		return;
	}

	if(m_bNightVisionOn){
		CEffectorPP* pp = pA->Cameras().GetPPEffector((EEffectorPPType)effNightvision);
		if(!pp){
			if (pCO&&pCO->m_NightVisionSect.size())
			{
				AddEffector(pA,effNightvision, pCO->m_NightVisionSect);
				gwr_nv_screen_mask(true);
				m_sounds.PlaySound("NightVisionOnSnd", pA->Position(), pA, bPlaySoundFirstPerson);
				m_sounds.PlaySound("NightVisionIdleSnd", pA->Position(), pA, bPlaySoundFirstPerson, true);
			}
		}
	}else{
 		CEffectorPP* pp = pA->Cameras().GetPPEffector((EEffectorPPType)effNightvision);
		if(pp)
			pp->Stop			(1.0f);
		// The mask (vignette+stripes) and off-sound must go even if pp is already null: during an emission
		// UpdateElectronicsProblems stops the green effector (leaving the mask up), so a manual NV-off then
		// finds no pp -- but the mask still has to come down. gwr_nv_screen_mask/RemoveCustomStatic is a no-op
		// if it isn't showing, so this is safe when NV was already fully off.
		gwr_nv_screen_mask	(false);
		m_sounds.PlaySound("NightVisionOffSnd", pA->Position(), pA, bPlaySoundFirstPerson);
		m_sounds.StopSound("NightVisionIdleSnd");
	}
}

// --- GS blowout NV-effector glitch -------------------------------------------------------------
// GS's CTorch__StopNvEffector kills only the NV postprocess (the green-screen effector) while the device
// stays switched on; the surge loop re-lights and re-kills it -> a flicker. These mirror the effector
// add/stop halves of SwitchNightVision() but leave m_bNightVisionOn untouched and play no on/off sound.
bool CTorch::IsNightVisionEffectorActive()
{
	CActor* pA = smart_cast<CActor*>(H_Parent());
	if(!pA)	return false;
	return pA->Cameras().GetPPEffector((EEffectorPPType)effNightvision) != nullptr;
}

void CTorch::StartNightVisionEffector()
{
	if(!m_bNightVisionEnabled || !m_bNightVisionOn)	return;
	CActor* pA = smart_cast<CActor*>(H_Parent());
	if(!pA)	return;
	if(pA->Cameras().GetPPEffector((EEffectorPPType)effNightvision))	return;	// already lit
	CCustomOutfit* pCO = pA->GetOutfit();
	if(pCO && pCO->m_NightVisionSect.size()){
		AddEffector			(pA, effNightvision, pCO->m_NightVisionSect);
		gwr_nv_screen_mask	(true);
	}
}

void CTorch::StopNightVisionEffector(float speed)
{
	CActor* pA = smart_cast<CActor*>(H_Parent());
	if(!pA)	return;
	CEffectorPP* pp = pA->Cameras().GetPPEffector((EEffectorPPType)effNightvision);
	if(pp){
		// GS CTorch__StopNvEffector kills ONLY the green colour postprocess. The screen mask (the vignette +
		// scanline-stripes overlay, gwr_nv_screen_mask) is left ON -- during an emission the green washes out
		// but the goggle frame/stripes stay on screen. So do NOT touch the mask here.
		pp->Stop			(speed);
	}
}


void CTorch::UpdateSwitchNightVision   ()
{
	if(!m_bNightVisionEnabled) return;
	if (OnClient()) return;


	/*if(m_bNightVisionOn)
	{
		m_NightVisionChargeTime			-= Device.fTimeDelta;

		if(m_NightVisionChargeTime<0.f)
			SwitchNightVision(false);
	}
	else
	{
		m_NightVisionChargeTime			+= Device.fTimeDelta;
		clamp(m_NightVisionChargeTime, 0.f, m_NightVisionRechargeTime);
	}*/
}


void CTorch::Switch()
{
	if (OnClient()) return;
	bool bActive			= !m_switched_on;
	Switch					(bActive);
}

void CTorch::Switch	(bool light_on)
{
	m_switched_on			= light_on;
	if (can_use_dynamic_lights())
	{
		light_render->set_active(light_on);

		CActor *pA = smart_cast<CActor *>(H_Parent());
		if(!pA)light_omni->set_active(light_on);
		// hud twin + hud sphere light the hands only in first person (actor-carried)
		light_render_hud->set_active(pA ? light_on : false);
		light_omni_hud->set_active(pA ? light_on : false);
	}
	glow_render->set_active					(light_on);

	if (*light_trace_bone) 
	{
		IKinematics* pVisual				= smart_cast<IKinematics*>(Visual()); VERIFY(pVisual);
		u16 bi								= pVisual->LL_BoneID(light_trace_bone);

		pVisual->LL_SetBoneVisible			(bi,	light_on,	TRUE);
		pVisual->CalculateBones				(TRUE);
//.		pVisual->LL_SetBoneVisible			(bi,	light_on,	TRUE); //hack
	}
}

BOOL CTorch::net_Spawn(CSE_Abstract* DC) 
{
	CSE_Abstract			*e	= (CSE_Abstract*)(DC);
	CSE_ALifeItemTorch		*torch	= smart_cast<CSE_ALifeItemTorch*>(e);
	R_ASSERT				(torch);
	cNameVisual_set			(torch->get_visual());

	R_ASSERT				(!CFORM());
	R_ASSERT				(smart_cast<IKinematics*>(Visual()));
	collidable.model		= xr_new<CCF_Skeleton>	(this);

	if (!inherited::net_Spawn(DC))
		return				(FALSE);
	
	bool b_r2				= !!psDeviceFlags.test(rsR2);
	b_r2					|= !!psDeviceFlags.test(rsR3);

	IKinematics* K			= smart_cast<IKinematics*>(Visual());
	CInifile* pUserData		= K->LL_UserData(); 
	R_ASSERT3				(pUserData,"Empty Torch user data!",torch->get_visual());
	lanim					= LALib.FindItem(pUserData->r_string("torch_definition","color_animator"));
	guid_bone				= K->LL_BoneID	(pUserData->r_string("torch_definition","guide_bone"));	VERIFY(guid_bone!=BI_NONE);

	// task: allow the headlamp's light-position offset to be tuned from the item config so it can be pushed
	// forward to light the hands (GS-style) without a rebuild. Falls back to the hardcoded default.
	m_vTorchOffset			= READ_IF_EXISTS(pSettings, r_fvector3, cNameSect(), "torch_hands_offset", TORCH_OFFSET);
	m_vTorchHudOffset		= READ_IF_EXISTS(pSettings, r_fvector3, cNameSect(), "torch_hands_hud_offset", m_vTorchHudOffset);
	m_fTorchHudCone			= READ_IF_EXISTS(pSettings, r_float,    cNameSect(), "torch_hands_hud_cone",   m_fTorchHudCone);
	m_fTorchHudOmniRange	= READ_IF_EXISTS(pSettings, r_float,    cNameSect(), "torch_hands_hud_omni_range", m_fTorchHudOmniRange);

	Fcolor clr				= pUserData->r_fcolor				("torch_definition",(b_r2)?"color_r2":"color");
	fBrightness				= clr.intensity();
	float range				= pUserData->r_float				("torch_definition",(b_r2)?"range_r2":"range");
	light_render->set_color	(clr);
	light_render->set_range	(range);

	Fcolor clr_o			= pUserData->r_fcolor				("torch_definition",(b_r2)?"omni_color_r2":"omni_color");
	float range_o			= pUserData->r_float				("torch_definition",(b_r2)?"omni_range_r2":"omni_range");
	light_omni->set_color	(clr_o);
	light_omni->set_range	(range_o);

	// spot cone texture: the stock one (baked in the .db OGF userdata) has a HARD edge. Allow a config override
	// so we can point at GS's soft-edged tactical texture without editing the OGF. Falls back to the userdata.
	LPCSTR spot_tex = READ_IF_EXISTS(pSettings, r_string, cNameSect(), "torch_spot_texture",
									 pUserData->r_string("torch_definition","spot_texture"));

	light_render->set_cone	(deg2rad(pUserData->r_float			("torch_definition","spot_angle")));
	light_render->set_texture(spot_tex);

	// mirror the spot's look onto the hud twin (same colour/range/texture; cone overridable so the hands can
	// drop out of the beam when you pitch up, GS-style)
	light_render_hud->set_color		(clr);
	light_render_hud->set_range		(range);
	light_render_hud->set_cone		(deg2rad(m_fTorchHudCone > 0.f ? m_fTorchHudCone : pUserData->r_float("torch_definition","spot_angle")));
	light_render_hud->set_texture	(spot_tex);

	// hud "light sphere" (point): local range for a tight specular blik on the hands
	light_omni_hud->set_color		(clr);
	light_omni_hud->set_range		(m_fTorchHudOmniRange);

	glow_render->set_texture(pUserData->r_string				("torch_definition","glow_texture"));
	glow_render->set_color	(clr);
	glow_render->set_radius	(pUserData->r_float					("torch_definition","glow_radius"));

	//��������/��������� �������
	Switch					(torch->m_active);
	VERIFY					(!torch->m_active || (torch->ID_Parent != 0xffff));
	
	SwitchNightVision		(false);

	m_delta_h				= PI_DIV_2-atan((range*0.5f)/_abs(TORCH_OFFSET.x));

	return					(TRUE);
}

void CTorch::net_Destroy() 
{
	Switch					(false);
	SwitchNightVision		(false);

	inherited::net_Destroy	();
}

void CTorch::OnH_A_Chield() 
{
	inherited::OnH_A_Chield			();
	m_focus.set						(Position());
}

void CTorch::OnH_B_Independent	(bool just_before_destroy) 
{
	inherited::OnH_B_Independent	(just_before_destroy);
	time2hide						= TIME_2_HIDE;

	Switch						(false);
	SwitchNightVision			(false);

	m_sounds.StopAllSounds		();
}

void CTorch::UpdateCL() 
{
	inherited::UpdateCL			();
	
	UpdateSwitchNightVision		();

	if (!m_switched_on)			return;

	CBoneInstance			&BI = smart_cast<IKinematics*>(Visual())->LL_GetBoneInstance(guid_bone);
	Fmatrix					M;

	if (H_Parent()) 
	{
		CActor*			actor = smart_cast<CActor*>(H_Parent());
		if (actor)		smart_cast<IKinematics*>(H_Parent()->Visual())->CalculateBones_Invalidate	();

		if (H_Parent()->XFORM().c.distance_to_sqr(Device.vCameraPosition)<_sqr(OPTIMIZATION_DISTANCE) || GameID() != eGameIDSingle) {
			// near camera
			smart_cast<IKinematics*>(H_Parent()->Visual())->CalculateBones	();
			M.mul_43				(XFORM(),BI.mTransform);
		} else {
			// approximately the same
			M		= H_Parent()->XFORM		();
			H_Parent()->Center				(M.c);
			M.c.y	+= H_Parent()->Radius	()*2.f/3.f;
		}

		if (actor) 
		{
			m_prev_hp.x		= angle_inertion_var(m_prev_hp.x,-actor->cam_FirstEye()->yaw,TORCH_INERTION_SPEED_MIN,TORCH_INERTION_SPEED_MAX,TORCH_INERTION_CLAMP,Device.fTimeDelta);
			m_prev_hp.y		= angle_inertion_var(m_prev_hp.y,-actor->cam_FirstEye()->pitch,TORCH_INERTION_SPEED_MIN,TORCH_INERTION_SPEED_MAX,TORCH_INERTION_CLAMP,Device.fTimeDelta);

			Fvector			dir,right,up;	
			dir.setHP		(m_prev_hp.x+m_delta_h,m_prev_hp.y);
			Fvector::generate_orthonormal_basis_normalized(dir,up,right);


			if (true)
			{
				Fvector offset				= M.c;
				offset.mad					(M.i,m_vTorchOffset.x);
				offset.mad					(M.j,m_vTorchOffset.y);
				offset.mad					(M.k,m_vTorchOffset.z);
				light_render->set_position	(offset);
				// hud twin lives in CAMERA/HUD space (that's where the first-person hands are drawn), NOT at the
				// world head bone -- otherwise it never lines up with the hands. Camera pos + camera-basis offset.
				Fvector hud_pos = Device.vCameraPosition;
				hud_pos.mad(Device.vCameraRight,		m_vTorchHudOffset.x);
				hud_pos.mad(Device.vCameraTop,			m_vTorchHudOffset.y);
				hud_pos.mad(Device.vCameraDirection,	m_vTorchHudOffset.z);
				light_render_hud->set_position(hud_pos);
				light_omni_hud->set_position(hud_pos);		// hud sphere co-located with the hud spot
				// (re)activate the hud lights here, not only in Switch: on save/load net_Spawn calls Switch
				// BEFORE the actor parent is attached (pA==null), so the hud spot+omni were left OFF after a
				// load ("omni sphere disappears"). We're in the actor branch with m_switched_on -> force them on.
				light_render_hud->set_active(true);
				light_omni_hud->set_active(true);

				if(false)
				{
					offset						= M.c; 
					offset.mad					(M.i,OMNI_OFFSET.x);
					offset.mad					(M.j,OMNI_OFFSET.y);
					offset.mad					(M.k,OMNI_OFFSET.z);
					light_omni->set_position	(offset);
				}
			}//if (true)
			glow_render->set_position	(M.c);

			if (true)
			{
				light_render->set_rotation	(dir, right);
				// hud twin aims straight along the view (hands are always front-and-centre), from the camera
				light_render_hud->set_rotation(Device.vCameraDirection, Device.vCameraRight);

				if(false)
				{
					light_omni->set_rotation	(dir, right);
				}
			}//if (true)
			glow_render->set_direction	(dir);

		}// if(actor)
		else 
		{
			if (can_use_dynamic_lights()) 
			{
				light_render->set_position	(M.c);
				light_render->set_rotation	(M.k,M.i);

				Fvector offset				= M.c; 
				offset.mad					(M.i,OMNI_OFFSET.x);
				offset.mad					(M.j,OMNI_OFFSET.y);
				offset.mad					(M.k,OMNI_OFFSET.z);
				light_omni->set_position	(M.c);
				light_omni->set_rotation	(M.k,M.i);
			}//if (can_use_dynamic_lights()) 

			glow_render->set_position	(M.c);
			glow_render->set_direction	(M.k);
		}
	}//if(HParent())
	else 
	{
		if (getVisible() && m_pPhysicsShell) 
		{
			M.mul						(XFORM(),BI.mTransform);

			//. what should we do in case when 
			// light_render is not active at this moment,
			// but m_switched_on is true?
//			light_render->set_rotation	(M.k,M.i);
//			light_render->set_position	(M.c);
//			glow_render->set_position	(M.c);
//			glow_render->set_direction	(M.k);
//
//			time2hide					-= Device.fTimeDelta;
//			if (time2hide<0)
			{
				m_switched_on			= false;
				light_render->set_active(false);
				light_omni->set_active(false);
				glow_render->set_active	(false);
			}
		}//if (getVisible() && m_pPhysicsShell)  
	}

	if (!m_switched_on)					return;

	// calc color animator
	if (!lanim)							return;

	int						frame;
	// ���������� � ������� BGR
	u32 clr					= lanim->CalculateBGR(Device.fTimeGlobal,frame); 

	Fcolor					fclr;
	fclr.set				((float)color_get_B(clr),(float)color_get_G(clr),(float)color_get_R(clr),1.f);
	fclr.mul_rgb			(fBrightness/255.f);
	if (can_use_dynamic_lights())
	{
		light_render->set_color	(fclr);
		light_omni->set_color	(fclr);
	}
	glow_render->set_color		(fclr);
}


void CTorch::create_physic_shell()
{
	CPhysicsShellHolder::create_physic_shell();
}

void CTorch::activate_physic_shell()
{
	CPhysicsShellHolder::activate_physic_shell();
}

void CTorch::setup_physic_shell	()
{
	CPhysicsShellHolder::setup_physic_shell();
}

void CTorch::net_Export			(NET_Packet& P)
{
	inherited::net_Export		(P);
//	P.w_u8						(m_switched_on ? 1 : 0);


	BYTE F = 0;
	F |= (m_switched_on ? eTorchActive : 0);
	F |= (m_bNightVisionOn ? eNightVisionActive : 0);
	const CActor *pA = smart_cast<const CActor *>(H_Parent());
	if (pA)
	{
		if (pA->attached(this))
			F |= eAttached;
	}
	P.w_u8(F);
//	Msg("CTorch::net_export - NV[%d]", m_bNightVisionOn);
}

void CTorch::net_Import			(NET_Packet& P)
{
	inherited::net_Import		(P);
	
	BYTE F = P.r_u8();
	bool new_m_switched_on				= !!(F & eTorchActive);
	bool new_m_bNightVisionOn			= !!(F & eNightVisionActive);

	if (new_m_switched_on != m_switched_on)			Switch						(new_m_switched_on);
	if (new_m_bNightVisionOn != m_bNightVisionOn)	
	{
//		Msg("CTorch::net_Import - NV[%d]", new_m_bNightVisionOn);

		SwitchNightVision			(new_m_bNightVisionOn);
	}
}

bool  CTorch::can_be_attached		() const
{
//	if( !inherited::can_be_attached() ) return false;

	const CActor *pA = smart_cast<const CActor *>(H_Parent());
	if (pA) 
	{
//		if(pA->inventory().Get(ID(), false))
		if((const CTorch*)smart_cast<CTorch*>(pA->inventory().m_slots[GetSlot()].m_pIItem) == this )
			return true;
		else
			return false;
	}
	return true;
}
void CTorch::afterDetach			()
{
	inherited::afterDetach	();
	Switch					(false);
}
void CTorch::renderable_Render()
{
	inherited::renderable_Render();
}

void CTorch::enable(bool value)
{
	inherited::enable(value);

	if(!enabled() && m_switched_on)
		Switch				(false);

}
