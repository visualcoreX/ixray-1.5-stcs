#pragma once

// Dust hanging in a handheld torch's light cone, handed from the GAME to the RENDERER once per frame
// through IGame_Persistent::GetTorchDust -- the same bridge the laser beams use (LaserBeamRender.h).
// Everything is resolved to world space and plain numbers; R3 draws it with effects\torch_dust (see
// torch_dust.ps for how each term is used).
//
// The motes are procedural: space is cut into cubic cells and each cell holds at most one mote at a
// hashed spot, so they are sharp, stay fixed in the world (plus the drift) and cost no marching. The
// laser's noise texture decides where the air is dusty: it thins the motes out into drifting clumps.
struct STorchDustRender
{
	Fvector		apex;				// the lens, world space
	float		length;				// the cone ends here along dir, m (the surface the light hits)
	Fvector		dir;				// unit axis
	float		cos_half;			// cos of the cone's half angle

	Fvector		color;				// colour * intensity of a fully lit mote
	float		range;				// the light's reach: a mote this far from the lens is dark, m
	float		fade_cam_start;		// motes fade with the distance from the eye: full up to here...
	float		fade_cam_end;		// ...gone from here, m
	float		fade_start;			// ramps in over the first metres off the lens
	float		edge;				// share of the half angle over which the cone's edge softens (0..1)

	float		cell;				// cell size, m: one mote at most per cell
	float		density;			// share of the cells that hold a mote (before the noise)
	float		radius;				// mote radius, m
	float		radius_var;			// +- share of the radius from mote to mote

	float		twinkle;			// how deep a mote's brightness pulses (0 = steady)
	float		twinkle_speed;		// rad/s
	float		noise_scale;		// noise tiles per metre
	float		noise_threshold;	// noise level below which the air is clear

	Fvector		mote_offset;		// drift of the mote grid, m (already wrapped to its repeat)
	float		noise_contrast;		// how fast the air goes from clear to dusty above the threshold
	Fvector		noise_offset;		// drift of the noise, m (already wrapped to its tile)
	float		noise_amount;		// 0 = even dust everywhere, 1 = only where the noise says

	float		noise_depth;		// texels: depth of one slice of the 3D noise
	float		_reserved[3];

	shared_str	texture;			// noise texture (R, G, B = three independent noises), as the laser's
};
