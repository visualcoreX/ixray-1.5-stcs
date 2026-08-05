#pragma once
#include "inventory_item_object.h"
#include "../xrEngine/feel_touch.h"
#include "hudsound.h"
#include "customzone.h"
#include "artefact.h"
#include "ai_sounds.h"
#include "ui/ArtefactDetectorUI.h"
#include "../xrEngine/Render.h"		// ref_light / ref_glow for the GS handheld torch

class CCustomZone;
class CInventoryOwner;

struct ITEM_TYPE
{
	Fvector2			freq; //min,max
	HUD_SOUND_ITEM		detect_snds;

	shared_str			zone_map_location;
	shared_str			nightvision_particle;
};

//�������� ����, ������������ ����������
struct ITEM_INFO
{
	ITEM_TYPE*						curr_ref;
	float							snd_time;
	//������� ������� ������ �������
	float							cur_period;
	//particle for night-vision mode
	CParticlesObject*				pParticle;

									ITEM_INFO		();
									~ITEM_INFO		();
};

template <typename K> 
class CDetectList : public Feel::Touch
{
protected:
	typedef xr_map<CLASS_ID, ITEM_TYPE>	TypesMap;
	typedef typename TypesMap::iterator	TypesMapIt;
	TypesMap							m_TypesMap;
public:
	typedef xr_map<K*,ITEM_INFO>		ItemsMap;
	typedef typename ItemsMap::iterator	ItemsMapIt;
	ItemsMap							m_ItemInfos;

protected:
	virtual void 	feel_touch_new		(CObject* O)
	{
		K* pK							= smart_cast<K*>(O);
		R_ASSERT						(pK);
		TypesMapIt it					= m_TypesMap.find(O->CLS_ID);
		R_ASSERT						(it!=m_TypesMap.end());
		m_ItemInfos[pK].snd_time		= 0.0f;
		m_ItemInfos[pK].curr_ref		= &(it->second);
	}
	virtual void 	feel_touch_delete	(CObject* O)
	{
		K* pK							= smart_cast<K*>(O);
		R_ASSERT						(pK);
		m_ItemInfos.erase				(pK);
	}
public:
	void			destroy				()
	{
		TypesMapIt it = m_TypesMap.begin();
		for(; it!=m_TypesMap.end(); ++it)
			HUD_SOUND_ITEM::DestroySound(it->second.detect_snds);
	}
	void			clear				()
	{
		m_ItemInfos.clear				();
		Feel::Touch::feel_touch.clear	();
	}
	virtual void	load				(LPCSTR sect, LPCSTR prefix)
	{
		u32 i					= 1;
		string256				temp;
		do{
			xr_sprintf			(temp, "%s_class_%d", prefix, i);
			if(pSettings->line_exist(sect,temp))
			{
				LPCSTR z_Class			= pSettings->r_string(sect,temp);
				CLASS_ID item_cls		= TEXT2CLSID(pSettings->r_string(z_Class,"class"));

				m_TypesMap.insert		(std::make_pair(item_cls,ITEM_TYPE()));
				ITEM_TYPE& item_type	= m_TypesMap[item_cls];

				xr_sprintf				(temp, "%s_freq_%d", prefix, i);
				item_type.freq			= pSettings->r_fvector2(sect,temp);

				xr_sprintf				(temp, "%s_sound_%d_", prefix, i);
				HUD_SOUND_ITEM::LoadSound	(sect, temp	,item_type.detect_snds		, SOUND_TYPE_ITEM);

				++i;
			}else 
				break;

		} while(true);
	}
};
/*
class CZoneList  :public CDetectList<CCustomZone>
{
protected:
	virtual BOOL 	feel_touch_contact	(CObject* O);
};
*/
class CAfList  :public CDetectList<CArtefact>
{
protected:
	virtual BOOL 	feel_touch_contact	(CObject* O);
public:
					CAfList		():m_af_rank(0){}
	int				m_af_rank;
};

