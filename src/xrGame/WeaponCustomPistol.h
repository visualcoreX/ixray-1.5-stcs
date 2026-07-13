#pragma once

#include "WeaponMagazined.h"

class CWeaponCustomPistol: public CWeaponMagazined
{
private:
	typedef CWeaponMagazined inherited;
public:
					CWeaponCustomPistol	();
	virtual			~CWeaponCustomPistol();
	virtual	int		GetCurrentFireMode	() { return 1; };
	// semi-auto (one shot per trigger pull) despite the infinite-queue default -> never
	// auto-resume fire when the trigger is held through an aim transition.
	virtual bool	CanAutoResumeFire	() const { return false; }
protected:
	virtual void	FireEnd				();
	virtual void	switch2_Fire		();
};
