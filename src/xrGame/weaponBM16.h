#pragma once

#include "weaponShotgun.h"
#include "script_export_space.h"

class CWeaponBM16 :public CWeaponShotgun
{
	typedef CWeaponShotgun inherited;

public:
	virtual			~CWeaponBM16					();
	virtual void	Load							(LPCSTR section);

protected:
	virtual void	PlayAnimShoot					();
	virtual void	PlayAnimReload					();
	virtual void	PlayReloadSound					();
	virtual void	PlayAnimIdle					();
	virtual void	PlayAnimIdleMoving				();
	virtual void	PlayAnimIdleSprint				();
	virtual void	PlayAnimShow					();
	virtual void	PlayAnimHide					();
	virtual void	PlayAnimBore					();
	// aim in/out transition + torch/NV gesture get the same 0/1/2 loaded-shell
	// variation as idle (the item-side barrel state is the 2nd config token).
	virtual void	SelectAimTransitionAnim			(bool bAimIn, string_path& result);
	virtual void	SelectActionAnim				(LPCSTR base, string_path& result);
	virtual void	SelectAimIdleAnim				(string_path& result);	// aim-walk + shell suffix
	LPCSTR			ShellSuffix						();	// "_0" / "_1" / "_2" by loaded shells
	DECLARE_SCRIPT_REGISTER_FUNCTION
};
add_to_type_list(CWeaponBM16)
#undef script_type_list
#define script_type_list save_type_list(CWeaponBM16)
