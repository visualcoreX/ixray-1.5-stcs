#ifndef PHYSICS_GAME_PARS_H
#define PHYSICS_GAME_PARS_H
extern float object_damage_factor;
extern float collide_volume_max;
extern float collide_volume_min;


struct EffectPars
{
	const static  float vel_cret_sound;
	const static float vel_cret_particles;
	const static float vel_cret_wallmark;
};

// Same collide sound and wallmark as EffectPars, but NO particles ever. For small props that are
// meant to clatter on the floor without a puff of dust -- the Gunslinger item-use trash, which
// throws no particles in GS either. Opted into per section (`no_collide_particles`), so nothing
// else in the world changes.
struct QuietEffectPars
{
	const static  float vel_cret_sound;
	const static float vel_cret_particles;
	const static float vel_cret_wallmark;
};

struct CharacterEffectPars
{
	const static  float vel_cret_sound;
	const static float vel_cret_particles;
	const static float vel_cret_wallmark;
};

void	LoadPhysicsGameParams	()	;
#endif