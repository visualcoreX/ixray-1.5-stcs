#include "stdafx.h"
#include "../xrRender/blender_scope_distort.h"

CBlender_ScopeDistort::CBlender_ScopeDistort() { description.CLS = 0; }
CBlender_ScopeDistort::~CBlender_ScopeDistort() {}

void CBlender_ScopeDistort::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);
    switch (C.iElement)
    {
        case 0:
            C.r_Pass("stub_notransform_aa_AA", "scope_distort", FALSE, FALSE, FALSE);
            // Under MSAA both sources are read from their resolved copies: generic0_r is resolved by
            // phase_combine before the bloom, generic1_r is resolved by the scope pass itself right
            // after the mask is drawn. That keeps this one shader free of an MSAA variant.
            C.r_dx10Texture("s_image", RImplementation.o.dx10_msaa ? r2_RT_generic0_r : r2_RT_generic0);
            C.r_dx10Texture("s_distort", RImplementation.o.dx10_msaa ? r2_RT_generic1_r : r2_RT_generic1);
            C.r_dx10Sampler("smp_nofilter");
            C.r_dx10Sampler("smp_rtlinear");
            C.r_End();
            break;
    }
}
