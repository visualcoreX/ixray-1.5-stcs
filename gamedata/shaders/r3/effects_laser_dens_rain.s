-- Laser beams: the rain streaks into $user$laser_dens (r3_rendertarget_phase_laser.cpp). t_base = the rain texture.
function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("particle_density",	"laser_dens_rain")
			: blend		(true, blend.one, blend.one)
			: aref		(false, 0)
			: zb		(false, false)
			: fog		(false)

	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
end
