#include "stdafx.h"
#include "../xrRender/blender_smaa.h"

CBlender_SMAA::CBlender_SMAA() {
    description.CLS = 0;
}

CBlender_SMAA::~CBlender_SMAA() = default;

void CBlender_SMAA::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0:
        // Scene snapshot into generic1. The blend pass below cannot read generic0 while writing it,
        // and the combine that follows the filter reads generic0, so the copy goes at the front and
        // the last pass writes back into place.
        C.r_Pass("smaa_copy", "smaa_copy", false, FALSE, FALSE);
        C.r_Sampler_clf("s_smaa_image", r2_RT_generic0);
        C.r_End();
        break;
    case 1:
        C.r_Pass("smaa_edge_detect", "smaa_edge_detect", false, FALSE, FALSE);
        C.r_Sampler_clf("s_smaa_image", r2_RT_generic1);
        C.r_End();
        break;
    case 2:
        C.r_Pass("smaa_bweight_calc", "smaa_bweight_calc", false, FALSE, FALSE);
        C.r_Sampler_clf("s_edgetex", r2_RT_smaa_edgetex);
        // the DX9 table, not the DX10 one R3 binds: SMAA_HLSL_3 reads the area texture as .ra,
        // which is how this file is packed
        C.r_Sampler_clf("s_areatex", "smaa\\smaa_area_tex_dx9");
        C.r_Sampler_rtf("s_searchtex", "smaa\\smaa_search_tex");
        C.r_End();
        break;
    case 3:
        C.r_Pass("smaa_neighbour_blend", "smaa_neighbour_blend", false, FALSE, FALSE);
        C.r_Sampler_clf("s_smaa_image", r2_RT_generic1);
        C.r_Sampler_clf("s_blendtex", r2_RT_smaa_blendtex);
        C.r_End();
        break;
    }
}
