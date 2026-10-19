local tex_env0	= "$user$sky0"
local tex_env1	= "$user$sky1"

function normal   (shader, t_base, t_second, t_detail)
	shader:begin	("model_scope_eye","model_scope_lense")
      : fog			(true)
      : zb			(true,false)
      : blend		(true,blend.srcalpha,blend.invsrcalpha)
      : aref		(true,0)
      : sorting		(2,true)
      : distort		(true)
	shader:sampler  ("s_base")		:texture	(t_base)
	shader:sampler  ("s_vp2")		:texture	("$user$scope")
	shader:sampler  ("s_scope_hud")	:texture	("$user$scopehud")	-- the prism mode: the look through the glass
--	shader:sampler	("s_env0")		:texture	(tex_env0)   : clamp()
--	shader:sampler	("s_env1")		:texture	(tex_env1)   : clamp()  
end

-- The lens-off prism mode (CWeapon::IsScopePrismCfg: the 3D lens switched off in the options): the eyepiece cuts
-- the hud out the way the r3 prism sight's glass does, so the world shows through it, and the lens above draws on
-- the frame (model_scope_lense*.ps, m_collim_prism.w). Pass 0 the cut, 1 the mark, 2 the late pass -- the order
-- r__dsgraph_render.cpp draws them in. The pixel shaders drop everything unless the mode is on. (r3: models_zoom.s)
function l_special	(shader, t_base, t_second, t_detail)
	shader:begin	("model_collim_mask","model_scope_mask")
			: fog		(false)
			: zb		(true,true)
			: blend		(false,blend.one,blend.zero)
			: aref		(false,0)
			: lens_mask	(true)
			: zfunc		(cmp_func.always)
			: color_write_enable	(true, true, true, true)	-- DX9: the mark before it turned colour off, and this pass's state block left it so -- the G-buffer under the glass kept the hud's position (the sun shafts then missed the sky there)
			: stencil	(true, cmp_func.equal, 128, 255, stencil_op.keep, stencil_op.zero, stencil_op.keep)
			: stencil_ref	(128)

	shader:begin	("model_collim_reflect","model_scope_mask")
			: fog		(false)
			: zb		(true,false)
			: blend		(false,blend.one,blend.zero)
			: aref		(false,0)
			: stencil	(true, cmp_func.always, 255, 128, stencil_op.keep, stencil_op.replace, stencil_op.keep)
			: stencil_ref	(128)
			: color_write_enable	(false, false, false, false)

	shader:begin	("model_collim_reflect","model_scope_late")
			: fog		(false)
			: zb		(true,false)
			: blend		(true,blend.one,blend.invsrcalpha)
			: aref		(false,0)
	shader:sampler	("s_scope_late")	:texture	("$user$scopelate")	-- the prism mode's fade: the frames without the cut, finished
	shader:sampler	("s_scope_latec")	:texture	("$user$scopelatec")	-- ...and with it

	shader:begin	("model_collim_reflect","model_scope_undistort")	-- pass 3: the distortion mask (r_dsgraph_render_lens_undistort)
			: fog		(false)
			: zb		(true,false)
			: blend		(true,blend.srcalpha,blend.invsrcalpha)
			: aref		(false,0)
			: color_write_enable	(true, true, true, true)	-- DX9: never trust what an earlier pass's block left
			: stencil	(false, cmp_func.always, 255, 255, stencil_op.keep, stencil_op.keep, stencil_op.keep)
end
