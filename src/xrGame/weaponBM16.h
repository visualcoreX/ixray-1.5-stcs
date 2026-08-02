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
	void			PlayReloadSoundNormal			();	// the single/full/ammochange split (non-jam case)
	virtual void	PlayAnimIdle					();
	virtual void	PlayAnimIdleMoving				();
	virtual LPCSTR	SprintLoopBase					();
	virtual void	PlayAnimShow					();
	virtual void	PlayAnimHide					();
	virtual void	PlayAnimBore					();
	// aim in/out transition + torch/NV gesture get the same 0/1/2 loaded-shell
	// variation as idle (the item-side barrel state is the 2nd config token).
	virtual void	SelectAimTransitionAnim			(bool bAimIn, string_path& result);
	virtual void	SelectActionAnim				(LPCSTR base, string_path& result);
	virtual void	SelectAimIdleAnim				(string_path& result);	// aim-walk + shell suffix
	// GS ModifierBM16 appends the loaded-shell count LAST, so a state token goes IN FRONT of it
	// (anm_reload_jammed_1, not anm_reload_1_jammed) -- teach the splitter about that trailing digit.
	virtual bool	SplitStateSuffix				(LPCSTR name, string_path& stem, string_path& tail);
	virtual void	SelectJammedShootBase			(string_path& out);		// anm_shoot[_aim[_scope]]_<n>
	virtual void	SelectDryFireAnim				(string_path& result);	// anm_fakeshoot[_aim]_<n>
	virtual bool	HasJammedReloadAnim				();						// anm_reload_jammed_<n>
	LPCSTR			ShellSuffix						();	// "_0" / "_1" / "_2" by loaded shells
	DECLARE_SCRIPT_REGISTER_FUNCTION
};
add_to_type_list(CWeaponBM16)
#undef script_type_list
#define script_type_list save_type_list(CWeaponBM16)
