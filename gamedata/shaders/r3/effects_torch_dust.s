-- Dust in a handheld torch's light cone (r3_rendertarget_phase_laser.cpp, phase_torch_dust). t_base =
-- the noise texture that clumps the motes (the laser beam's: R, G and B are three independent noises).
-- Drawn on one quad over the view; no hardware depth test for the same reason as the laser beam (the HUD's
-- squeezed depth) -- torch_dust.ps tests every mote against the G-buffer's view depth itself.
function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("laser_beam",	"torch_dust")
			: sorting	(3, false)
			: blend		(true, blend.one, blend.one)
			: aref		(false, 0)
			: zb		(false, false)
			: fog		(false)

	shader:dx10texture	("s_base",			t_base)
	shader:dx10texture	("s_position",		"$user$position")

	shader:dx10sampler	("smp_linear")
	shader:dx10sampler	("smp_nofilter")
end
