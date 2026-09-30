-- Laser-designator procedural dot (r3_rendertarget_phase_laser.cpp). Additive, and like the beam without
-- a hardware depth test: laser_dot.ps tests against the G-buffer's view depth itself.
function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("laser_beam",	"laser_dot")
			: sorting	(3, false)
			: blend		(true, blend.one, blend.one)
			: aref		(false, 0)
			: zb		(false, false)
			: fog		(false)

	shader:dx10texture	("s_position",		"$user$position")
	shader:dx10sampler	("smp_nofilter")
end
