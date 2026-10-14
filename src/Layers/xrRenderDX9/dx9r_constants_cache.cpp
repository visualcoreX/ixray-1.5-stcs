#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/r_constants_cache.h"

void R_constants::flush_cache()
{
	if (a_pixel.b_dirty)
	{
		// fp
		R_constant_array::t_f&	F	= a_pixel.c_f;
		{
			// ps_3_0 has 224 float registers. This uploaded 31 at most, from no further than c32 -- a ps_2_0
			// limit ("hack") -- and a shader whose constants reach past c31 read zeros there: the collimator
			// glass on R2 (its lamps take c0..c23) lost every parameter after them and drew nothing at all.
			const u32	limit	= 224;
			if (F.r_lo() < limit)
			{		
				u32		count		= F.r_hi()-F.r_lo();
				if (count)			{
					if (F.r_lo() + count > limit)	count = limit - F.r_lo();
					PGO		(Msg("PGO:P_CONST:%d",count));
					CHK_DX	(HW.pDevice->SetPixelShaderConstantF	(F.r_lo(), (float*)F.access(F.r_lo()),count));
					F.flush	();
				}
			}
		}
		a_pixel.b_dirty		= false;
	}
	if (a_vertex.b_dirty)
	{
		// fp
		R_constant_array::t_f&	F	= a_vertex.c_f;
		{
			u32		count		= F.r_hi()-F.r_lo();
			if (count)			{
#ifdef DEBUG
				if (F.r_hi() > HW.Caps.geometry.dwRegisters)
				{
					Debug.fatal(DEBUG_INFO,"Internal error setting VS-constants: overflow\nregs[%d],hi[%d]",
						HW.Caps.geometry.dwRegisters,F.r_hi()
						);
				}
				PGO		(Msg("PGO:V_CONST:%d",count));
#endif				&
				CHK_DX	(HW.pDevice->SetVertexShaderConstantF	(F.r_lo(), (float*)F.access(F.r_lo()),count));
				F.flush	();
			}
		}
		a_vertex.b_dirty	= false;
	}
}