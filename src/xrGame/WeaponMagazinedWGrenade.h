#pragma once
#include "weaponmagazined.h"
#include "rocketlauncher.h"


class CWeaponFakeGrenade;


class CWeaponMagazinedWGrenade : public CWeaponMagazined,
								 public CRocketLauncher
{
	typedef CWeaponMagazined inherited;
public:
					CWeaponMagazinedWGrenade	(ESoundTypes eSoundType=SOUND_TYPE_WEAPON_SUBMACHINEGUN);
	virtual			~CWeaponMagazinedWGrenade	();

	virtual void	Load				(LPCSTR section);

	// gwr: the loaded grenade's HUD bone (gl_ammo_params_section_<type> -> configuration_<count>)
	virtual int		gwr_GLBonesState	();
	virtual void	gwr_UpdateBonesGL	();

	virtual BOOL	net_Spawn			(CSE_Abstract* DC);
	virtual void	net_Destroy			();
	virtual void	net_Export			(NET_Packet& P);
	virtual void	net_Import			(NET_Packet& P);
	
	virtual void	OnH_B_Independent	(bool just_before_destroy);

	virtual void	save				(NET_Packet &output_packet);
	virtual void	load				(IReader &input_packet);


	virtual bool	Attach					(PIItem pIItem, bool b_send_event);
	virtual bool	Detach					(const char* item_section_name, bool b_spawn_item);
	virtual bool	CanAttach				(PIItem pIItem);
	virtual bool	CanDetach				(const char* item_section_name);
	virtual void	InitAddons				();
	virtual bool	UseScopeTexture			();
	virtual	float	CurrentZoomFactor		();
	virtual	u8		GetCurrentHudOffsetIdx	();
	virtual void	FireEnd					();
			void	LaunchGrenade			();
			void	LaunchGrenade_Correct	(Fvector3* v);
	
	virtual void	OnStateSwitch	(u32 S);
	
	virtual void	switch2_Reload	();
	virtual void	switch2_SwitchMode();
	virtual void	state_Fire		(float dt);
	virtual void	OnShot			();
	virtual void	OnEvent			(NET_Packet& P, u16 type);
	virtual void	ReloadMagazine	();

	virtual bool	Action			(s32 cmd, u32 flags);

	virtual void	UpdateSounds	();

	//������������ � ����� �������������
	virtual bool	SwitchMode		();
	// slot 2 of the sprint-exit wait is the launcher flip; anything else is the base class's
	virtual void	ResumeSprintDeferred(u8 action);
	void			PerformSwitchGL	();
	void			OnAnimationEnd	(u32 state);

	virtual bool	IsNecessaryItem	    (const shared_str& item_sect);

	//����������� ������� ��� ������������ �������� HUD
	virtual void	PlayAnimShow		();
	virtual void	PlayAnimHide		();
	virtual void	PlayAnimReload		();
	virtual void	SelectActionAnim	(LPCSTR base, string_path& result);
	virtual void	PlayAnimIdle		();
	virtual void	PlayAnimShoot		();
	virtual void	SelectShootAnim		(string_path& result);
	virtual void	SelectJammedShootBase(string_path& out);	// same, for the shot that JAMS
	virtual void	PlayAnimFireModeSwitch	();
	virtual void	PlayAnimModeSwitch	();
	virtual void	PlayAnimBore		();
	virtual void	PlayAnimIdleMoving	();
	virtual LPCSTR	SprintLoopBase		();
	virtual void	SelectAimIdleAnim	(string_path& result);
	virtual void	SelectDryFireAnim	(string_path& result);
	virtual void	SelectAimTransitionAnim	(bool bAimIn, string_path& result);
	
private:
	virtual	void	net_Spawn_install_upgrades	( Upgrades_type saved_upgrades );
	virtual bool	install_upgrade_impl		( LPCSTR section, bool test );
	virtual	bool	install_upgrade_ammo_class	( LPCSTR section, bool test );
	
public:
	// xrMPE actor torso sets for the two launcher states; empty falls back to the plain group
	shared_str				m_actor_anim_group_gl_off;
	shared_str				m_actor_anim_group_gl_on;
	virtual const shared_str& ActorAnimGroup		() const;

	//�������������� ��������� ��������
	//��� �������������
	CWeaponAmmo*			m_pAmmo2;
	shared_str				m_ammoSect2;
	xr_vector<shared_str>	m_ammoTypes2;
	u32						m_ammoType2;
	shared_str				m_ammoName2;
	int						iMagazineSize2;
	xr_vector<CCartridge>	m_magazine2;
	bool					m_bGrenadeMode;
	virtual bool			InertionGrenadeModeNow	() const	{ return m_bGrenadeMode; }

	CCartridge				m_DefaultCartridge2;
	int						iAmmoElapsed2;

	virtual void UpdateGrenadeVisibility(bool visibility);
	virtual bool IsGrenadeMode	() const { return m_bGrenadeMode; }

	// The "_empty" animation token follows the RIFLE's magazine, never the launcher (GS ModifierStd uses
	// GetAmmoInMagCount, which is the main magazine in either mode). PerformSwitchGL swaps m_magazine with
	// m_magazine2 and rewrites iAmmoElapsed from it, so in grenade mode the base implementation was reading
	// the GRENADE count: firing the last grenade made a loaded rifle play its empty anims, and loading a
	// grenade made an empty rifle stop playing them.
	virtual bool NeedEmptyAnim	() { return 0 == (m_bGrenadeMode ? m_magazine2.size() : m_magazine.size()); }

	// same swap for the controller-suicide checks: in GL mode the rifle rounds live in m_magazine2
	virtual int  SuicideRifleAmmo	() const { return int(m_bGrenadeMode ? m_magazine2.size() : m_magazine.size()); }

	// GS TryShootGLFix (WeaponEvents.pas:2118) -- the launcher needs a rocket OBJECT before it can fire
	virtual void SuicideShoot		();
};