class CCustomDetector :		public CHudItemObject
{
	typedef	CHudItemObject	inherited;
protected:
	CUIArtefactDetectorBase*			m_ui;
	bool			m_bFastAnimMode;
	bool			m_bEmergencyShow;	// next show plays anm_show_emergency (drawn together with a weapon)
	bool			m_bNeedActivation;
	bool			m_bNeedActivationManual;	// that deferred draw came from a KEYPRESS, so when it finally
										// fires it must take the manual path (the weapon plays its
										// anm_prepare_detector / anm_draw_detector hand gesture).
										// ShowDetector() is the engine path and deliberately plays none.
	bool			m_bAutoToggle;		// this show/hide is an auto hide/re-show (reload/aim), NOT a manual toggle
										// -> don't play the weapon's draw/prepare-detector gesture
	bool			m_bRestoreWithWeapon;	// this deferred draw must go up TOGETHER with the weapon that is
										// being drawn, not after it (GS actShowDetectorNow force-unhide).
										// Only meaningful while m_bNeedActivation is set.
	bool			m_bCompanionOneShot;	// the companion now playing is a one-shot (CMotionDef::StopAtEnd):
											// it freezes on its last frame when done, a loop (aim idle) doesn't
	shared_str		m_companion_done;		// a one-shot companion that just ended; the idle mirror must not
											// re-pick it while the weapon still plays its own one-shot

public:
					CCustomDetector		();
	virtual			~CCustomDetector	();

	virtual BOOL 	net_Spawn			(CSE_Abstract* DC);
	virtual void 	Load				(LPCSTR section);

	virtual void 	OnH_A_Chield		();
	virtual void 	OnH_B_Independent	(bool just_before_destroy);

	virtual void 	shedule_Update		(u32 dt);
	virtual void 	UpdateCL			();


			bool 	IsWorking			();

	virtual void 	OnMoveToSlot		();
	virtual void 	OnMoveToRuck		(EItemPlace prev);

	virtual void	OnActiveItem		();
	virtual void	OnHiddenItem		();
	virtual void	OnStateSwitch		(u32 S);
	virtual void	OnAnimationEnd		(u32 state);
	virtual void	PlayAnimIdle		();	// mirror an out weapon's aim (companion anim) instead of the own idle
	virtual bool	PlayCompanionAction	(LPCSTR action, bool bRestart = false);	// play anm_wpn_<action> synced to the weapon's action; true if played
	void			WeaponDetectorGesture(bool draw);	// tell an in-hand weapon to play its draw/prepare detector gesture
	void			ShowAfterPrepare	();	// weapon's anm_prepare_detector finished -> actually show the detector now
	virtual	void	UpdateXForm			();
	// re-select the detector's (companion) idle NOW — called by the weapon when its aim state changes
	void			RefreshCompanionIdle();

	// ---- GS handheld torch (configs/weapons/detectors/torch) --------------------------------------
	// GS builds its flashlight as a DETECTOR: same slot, same companion machinery, plus a light off
	// one of its bones. There is no toggle key -- the light comes on partway through the DRAW
	// animation and goes off partway through the HIDE, per `torch_enable_time_<alias>` /
	// `torch_disable_time_<alias>` in the hud section. Opt-in: `torch_installed` in the item section.
public:
			bool	HasTorch			() const	{ return m_bTorchInstalled; }
protected:
			void	LoadTorchParams		(LPCSTR section);
			void	UpdateTorch			();	// per frame from UpdateCL
			void	StopTorch			();
			void	ScheduleTorch		(LPCSTR anim_alias);	// arm the on/off moment for this motion
			// the emitter/cone geometry on the model is shown and hidden WITH the light, so a drawn
			// but not yet lit torch has a dark lens
			void	UpdateTorchBones	(bool on);

