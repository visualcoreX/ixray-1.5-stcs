#include "stdafx.h"
#pragma hdrstop

#include "blender_hud_shadow.h"

CBlender_HUD_Shadow::CBlender_HUD_Shadow	()	{	description.CLS		= 0;	}
CBlender_HUD_Shadow::~CBlender_HUD_Shadow	()	{	}

void	CBlender_HUD_Shadow::Compile			(CBlender_Compile& C)
{
	IBlender::Compile		(C);

	// Multiplicative into the accumulator (dst *= src), so a pixel the shader leaves at 1.0 is
	// untouched. Ztest ON / Zwrite OFF: the test is what confines the pass to the HUD depth band,
	// and the compare function is flipped to GREATER right after -- the quad sits at the band edge
	// (see hud_shadow.vs), so "greater" means "the buffer holds something nearer than the edge",
	// which is the HUD.
	C.r_Pass				("hud_shadow", "hud_shadow", FALSE, TRUE, FALSE, TRUE, D3DBLEND_ZERO, D3DBLEND_SRCCOLOR);
	C.PassSET_ZB			(TRUE, FALSE, TRUE);	// TRUE = invert -> D3DCMP_GREATER
	C.r_Sampler_rtf			("s_position", r2_RT_P);
	C.r_End					();
}
