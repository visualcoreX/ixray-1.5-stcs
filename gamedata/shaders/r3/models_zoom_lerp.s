local tex_env0	= "$user$sky0"
local tex_env1	= "$user$sky1"

function normal   (shader, t_base, t_second, t_detail)
	  shader:begin	("model_scope_eye","model_scope_lense_lerp")
      : fog			(true)
      : zb			(true,false)
      : blend		(true,blend.srcalpha,blend.invsrcalpha)
      : aref		(true,0)
      : sorting		(2,true)
      : distort		(true)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10texture 	("s_vp2",	"$user$scope")	
	shader:dx10texture	("s_scope_hud",	"$user$scopehud")	-- the prism mode: the tube under the glass
	shader:dx10texture	("s_scope_distort",	"$user$scopedistort")	-- the prism mode: the distortion behind the glass
	shader:dx10sampler	("smp_base")	
end

-- The lens-off prism mode (CWeapon::IsScopePrismCfg: the 3D lens switched off in the options): the eyepiece
-- cuts the hud out the way the Rakurs's glass does (models_lenses.s l_special), so the world shows through it,
-- and the lens above draws only its reticle (model_scope_lense*.ps, m_collim_prism.w). Pass 0 the cut, 1 the
-- mark, 2 the late pass -- the order r__dsgraph_render.cpp draws them in. The pixel shaders drop everything
-- unless the mode is on, so with the 3D lens on (or no optic aimed) these passes do nothing.
function l_special	(shader, t_base, t_second, t_detail)
	shader:begin	("model_collim_mask","model_scope_mask")
			: fog		(false)
			: zb		(true,true)
			: blend		(false,blend.one,blend.zero)
			: aref		(false,0)
			: lens_mask	(true)
	shader:dx10zfunc	(cmp_func.always)
	shader:dx10stencil	(true, cmp_func.equal, 128, 255, stencil_op.keep, stencil_op.zero, stencil_op.keep)
	shader:dx10stencil_ref	(128)

	shader:begin	("model_collim_reflect","model_scope_mask")
			: fog		(false)
			: zb		(true,false)
			: blend		(false,blend.one,blend.zero)
			: aref		(false,0)
	shader:dx10stencil	(true, cmp_func.always, 255, 128, stencil_op.keep, stencil_op.replace, stencil_op.keep)
	shader:dx10stencil_ref	(128)
	shader:dx10color_write_enable	(false, false, false, false)

	shader:begin	("model_collim_reflect","model_scope_late")
			: fog		(false)
			: zb		(true,false)
			: blend		(true,blend.one,blend.invsrcalpha)
			: aref		(false,0)
	shader:dx10texture	("s_scope_late",	"$user$scopelate")	-- the prism mode's fade: the frames without the cut, finished
	shader:dx10texture	("s_scope_latec",	"$user$scopelatec")	-- ...and with it
	shader:dx10texture	("s_position",	"$user$position")	-- MSAA: the G-buffer's depth for the hand-made depth test (collim.h collim_late_cover)

	shader:begin	("model_collim_reflect","model_scope_undistort")	-- pass 3: the distortion mask (r_dsgraph_render_lens_undistort)
			: fog		(false)
			: zb		(true,false)
			: blend		(true,blend.srcalpha,blend.invsrcalpha)
			: aref		(false,0)
	-- pass 4: the glass laid into the HDR half of the frame, as the lens lays it into the picture, so the bloom glows
	-- what the lens shows (r_dsgraph_render_lens_hdr, model_scope_hdr.ps)
	shader:begin	("model_scope_eye","model_scope_hdr")
			: fog		(false)
			: zb		(true,false)
			: blend		(true,blend.one,blend.invsrcalpha)
			: aref		(false,0)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10texture	("s_scope_hdr",	"$user$scopehdr")	-- the HDR half as the world left it (CRenderTarget::phase_scope_hdr)
	shader:dx10texture	("s_scope_distort",	"$user$scopedistort")
	shader:dx10texture	("s_position",	"$user$position")	-- MSAA: the G-buffer's depth for the hand-made depth test (collim.h)
	shader:dx10sampler	("smp_base")
	shader:dx10sampler	("smp_rtlinear")
end