			bool			m_bTorchInstalled;
			bool			m_bTorchOn;
			u32				m_dwTorchSwitchAt;		// Device time to apply m_bTorchPending (0 = nothing armed)
			bool			m_bTorchPending;
			shared_str		m_sTorchBone;			// torch_light_bone on the HUD model
			shared_str		m_sTorchConeBones;		// torch_cone_bones: comma-separated beam/cone geometry
			int				m_iTorchBonesShown;		// last applied visibility: 1 shown, 0 hidden, -1 unknown
			Fvector			m_vTorchOffset;			// torch_attach_offset_*
			Fvector			m_vTorchOmniOffset;		// torch_omni_attach_offset_* (defaults to the above)
			Fvector			m_vTorchAimOffset;		// torch_aim_attach_offset_*: added while the weapon is aimed
			Fcolor			m_TorchColor;			// torch_r2_color_*
			float			m_fTorchRange;			// torch_r2_range
			float			m_fTorchCone;			// torch_spot_angle (radians)
			shared_str		m_sTorchSpotTex;		// torch_spot_texture
			Fcolor			m_TorchOmniColor;		// torch_r2_omni_color_*
			float			m_fTorchOmniRange;		// torch_r2_omni_range
			bool			m_bTorchGlow;			// create_glow
			shared_str		m_sTorchGlowTex;		// torch_glow_texture
			float			m_fTorchGlowRadius;		// torch_glow_radius
			ref_light		m_pTorchSpot;
			ref_light		m_pTorchOmni;
			ref_glow		m_pTorchGlow;
public:

	void			ToggleDetector		(bool bFastMode);
	void			HideDetector		(bool bFastMode);
	void			ShowDetector		(bool bFastMode);
	void			ShowDetectorEmergency();	// show with the anm_show_emergency (weapon-in-hand) draw
	// GS RestoreLastActorDetector (ActorUtils.pas:1700): ask for the detector back once the hands are
	// free again. Deferred on purpose -- ShowDetector() here would draw it while the item that
	// replaced the grenade is still coming up; UpdateVisibility's m_bNeedActivation path waits for
	// that item to be out and compatible, then plays the engine-driven (no weapon gesture) re-show.
	void			RequestRestore		();
	// Drop a pending deferred draw. Something else is taking over the hands and will decide for
	// itself whether the detector comes back -- letting the old request stand would have it grab a
	// slot out from under that.
	void			CancelRestore		()	{ m_bNeedActivation = false; m_bNeedActivationManual = false;
											  m_bRestoreWithWeapon = false; }
	float			m_fAfDetectRadius;
	virtual bool	CheckCompatibility	(CHudItem*);

	virtual u32		ef_detector_type	() const	{return 1;};

	virtual bool	NeedActivation		() const	{return m_bNeedActivation;};

	// Play a one-shot torch/NV toggle gesture on this detector's HUD (left hand). Returns false
	// (so the caller falls back) if the detector isn't idle/working or has no matching anm_* alias.
	bool			PlayHudActionAnim	(LPCSTR base);

protected:
	enum { eDetActionAnim = eBore + 1 };	// one-shot torch/NV gesture state
	shared_str		m_action_anim;			// the chosen gesture motion alias

	// for_draw: the caller is asking "may the detector come OUT right now" (a keypress, a deferred
	// draw). Only then does an item still rising block it -- an item coming up is never a reason to
	// put an ALREADY drawn detector away, which is what the hide path (CheckCompatibility) asks.
			bool	CheckCompatibilityInt		(CHudItem*, u32* slot_to_activate = NULL, bool for_draw = false);
			bool	AnimForbidsDetector			(CHudItem*);	// GS disable_detector_<alias>
			bool	HasDetectorDrawGesture		(CHudItem*);	// plays anm_prepare_detector on the draw?
			// ...and is that gesture something this draw actually has to WAIT for? A restore that comes
			// up with the weapon does not: the hand-over only makes sense when the weapon is already out.
			bool	WaitForDrawGesture			(CHudItem* itm)	{ return HasDetectorDrawGesture(itm) && !m_bRestoreWithWeapon; }
			void 	TurnDetectorInternal		(bool b);
	void 			UpdateNightVisionMode		(bool b_off);
	void			UpdateVisibility			();
	virtual void	UpfateWork					();
	virtual void 	UpdateAf					()				{};
	virtual void 	CreateUI					()				{};

	bool			m_bWorking;
	float			m_fAfVisRadius;

	CAfList			m_artefacts;
};
