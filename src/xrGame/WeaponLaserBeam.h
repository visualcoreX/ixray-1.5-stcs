#pragma once

#include "../xrEngine/LaserBeamRender.h"
#include "../xrEngine/Render.h"		// ref_light for the dot's surface light

class IKinematics;

// Where a model's MESH beam (the laser_ray_bones geometry) leaves the device and which way it runs,
// read off that geometry once per model. Kept in the space of the nearest ancestor of the beam bones
// that is not itself a beam bone (the device body), because a hidden bone -- laser_beam_hide_mesh, a
// switched-off laser -- stops having its matrix recalculated, while the device's keeps moving.
struct SLaserEmitter
{
	IKinematics*	model;			// what this was worked out for (hud models get re-created)
	bool			valid;
	u16				ref_bone;		// the device bone everything below is relative to
	Fvector			pos;			// near end of the mesh beam, ref_bone space
	Fvector			dir;			// its axis, unit, ref_bone space
	Fmatrix			attach_rel;		// laserdot_attach_bone in ref_bone space (bind pose), for when it is hidden

					SLaserEmitter	()	{ model = nullptr; valid = false; }
	// ray_bones: CSV of the beam bones; attach_bone: laserdot_attach_bone. Cheap after the first call.
	bool			Update			(IKinematics* K, LPCSTR ray_bones, LPCSTR attach_bone);
};

// The laser designator's volumetric beam: a thin glowing tube with dust drifting in it, drawn by the
// renderer (R3, effects\laser_beam) from what Publish() hands it every frame. It lives next to the
// weapon's old mesh beam (the `line` bones), which stays as it is unless hide_mesh says otherwise.
//
// Config: the weapon's laser_params_section names it with `laser_beam_section = <section>`; that
// section holds everything below as laser_beam_* keys (see Load for names and defaults). No section
// = no volumetric beam, exactly the old behaviour.
struct SLaserBeamParams
{
	bool		enabled;			// the section is there (beam and/or dot)
	bool		beam;				// laser_beam_enabled: draw the volumetric beam
	bool		hide_mesh;			// hide the HUD model's own beam bones while this beam draws

	Fvector		color;				// 0..1
	float		intensity;
	float		radius;				// m
	float		max_length;			// m; a beam that hits nothing dissolves over fade_end_far before this
	float		fade_cam_start;		// m from the eye: full brightness up to here...
	float		fade_cam_end;		// ...gone from here
	float		fade_start;			// m: ramps in off the emitter
	float		fade_end_hit;		// m: ends this softly on the surface it hits
	float		fade_end_far;		// m: and this softly when it runs out at max_length
	float		falloff;			// m, 0 = off: brightness loss along the beam
	float		view_boost;			// cap of the brightening when looking down the beam

	float		core;				// smooth glow between the motes
	float		dust;				// motes
	float		dust_scale;			// noise tiles per metre
	float		dust_scale2;		// second noise layer, multiple of dust_scale
	float		dust_threshold;
	float		dust_contrast;
	float		dust_depth;			// texels: depth of one slice of the 3D noise, ~ the texture's feature size
	Fvector		dust_drift;			// m/s, world: drift of the first noise layer...
	Fvector		dust_drift2;		// ...and of the second, so the two slide past each other
	float		dust_wind;			// share of the weather's wind added to both drifts

	// brightness factor = fog_base + fog_gain * fog_density
	//                   + fog_dist_gain * (1 - fog_distance / fog_dist_max) ^ fog_dist_power
	// (nearly every weather keeps fog_density at 0.9, so how close the fog wall stands is what tells a
	// foggy day apart: fog_distance 0 = full effect, fading out towards fog_dist_max)
	float		fog_base;
	float		fog_gain;
	float		fog_dist_gain;
	float		fog_dist_max;		// m
	float		fog_dist_power;

	float		smoke_gain;			// brightening where blend-shaded smoke/dust particles cross the beam
	float		smoke_range;		// m of depth such a particle may be off the beam and still count
	float		rain_gain;			// the same for rain drops (the weather's rain streaks)
	float		rain_range;

	shared_str	texture;

	// Coaxial with the weapon: while the gun looks ahead the beam runs to the dot; when an animation
	// swings it off the view (reload, sprint) by axis_angle_start..end the beam turns to the
	// weapon's own axis instead -- and the dot with it when dot_follow.
	float		axis_angle_start;	// radians
	float		axis_angle_end;
	bool		dot_follow;

	// The dot, drawn procedurally by the renderer next to the beam (laser_beam_dot). The old particle dot
	// (laserdot_particle_*) stays as the fallback: laser_beam_dot_particle, on by default only while the
	// procedural dot is off.
	bool		dot;
	bool		dot_particle;
	float		dot_intensity;		// core brightness: past 1 the core bleaches to white
	float		dot_core;			// m
	float		dot_halo;			// m
	float		dot_halo_gain;
	float		dot_white;
	float		dot_min_px;
	float		dot_halo_min_gain;	// floor of the far halo's dimming (share of its brightness)
	// ...and the light it throws on what is around it (laser_beam_dot_light): a small point light, no
	// shadows, so it costs next to nothing
	bool		dot_light;
	float		dot_light_range;	// m
	float		dot_light_bright;
	float		dot_light_offset;	// m it hangs off the surface, towards the emitter
	ref_light	light;

	// runtime: integrated drift of the two noise layers (kept wrapped to one tile)
	Fvector		acc1;
	Fvector		acc2;
	// runtime: the mesh beam's emitter on the hud and on the world model
	SLaserEmitter	hud;
	SLaserEmitter	world;

	// 0 = aim at the dot, 1 = follow the weapon's axis; `swing` = radians the apparent axis is off the view
	float		AxisWeight			(float swing) const;

				SLaserBeamParams	()	{ enabled = beam = dot = dot_light = false; dot_particle = true; hide_mesh = false; acc1.set(0.f, 0.f, 0.f); acc2.set(0.f, 0.f, 0.f); }
	void		Load				(LPCSTR sect);	// NULL / missing section = disabled
	// Hand the beam from `from` to `to` to the renderer for this frame. `hit` = `to` lies on a surface
	// (otherwise the beam just ran out of range). `dot_ok` = there is a surface for the dot, at dot_pos.
	// `axis` = the way the emitter points: nothing is drawn BEHIND the emitter along it (a gun pushed into
	// a wall has its emitter past the surface the trace found -- the beam would run back at the camera).
	void		Publish				(const Fvector& from, const Fvector& axis, const Fvector& to, bool hit, bool dot_ok, const Fvector& dot_pos);
	void		LightOff			();			// the dot's light, whenever the dot is not drawn
	bool		DrawParticleDot		() const	{ return !enabled || dot_particle; }
};

// Move a point of the HUD model to where the WORLD projection has to draw it to appear over the same
// pixel: the HUD is drawn with its own (narrower) field of view, so the beam's start would otherwise
// float off the emitter on screen. Depth along the view axis is kept.
void	LaserBeam_HudPointToWorld	(Fvector& p);
// The same for a direction: a straight line of the HUD model maps onto a straight line, so a beam
// from HudPointToWorld(start) along HudDirToWorld(dir) covers exactly the pixels of the HUD's axis.
void	LaserBeam_HudDirToWorld		(Fvector& d);

// What CGamePersistent::GetLaserBeams returns: the beams published this frame.
u32		LaserBeams_Get				(const SLaserBeamRender*& beams);
