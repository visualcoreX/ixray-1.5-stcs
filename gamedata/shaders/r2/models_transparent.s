-- models\transparent: no script used to exist, the shaders.xr MODEL blender drew it -- its forward path,
-- which this repeats as it was (model_def_lq, alpha blend, aref 0, priority 2 strictly sorted).
--
-- The one addition: crosshair\crosshair_contur, a sight's front aperture (the Rakurs's on the TOZ-34 and the
-- VAL), goes under the rest of the hud's glass (hud_first) -- it sits behind the eyepiece, but the sorting
-- centre of its mesh can put it in front. The collimator glass itself is models\lenses (models_lenses.s).

function normal		(shader, t_base, t_second, t_detail)
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
end
