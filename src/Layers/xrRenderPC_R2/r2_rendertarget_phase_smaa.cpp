#include "stdafx.h"

#include "r2_rendertarget.h"

// One full-screen quad per SMAA element; the caller picks the target and the pass. Same quad the
// FXAA phase draws, half-texel offset included -- without it DX9 samples half a pixel off and the
// edge pass reports edges everywhere.
void CRenderTarget::phase_smaa(u32 pass) {
    u32 Offset = 0;
    float _w = float(Device.dwWidth);
    float _h = float(Device.dwHeight);
    float ddw = 1.0f / _w;
    float ddh = 1.0f / _h;

    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    FVF::V* pv = (FVF::V*)RCache.Vertex.Lock(4, g_smaa->vb_stride, Offset);
    pv->set(ddw - 0.5f, ddh + _h - 0.5f, 0.0f, 0.0f, 1.0f);
    pv++;
    pv->set(ddw - 0.5f, ddh - 0.5f, 0.0f, 0.0f, 0.0f);
    pv++;
    pv->set(ddw + _w - 0.5f, ddh + _h - 0.5f, 0.0f, 1.0f, 1.0f);
    pv++;
    pv->set(ddw + _w - 0.5f, ddh - 0.5f, 0.0f, 1.0f, 0.0f);
    pv++;
    RCache.Vertex.Unlock(4, g_smaa->vb_stride);

    RCache.set_Element(s_smaa->E[pass]);
    RCache.set_Geometry(g_smaa);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}
