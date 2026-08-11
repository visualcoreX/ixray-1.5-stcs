#include "stdafx.h"
#pragma hdrstop

#include "blender_hud_shadow.h"

CBlender_HUD_Shadow::CBlender_HUD_Shadow	()	{	description.CLS		= 0;	}
CBlender_HUD_Shadow::~CBlender_HUD_Shadow	()	{	}

void	CBlender_HUD_Shadow::Compile			(CBlender_Compile& C)
{
	IBlender::Compile		(C);

	// Multiplicative into the accumulator (dst *= src), so a pixel the shader leaves at 1.0 is
	// untouched. Ztest ON / Zwrite OFF, then the compare flipped to GREATER: the quad sits at the
	// edge of the HUD depth band (hud_shadow.vs), so "greater" means "the buffer holds something
	// nearer than the edge", which is the HUD and only the HUD.
	//
	// No MSAA variants here on purpose. USE_MSAA is a global define in R3, so this one shader
	// already compiles against the Texture2DMS declaration; a soft occlusion term does not need
	// per-sample evaluation the way lighting does.
	C.r_Pass				("hud_shadow", "hud_shadow", FALSE, TRUE, FALSE, TRUE, D3DBLEND_ZERO, D3DBLEND_SRCCOLOR);
	C.PassSET_ZB			(TRUE, FALSE, TRUE);	// TRUE = invert -> D3DCMP_GREATER
	C.r_Stencil				(FALSE);
	C.r_CullMode			(D3DCULL_NONE);
	C.r_dx10Texture			("s_position",	r2_RT_P);
	C.r_dx10Sampler			("smp_nofilter");
	C.r_End					();
}
