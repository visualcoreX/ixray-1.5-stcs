#pragma once

// The weapon's own recoil on the hud (player_hud: the recoil spring, applied after the sway). Each shot
// the camera kicks (CWeaponShotEffector); the weapon takes Inherit of that same kick ON TOP of the
// camera, so it leads the view instead of being glued to it, and comes back once the shooting stops.
// The weapon's lead lives in screen terms -- angles as seen in the world, the hud's own fov already
// taken out -- so a lead of 1 degree puts the barrel and the shot 1 degree off the centre of the screen.
// The lead is held inside a half disc of Radius about the centre (ZoneDown lets it reach a little below
// the centre too); while the trigger is held the mouse moves the weapon inside it (Mouse), which is the
// spray control: the camera only follows when the weapon is at the edge.
// Keys: hud_recoil_<name> (hip) and hud_recoil_aim_<name> (aiming, falls back to hip), looked up in the
// weapon section, the hud section, the profile they name (hud_recoil_profile = <section>), [hud_recoil],
// then the code default (CWeapon::LoadHudRecoil).
struct HudRecoilData
{
	float	Inherit;		// vertical lead per shot, part of the camera's own kick (0.25 = a quarter above it)
	float	InheritHorz;	// the same sideways
	float	Radius;			// rad, how far the weapon may lead the camera
	float	ZoneDown;		// part of Radius the zone reaches below the centre (0 = upper half disc)
	float	Roll;			// rad, peak of the per-shot roll wave, random side
	float	RollTime;		// s, length of one roll wave
	float	RiseFreq;		// Hz, spring that carries the weapon to a kick (and to the mouse)
	float	RiseDamp;		// its damping ratio, 1 = no overshoot
	float	RelaxDelay;		// s after the last shot / mouse move before the weapon starts back
	float	RelaxSpeed;		// rad/s, how fast the lead itself goes back to the centre
	float	RelaxFreq;		// Hz, spring that brings the weapon back after it
	float	RelaxDamp;		// <1 swings past the centre on the way back
	float	Mouse;			// 0..1, part of the mouse the weapon takes inside the zone while firing
	float	CameraSplit;	// 0..1: the camera's kick shrinks by Inherit * this -- the weapon's lead is taken
							// out of the camera's kick instead of added to it (1: camera 1 - Inherit)
	float	PushBack;		// m the weapon slides back per degree it leads upwards
	float	PushMax;		// m, limit of that
	bool	HasPivot;		// false: turn about the sway's pivot (inertia_rot_pivot, inertia_aim_rot_pivot)
	Fvector	Pivot;			// camera space (m)

	HudRecoilData()
	{
		Inherit		= 0.25f;
		InheritHorz	= 0.25f;
		Radius		= deg2rad(4.f);
		ZoneDown	= 0.f;
		Roll		= deg2rad(1.5f);
		RollTime	= 0.3f;
		RiseFreq	= 10.f;
		RiseDamp	= 1.f;
		RelaxDelay	= 0.12f;
		RelaxSpeed	= deg2rad(15.f);
		RelaxFreq	= 3.f;
		RelaxDamp	= 0.45f;
		Mouse		= 0.5f;
		CameraSplit	= 0.f;
		PushBack	= 0.f;
		PushMax		= 0.03f;
		HasPivot	= false;
		Pivot.set	(0.f, 0.f, 0.f);
	}

	HudRecoilData& lerp(const HudRecoilData& from, const HudRecoilData& to, float f)
	{
		auto L = [f](float a, float b) { return a + f * (b - a); };
		Inherit		= L(from.Inherit,		to.Inherit);
		InheritHorz	= L(from.InheritHorz,	to.InheritHorz);
		Radius		= L(from.Radius,		to.Radius);
		ZoneDown	= L(from.ZoneDown,		to.ZoneDown);
		Roll		= L(from.Roll,			to.Roll);
		RollTime	= L(from.RollTime,		to.RollTime);
		RiseFreq	= L(from.RiseFreq,		to.RiseFreq);
		RiseDamp	= L(from.RiseDamp,		to.RiseDamp);
		RelaxDelay	= L(from.RelaxDelay,	to.RelaxDelay);
		RelaxSpeed	= L(from.RelaxSpeed,	to.RelaxSpeed);
		RelaxFreq	= L(from.RelaxFreq,		to.RelaxFreq);
		RelaxDamp	= L(from.RelaxDamp,		to.RelaxDamp);
		Mouse		= L(from.Mouse,			to.Mouse);
		CameraSplit	= L(from.CameraSplit,	to.CameraSplit);
		PushBack	= L(from.PushBack,		to.PushBack);
		PushMax		= L(from.PushMax,		to.PushMax);
		HasPivot	= from.HasPivot || to.HasPivot;
		Pivot.lerp	(from.Pivot, to.Pivot, f);
		return *this;
	}
};
