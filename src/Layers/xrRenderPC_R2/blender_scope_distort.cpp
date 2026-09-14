#include "stdafx.h"
#include "../xrRender/blender_scope_distort.h"

CBlender_ScopeDistort::CBlender_ScopeDistort() { description.CLS = 0; }

CBlender_ScopeDistort::~CBlender_ScopeDistort() = default;

void CBlender_ScopeDistort::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);

    switch (C.iElement)
    {
    case 0:
        // fxaa_main.vs is the plain screen-quad vertex shader next door (position + tc0); this pass
        // draws the same quad, so there is no reason for a second copy of it.
        C.r_Pass("fxaa_main", "scope_distort", false, FALSE, FALSE);
        C.r_Sampler("s_image", r2_RT_generic0);
        C.r_Sampler("s_distort", r2_RT_generic1);
        C.r_End();
        break;
    }
}
