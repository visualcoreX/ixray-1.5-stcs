-- models\lenses: no script used to exist, the shaders.xr MODEL blender drew it -- its forward path, which
-- this repeats as it was (model_def_lq, alpha blend, aref 0, priority 2 strictly sorted) for anything but
-- a collimator's glass. crosshair\crosshair_contur, a sight's front aperture, goes under the rest of the
-- hud's glass (hud_first): it sits behind the eyepiece, but the sorting centre of its mesh can put it in front.
--
-- wpn\lenses is a collimator's glass, two passes (collim.h holds the constants and the shared code):
--   pass 0  model_collim_glass: the glass's texture (its alpha faded by the aim), the sky the coating
--           mirrors over it, the reticle's glow behind it -- one premultiplied-alpha pass (one, invsrcalpha);
--   pass 1  model_collim_tint: what darkens the view BEHIND the glass -- the coating's tint (2*src*dst, 0.5
--           = clear), a prism sight's field stop, an etched reticle's lines.
-- The hud draws pass 1 first and pass 0 over it (sorted_L1_passes); the world draws pass 0 alone, and its
-- binders then give a plain glass -- full texture, no sky, no reticle. The model reticle draws after all of
-- the hud's glass (models_collimsight.s, priority 3).

function normal		(shader, t_base, t_second, t_detail)
	local collim	= string.lower(t_base) == "wpn\\lenses"

	if not collim then
		-- the front aperture fades out on a prism sight (model_collim_contour, m_collim_glass.z)
		local contour	= string.lower(t_base) == "crosshair\\crosshair_contur"
		shader:begin	("model_def_lq", contour and "model_collim_contour" or "model_def_lq")
				: fog		(true)
				: zb		(true,false)
				: blend		(true,blend.srcalpha,blend.invsrcalpha)
				: aref		(true,0)
				: sorting	(2, true)
				: hud_first	(contour)	-- a sight's front aperture: under the eyepiece glass
		shader:sampler	("s_base")	:texture	(t_base)
		return
	end

	shader:begin	("model_collim_reflect","model_collim_glass")
			: fog		(false)
			: zb		(true,false)
			: blend		(true,blend.one,blend.invsrcalpha)
			: aref		(false,0)
			: sorting	(2, true)
	shader:sampler	("s_base")	:texture	(t_base)
	shader:sampler	("s_sky0")	:texture	("$user$sky0")	: clamp() : f_linear()
	shader:sampler	("s_sky1")	:texture	("$user$sky1")	: clamp() : f_linear()
	shader:sampler	("s_mark")	:texture	("$user$collim_mark")	: clamp() : f_linear()

	shader:begin	("model_collim_reflect","model_collim_tint")
			: fog		(false)
			: zb		(true,false)
			: blend		(true,blend.destcolor,blend.srccolor)
			: aref		(false,0)
	shader:sampler	("s_mark")	:texture	("$user$collim_mark")	: clamp() : f_linear()
end

-- The hud's glass over the distortion mask (r_dsgraph_render_lens_undistort draws pass 3 of every masked glass): a
-- collimator's glass puts the mask back to "no offset" over itself, so its dot does not ripple with the water or
-- the haze behind. Passes 0..2 are r3's prism-sight passes -- r2 has no prism mode, they draw nothing.
-- Only the collimator glass gets lens_mask, as on r3.
function l_special	(shader, t_base, t_second, t_detail)
	shader:begin	("model_collim_reflect","model_clip")
			: fog		(false)
			: zb		(true,false)
			: blend		(false,blend.one,blend.zero)
			: aref		(false,0)
			: lens_mask	(string.lower(t_base) == "wpn\\lenses")

	shader:begin	("model_collim_reflect","model_clip")
			: fog		(false)
			: zb		(true,false)
			: blend		(false,blend.one,blend.zero)
			: aref		(false,0)

	shader:begin	("model_collim_reflect","model_collim_late")	-- pass 2: the reticle's glow, after the distortion
			: fog		(false)
			: zb		(true,false)
			: blend		(true,blend.one,blend.invsrcalpha)
			: aref		(false,0)
			: color_write_enable	(true, true, true, true)
			: stencil	(false, cmp_func.always, 255, 255, stencil_op.keep, stencil_op.keep, stencil_op.keep)
	shader:sampler	("s_base")	:texture	(t_base)
	shader:sampler	("s_mark")	:texture	("$user$collim_mark")	: clamp() : f_linear()

	shader:begin	("model_collim_reflect","model_collim_undistort")	-- pass 3
			: fog		(false)
			: zb		(true,false)
			: blend		(false,blend.one,blend.zero)
			: aref		(false,0)
			: color_write_enable	(true, true, true, true)
			: stencil	(false, cmp_func.always, 255, 255, stencil_op.keep, stencil_op.keep, stencil_op.keep)
end
