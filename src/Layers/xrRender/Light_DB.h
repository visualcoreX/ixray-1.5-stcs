#pragma once

#include "light.h"
#include "light_package.h"

class	CLight_DB
{
private:
	xr_vector<ref_light>	v_static;
	xr_vector<ref_light>	v_hemi;
public:
	ref_light				sun_original;
	ref_light				sun_adapted;
	light_Package			package;

	// This frame's dynamic lights as the rain sees them (dxRainRender). Copied out when a light is
	// picked for the frame: the rain is drawn last, after the light passes have emptied `package`
	// (and torn every shadowed omni into six pieces on the way).
	struct rain_light
	{
		Fvector				position;
		float				range;
		Fvector				color;			// LOD fade included
		float				cone_cos;		// cosine of a spot's half-angle; below -1 for an omni
		Fvector				direction;
	};
	xr_vector<rain_light>	v_rain;
private:
	void					add_rain_light		(light*		L	);
public:
	void					add_light			(light*		L	);

	void					Load				(IReader*	fs	);
#if RENDER != R_R1
	void					LoadHemi			();
#endif
	void					Unload				();

	light*					Create				();
	void					Update				();

	CLight_DB				();
	~CLight_DB				();
};
