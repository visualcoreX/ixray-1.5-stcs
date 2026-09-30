-- Laser-designator volumetric beam (r3_rendertarget_phase_laser.cpp). t_base = the dust noise texture,
-- whose R, G and B channels are three independent tileable noises.
-- No hardware depth test: the HUD's depth is squeezed into [0, 0.02] and would hide the beam wherever it
-- crosses the weapon, so laser_beam.ps tests against the G-buffer's view depth itself.
function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("laser_beam",	"laser_beam")
			: sorting	(3, false)
			: blend		(true, blend.one, blend.one)
			: aref		(false, 0)
			: zb		(false, false)
			: fog		(false)

	shader:dx10texture	("s_base",			t_base)
	shader:dx10texture	("s_laser_dens",	"$user$laser_dens")
	shader:dx10texture	("s_position",		"$user$position")

	shader:dx10sampler	("smp_linear")
	shader:dx10sampler	("smp_rtlinear")
	shader:dx10sampler	("smp_nofilter")
end
