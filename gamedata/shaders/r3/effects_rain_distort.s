-- Rain refraction: the streaks dxRainRender drew, once more into the distortion mask
-- (r3_rendertarget_phase_rain.cpp). t_base = fx\fx_rain_dist, laid out as the pfx distortion
-- textures are: RG = offset around 0.5, B = blur, A = coverage.
function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("stub_default",	"rain_distort")
			: blend		(true, blend.srcalpha, blend.invsrcalpha)
			: alpha_blend	(blend.one,blend.one)	-- the mask alpha counts the distorting draws over a pixel (distort_combine.h)
			: aref		(false, 0)
			: zb		(true, false)
			: fog		(false)

	shader:dx10texture	("s_distort",	t_base)
	shader:dx10sampler	("smp_linear")
end
