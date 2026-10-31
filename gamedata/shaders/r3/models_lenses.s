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
-- The hud draws pass 1 first and pass 0 over it (sorted_L1_passes); the world draws pass 0
-- alone, and its binders then give a plain glass -- full texture, no sky, no reticle. The model reticle
-- draws after all of the hud's glass (models_collimsight.s, priority 3).

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
		shader:dx10texture	("s_base",	t_base)
		shader:dx10sampler	("smp_base")
		return
	end

	shader:begin	("model_collim_reflect","model_collim_glass")
			: fog		(false)
			: zb		(true,false)
			: blend		(true,blend.one,blend.invsrcalpha)
			: aref		(false,0)
			: sorting	(2, true)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10texture	("s_sky0",	"$user$sky0")
	shader:dx10texture	("s_sky1",	"$user$sky1")
	shader:dx10texture	("s_mark",	"$user$collim_mark")
	shader:dx10texture	("s_lens_cube",	"$user$lens_cube")	-- the world the coating mirrors (CRender::render_lens_cube)
	shader:dx10sampler	("smp_base")
	shader:dx10sampler	("smp_rtlinear")

	shader:begin	("model_collim_reflect","model_collim_tint")
			: fog		(false)
			: zb		(true,false)
			: blend		(true,blend.destcolor,blend.srccolor)
			: aref		(false,0)
	shader:dx10texture	("s_mark",	"$user$collim_mark")
	shader:dx10sampler	("smp_rtlinear")
end

-- A prism sight's eyepiece (the Rakurs's glass), drawn by r_dsgraph_render_hud right after the hud's
-- deferred pass, the world still to come:
--   pass 1  model_collim_mask, the mark, drawn first: inside the field stop, wherever the glass is in front
--           of the hud (depth-tested in the hud's slice -- not where the sight's rim, nearer the eye, hides
--           it), stencil bit 0x80 (the hud's deferred pass writes 0x7f only, so it is free);
--   pass 0  model_collim_mask, the cut, drawn next: where marked, the far plane back into the depth and 0
--           into the stencil, so the world fills it as if the sight's body were not there. It has to be pass
--           0: the compiler turns the depth write off on every later pass (PassSET_ZB), and without it the
--           stencil was cleared over the hud's own depth -- the world could not draw there, the sky showed.
--           It clears the G-buffer there too (model_collim_mask.ps), for where only the sky is behind;
--   pass 2  model_collim_late, drawn over the finished picture (r_dsgraph_render_lens_late, after the bloom
--           and the sun's shafts, before the lens flares): where the stop is black, the picture replaced by the glass
--           redrawn, an even haze of the shafts' (collimator_prism_veil) over the whole of it -- nothing
--           added later can light the black up.
-- Only the collimator glass gets lens_mask -- the element has to exist for every texture once l_special
-- does, the rest simply never draws it -- and the pixel shaders drop it all unless the held optic is a prism.
function l_special	(shader, t_base, t_second, t_detail)
	shader:begin	("model_collim_mask","model_collim_mask")
			: fog		(false)
			: zb		(true,true)
			: blend		(false,blend.one,blend.zero)
			: aref		(false,0)
			: lens_mask	(string.lower(t_base) == "wpn\\lenses")
	shader:dx10zfunc	(cmp_func.always)
	shader:dx10stencil	(true, cmp_func.equal, 128, 255, stencil_op.keep, stencil_op.zero, stencil_op.keep)
	shader:dx10stencil_ref	(128)

	shader:begin	("model_collim_reflect","model_collim_mask")
			: fog		(false)
			: zb		(true,false)
			: blend		(false,blend.one,blend.zero)
			: aref		(false,0)
	shader:dx10stencil	(true, cmp_func.always, 255, 128, stencil_op.keep, stencil_op.replace, stencil_op.keep)
	shader:dx10stencil_ref	(128)
	shader:dx10color_write_enable	(false, false, false, false)

	shader:begin	("model_collim_reflect","model_collim_late")
			: fog		(false)
			: zb		(true,false)
			: blend		(true,blend.one,blend.invsrcalpha)
			: aref		(false,0)
	shader:dx10texture	("s_vollight",	"$user$generic2")
	shader:dx10texture	("s_base",	t_base)
	shader:dx10texture	("s_sky0",	"$user$sky0")
	shader:dx10texture	("s_sky1",	"$user$sky1")
	shader:dx10texture	("s_mark",	"$user$collim_mark")
	shader:dx10texture	("s_lens_cube",	"$user$lens_cube")	-- the world the coating mirrors (CRender::render_lens_cube)
	shader:dx10sampler	("smp_base")
	shader:dx10sampler	("smp_rtlinear")

	shader:begin	("model_collim_reflect","model_collim_undistort")	-- pass 3: the distortion mask (r_dsgraph_render_lens_undistort)
			: fog		(false)
			: zb		(true,false)
			: blend		(false,blend.one,blend.zero)
			: aref		(false,0)
end
