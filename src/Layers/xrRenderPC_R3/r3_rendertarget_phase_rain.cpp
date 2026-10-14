#include "stdafx.h"

void CRenderTarget::phase_rain()
{
   if( !RImplementation.o.dx10_msaa )
      u_setrt	(rt_Color,NULL,NULL,HW.pBaseZB);
   else
      u_setrt	(rt_Color,NULL,NULL,rt_MSAADepth->pZRT);
	//u_setrt	(rt_Normal,NULL,NULL,HW.pBaseZB);
	RImplementation.rmNormal();
}
// The refraction through the rain: dxRainRender's streaks, kept from render_forward, into the
// distortion mask (rt_Generic_1, already bound with the scene depth by phase_combine). They have to
// wait for it -- the mask holds the combine's bright pass until the bloom has read it.
FVF::LIT* CRenderTarget::rain_distort_begin(u32 max_verts)
{
	m_rain_distort_count	= 0;
	if (!RImplementation.o.distortion_enabled || ps_r__rain_distort < EPS_L)	return nullptr;
	if (m_rain_distort.size() < max_verts)	m_rain_distort.resize(max_verts);
	return					&m_rain_distort.front();
}

void CRenderTarget::phase_rain_distort()
{
	const u32	count		= m_rain_distort_count;
	m_rain_distort_count	= 0;
	if (!count)				return;

	PIX_EVENT(rain_distort);

	if (!s_rain_distort)	s_rain_distort.create("effects\\rain_distort", "fx\\fx_rain_dist");
	if (!g_rain_distort)	g_rain_distort.create(FVF::F_LIT, RCache.Vertex.Buffer(), RCache.QuadIB);

	u32			vOffset;
	FVF::LIT*	v			= (FVF::LIT*)RCache.Vertex.Lock(count, g_rain_distort->vb_stride, vOffset);
	CopyMemory				(v, &m_rain_distort.front(), count*sizeof(FVF::LIT));
	RCache.Vertex.Unlock	(count, g_rain_distort->vb_stride);

	RCache.set_xform_world	(Fidentity);
	RCache.set_xform_view	(Device.mView);
	RCache.set_xform_project(Device.mProject);
	RCache.set_Shader		(s_rain_distort);
	RCache.set_CullMode		(CULL_NONE);	// after set_Shader, which brings its own rasterizer state
	RCache.set_c			("rain_distort", ps_r__rain_distort, 0.f, 0.f, 0.f);
	RCache.set_Geometry		(g_rain_distort);
	RCache.Render			(D3DPT_TRIANGLELIST, vOffset, 0, count, 0, count/2);
	RCache.set_CullMode		(CULL_CCW);
}
