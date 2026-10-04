-- normal pp
t_rt 		= "$user$albedo"
t_noise		= "fx\\fx_noise2"

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("null","postprocess")
			: fog	(false)
			: zb 	(false,false)
	shader:sampler	("s_base0")	:texture("$user$albedo")	: clamp() : f_linear ()
	shader:sampler	("s_base1")    	:texture("$user$albedo")	: clamp() : f_linear ()
	shader:sampler	("s_noise")    	:texture("fx\\fx_noise2")	: f_linear ()
	-- the injury: blood drops on the lens (overlay) and the glare they catch (additive), see pp_injury_drops
	shader:sampler	("s_blood_drops")	:texture("fx\\fx_blood_lowhealth_00")	: clamp() : f_linear ()
	shader:sampler	("s_blood_glare")	:texture("fx\\fx_blood_lowhealth_02")	: clamp() : f_linear ()
	shader:sampler	("s_blood_vessels")	:texture("fx\\fx_blood_vessels_00")	: clamp() : f_linear ()
	shader:sampler	("s_blood_droplet")	:texture("fx\\fx_blood_droplet")	: clamp() : f_linear ()
	shader:sampler	("s_bloom")		:texture("$user$bloom1")			: clamp() : f_linear ()	-- what the glare lights up from
end
