-- normal pp
t_rt 		= "$user$albedo"
t_noise		= "fx\\fx_noise2"

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("stub_notransform_postpr","postprocess")
			: fog	(false)
			: zb 	(false,false)
--	shader:sampler	("s_base0")	:texture("$user$albedo")	: clamp() : f_linear ()
--	shader:sampler	("s_base1")    	:texture("$user$albedo")	: clamp() : f_linear ()
--	shader:sampler	("s_noise")    	:texture("fx\\fx_noise2")	: f_linear ()

	shader:dx10texture	("s_base0", "$user$albedo")
	shader:dx10texture	("s_base1", "$user$albedo")
	shader:dx10texture	("s_noise", "fx\\fx_noise2")
	-- the injury: blood drops on the lens (overlay) and the glare they catch (additive), see pp_injury_drops
	shader:dx10texture	("s_blood_drops", "fx\\fx_blood_lowhealth_00")
	shader:dx10texture	("s_blood_glare", "fx\\fx_blood_lowhealth_02")
	shader:dx10texture	("s_blood_vessels", "fx\\fx_blood_vessels_00")
	shader:dx10texture	("s_blood_droplet", "fx\\fx_blood_droplet")
	shader:dx10texture	("s_bloom", "$user$bloom1")	-- what the glare lights up from

	shader:dx10sampler	("smp_rtlinear")
	shader:dx10sampler	("smp_linear")
end
