#pragma once
#include "hud_item_object.h"
#include "HudSound.h"

struct dContact;
struct SGameMtl;
class CMissile : public CHudItemObject
{
	typedef CHudItemObject inherited;
public:
	enum EMissileStates{
		eThrowStart = eLastBaseState+1,
		eReady,
		eThrow,
		eThrowEnd,
		eMissileAction,		// one-shot gesture (headlamp/NV toggle) played on the item in hand, then back to idle
	};

	// play a one-shot HUD gesture (anm_headlamp_on/off, anm_nv_on/off) on this item if it has the alias.
	// Lets the bolt/grenade in the RIGHT hand animate on a torch/NV toggle, like Gunslinger (the left-hand
	// headflash phantom is separate). Returns false if not idle or the item has no such gesture.
	bool					PlayHudActionAnim			(LPCSTR base);
							CMissile					();
	virtual					~CMissile					();

	virtual BOOL			AlwaysTheCrow				()				{ return TRUE; }
	virtual void			render_item_ui					();
	virtual bool			render_item_ui_query					();

	virtual void			reinit						();
	virtual CMissile*		cast_missile				()				{return this;}

	virtual void 			Load						(LPCSTR section);
	virtual BOOL 			net_Spawn					(CSE_Abstract* DC);
	virtual void 			net_Destroy					();

	virtual void 			UpdateCL					();
	virtual void 			shedule_Update				(u32 dt);

	virtual void 			OnH_A_Chield				();
	virtual void 			OnH_B_Independent			(bool just_before_destroy);

	virtual void 			OnEvent						(NET_Packet& P, u16 type);

	virtual void 			OnAnimationEnd				(u32 state);
	virtual void			OnMotionMark				(u32 state, const motion_marks&);


	virtual void 			Throw();
	virtual void 			Destroy();

	virtual bool 			Action						(s32 cmd, u32 flags);
			bool			CompanionDetectorBusy		() const;	// detector shares the draw and is still coming up

	virtual void 			State						(u32 state);
	virtual void 			OnStateSwitch				(u32 S);
	virtual void			GetBriefInfo				(xr_string& str_name, xr_string& icon_sect_name, xr_string& str_count, string16& fire_mode);

protected:
	virtual void			UpdateFireDependencies_internal	();
	virtual void			UpdateXForm						();
	void					UpdatePosition					(const Fmatrix& trans);
	void					spawn_fake_missile				();

	virtual void			OnActiveItem		();
	virtual void			OnHiddenItem		();

	//��� ����
	virtual void			net_Relcase			(CObject* O );
protected:

	shared_str				m_missile_action_anim;	// the gesture alias currently playing in eMissileAction

	//����� ���������� � ������� ���������
	u32						m_dwStateTime;
	bool					m_throw;
	
	//����� �����������
	u32						m_dwDestroyTime;
	u32						m_dwDestroyTimeMax;

	Fvector					m_throw_direction;
	Fmatrix					m_throw_matrix;

	CMissile				*m_fake_missile;

	//��������� ������
	
	float m_fMinForce, m_fConstForce, m_fMaxForce, m_fForceGrowSpeed;
//private:
	bool					m_constpower;
	bool					m_bSuicideThrow;	// GS: this throw is the controller-suicide one
public:
	bool			SuicideAllowed		();		// hud `allow_suicide` + the animation exists + grabbed
	bool			SuicideStillGrabbed	();
	void			SuicidePrepareForce	(LPCSTR key, float def);
protected:
	float					m_fThrowForce;
protected:
	//������������� ����� � ����������� ������ �������
	Fvector					m_vThrowPoint;
	Fvector					m_vThrowDir;
	//��� HUD
	Fvector					m_vHudThrowPoint;
	Fvector					m_vHudThrowDir;

protected:
			void			setup_throw_params		();
public:
	virtual void			activate_physic_shell	();
	virtual void			setup_physic_shell		();
	virtual void			create_physic_shell		();
	IC		void			set_destroy_time		(u32 delta_destroy_time) {m_dwDestroyTime = delta_destroy_time + Device.dwTimeGlobal;}
	virtual void			PH_A_CrPr				();

protected:
	u32						m_ef_weapon_type;

public:
	virtual u32				ef_weapon_type			() const;
	IC		u32				destroy_time			() const { return m_dwDestroyTime; }
	IC		int				time_from_begin_throw	() const { return (Device.dwTimeGlobal + m_dwDestroyTimeMax - m_dwDestroyTime); }
	static	void			ExitContactCallback		(bool& do_colide,bool bo1,dContact& c,SGameMtl * /*material_1*/,SGameMtl * /*material_2*/);
};