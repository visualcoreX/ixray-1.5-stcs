-- distort
function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("null","postprocess_d")
			: fog	(false)
			: zb 	(false,false)
	shader:sampler	("s_distort")  	:texture(t_distort)	: clamp() : f_linear ()
	shader:sampler	("s_base0")	:texture(t_rt)		: clamp() : f_linear ()
	shader:sampler	("s_base1")    	:texture(t_rt)		: clamp() : f_linear ()
	shader:sampler	("s_noise")    	:texture(t_noise)	: f_linear ()
	-- the injury: blood drops, vessels and single droplets on the lens (see pp_scope.h)
	shader:sampler	("s_blood_drops")	:texture("fx\\fx_blood_lowhealth_00")	: clamp() : f_linear ()
	shader:sampler	("s_blood_vessels")	:texture("fx\\fx_blood_vessels_00")	: clamp() : f_linear ()
	shader:sampler	("s_blood_droplet")	:texture("fx\\fx_blood_droplet")	: clamp() : f_linear ()
end
