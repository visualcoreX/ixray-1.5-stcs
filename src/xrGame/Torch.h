#pragma once

#include "inventory_item_object.h"
//#include "night_vision_effector.h"
#include "hudsound.h"
#include "script_export_space.h"

class CLAItem;
class CMonsterEffector;

class CTorch : public CInventoryItemObject {
private:
    typedef	CInventoryItemObject	inherited;

protected:
	float			fBrightness;
	CLAItem*		lanim;
	float			time2hide;

	u16				guid_bone;
	shared_str		light_trace_bone;

	float			m_delta_h;
	// spot-light position offset in the guide-bone frame (task: move the headlamp forward so its near
	// field spills onto the hands, GS-style). Defaults to the old hardcoded TORCH_OFFSET; overridable per
	// config via [device_torch] torch_hands_offset so it can be tuned without a rebuild.
	Fvector			m_vTorchOffset;
	// position of the hud twin light in CAMERA space (x=right, y=up, z=forward). GS's headlamp is a HUD-item
	// torch whose light sits on the HUD bone; ours is a world head device, so the hud twin has to be placed in
	// camera/HUD space to line up with where the hands are drawn. Config: [device_torch] torch_hands_hud_offset.
	Fvector			m_vTorchHudOffset;
	// optional cone override (deg) for the hud twin; <=0 = use the world spot's cone. A narrower cone makes
	// the hands drop out of the beam sooner when you pitch up (GS-like), so it's separately tunable.
	float			m_fTorchHudCone;
	Fvector2		m_prev_hp;
	bool			m_switched_on;
	ref_light		light_render;
	// hud-mode twin of light_render: lights the first-person HUD hands/weapon (the world spot can't reach HUD
	// geometry). Active only while the actor carries the headlamp. Co-located with light_render each frame.
	ref_light		light_render_hud;
	// hud-mode POINT light -- the GS "light sphere" on the hands: a point source gives the crisp, view-dependent
	// specular blik the spot alone doesn't. Co-located with the hud spot; range tuned small so it's local.
	ref_light		light_omni_hud;
	float			m_fTorchHudOmniRange;
	ref_light		light_omni;
	ref_glow		glow_render;
	Fvector			m_focus;
private:
	inline	bool	can_use_dynamic_lights	();

public:
					CTorch				(void);
	virtual			~CTorch				(void);

	virtual void	Load				(LPCSTR section);
	virtual BOOL	net_Spawn			(CSE_Abstract* DC);
	virtual void	net_Destroy			();
	virtual void	net_Export			(NET_Packet& P);				// export to server
	virtual void	net_Import			(NET_Packet& P);				// import from server

	virtual void	OnH_A_Chield		();
	virtual void	OnH_B_Independent	(bool just_before_destroy);

	virtual void	UpdateCL			();

			void	Switch				();
			void	Switch				(bool light_on);

	virtual bool	can_be_attached		() const;

	//CAttachableItem
	virtual	void				enable					(bool value);
 
public:
			void	SwitchNightVision		  ();
			void	SwitchNightVision		  (bool light_on);
			void	UpdateSwitchNightVision   ();
			float	NightVisionBattery		  ();

			// GS blowout: glitch the NV *effector* (postprocess) without changing device on/off state or
			// playing the on/off sounds -- the device stays logically ON, the visual flickers.
			void	StartNightVisionEffector  ();
			void	StopNightVisionEffector	  (float speed);
			bool	IsNightVisionEffectorActive();

			bool	GetNightVisionStatus	() { return m_bNightVisionOn; }
			bool	torch_active			() const { return m_switched_on; }
protected:
	bool					m_bNightVisionEnabled;
	bool					m_bNightVisionOn;

	HUD_SOUND_COLLECTION	m_sounds;

	enum EStats{
		eTorchActive				= (1<<0),
		eNightVisionActive			= (1<<1),
		eAttached					= (1<<2)
	};

public:

	virtual bool			use_parent_ai_locations	() const
	{
		return				(!H_Parent());
	}
	virtual void	create_physic_shell		();
	virtual void	activate_physic_shell	();
	virtual void	setup_physic_shell		();

	virtual void	afterDetach				();
	virtual void	renderable_Render		();

	DECLARE_SCRIPT_REGISTER_FUNCTION
};
add_to_type_list(CTorch)
#undef script_type_list
#define script_type_list save_type_list(CTorch)
