#pragma once
#include "weaponcustompistol.h"

class CWeaponPistol :
	public CWeaponCustomPistol
{
	typedef CWeaponCustomPistol inherited;
public:
					CWeaponPistol	();
	virtual			~CWeaponPistol	();

	virtual void	Load			(LPCSTR section);
	
	virtual void	switch2_Reload	();

	virtual void	OnShot			();
	virtual void	OnAnimationEnd	(u32 state);
	virtual void	net_Destroy		();
	virtual void	OnH_B_Chield	();

	//��������
	virtual void	PlayAnimShow	();
	virtual void	PlayAnimIdle	();
	virtual void	PlayAnimIdleMoving	();
	virtual void	PlayAnimIdleSprint	();
	virtual void	PlayAnimHide	();
	virtual void	PlayAnimReload	();
	virtual void	SelectShootAnim	(string_path& result);	// ADS shoot + last-round slide-lock
	virtual void	SelectAimTransitionAnim(bool bAimIn, string_path& result);	// empty aim in/out
	virtual void	PlayAnimBore	();
	virtual void	SelectAimIdleAnim(string_path& result);	// empty + directional aim-walk

	virtual void	UpdateSounds	();
protected:	
	virtual bool	AllowFireWhileWorking() {return true;}

	ESoundTypes			m_eSoundClose;
};
