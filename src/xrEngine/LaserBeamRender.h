#pragma once

// A visible laser-designator beam, handed from the GAME to the RENDERER once per frame through
// IGame_Persistent::GetLaserBeams. Everything is already resolved to world space and plain numbers
// (config, weather and wind are the game's business), so the renderer only builds a camera-facing
// strip around the axis and feeds these values to the effects\laser_beam shader, which draws the
// beam as a thin glowing tube with drifting dust motes inside it. See laser_beam.ps for how each
// term is used.
struct SLaserBeamRender
{
	Fvector		start;				// emitter, world space
	float		length;				// visible length along dir, m
	Fvector		dir;				// unit direction
	float		radius;				// tube radius, m

	Fvector		color;				// colour * intensity, with the fog factor already folded in
	float		view_boost;			// cap of the brightening when the beam is seen along its own axis

	float		fade_cam_start;		// fades out with the distance from the eye: full up to here...
	float		fade_cam_end;		// ...gone from here, m
	float		fade_start;			// ramps in over the first metres of the beam
	float		fade_end;			// and out over the last ones

	float		dust_scale;			// motes per metre (tiling of the noise texture in world space)
	float		dust_threshold;		// noise level a mote starts at
	float		dust_contrast;		// how hard a mote's edge is
	float		dust_amount;		// brightness of the motes

	Fvector		dust_offset1;		// drift of the first noise layer, m (already wrapped to its tile)
	float		core_amount;		// brightness of the smooth glow between the motes
	Fvector		dust_offset2;		// drift of the second layer
	float		dust_scale2;		// second layer's scale, as a multiple of dust_scale

	float		smoke_gain;			// extra brightness where blend-shaded smoke/dust particles cross the beam
	float		smoke_range;		// how far in depth such a particle may be from the beam and still count, m
	float		falloff;			// brightness halves roughly every falloff*0.7 m along the beam (0 = off)
	float		rain_gain;			// the same for rain drops crossing the beam

	float		rain_range;			// m, depth tolerance for the drops
	float		dust_depth;			// depth of one slice of the 3D noise, in texels (about a noise feature's size)
	float		_reserved[2];

	// The dot where the beam lands, drawn procedurally in the same pass (dot_show 0 = none this frame;
	// length 0 above = a dot without a beam). A bright core that bleaches to white, in a coloured halo;
	// once the core is smaller than a pixel it dims instead of shrinking, so a far dot is just coloured.
	Fvector		dot_pos;			// on the surface, world
	float		dot_show;
	Fvector		dot_color;			// colour * dot intensity (no fog boost: the dot is reflected light, not scattered)
	float		dot_core;			// core radius, m
	float		dot_halo;			// halo radius, m
	float		dot_halo_gain;		// halo brightness, relative to the core
	float		dot_white;			// how far past full brightness the core has to be to turn fully white
	float		dot_min_px;			// the halo never gets smaller than this many pixels (it dims instead)
	float		dot_halo_min_gain;	// ...but never dims below this share of its own brightness (0 = it may fade out)
	float		_reserved_dot[3];

	shared_str	texture;			// noise texture; its R, G and B channels are three independent noises
};
