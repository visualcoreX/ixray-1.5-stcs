-- models\lenses: no script used to exist, the shaders.xr MODEL blender drew it -- its forward path, which pass 0
-- repeats as it was (model_def_lq, alpha blend, aref 0, priority 2 strictly sorted).
--
-- wpn\lenses on it is a collimator's glass, and gets a second pass: the coating on that glass. It multiplies
-- the view behind by m_collim_tint (destcolor*src + dst*srccolor = 2*src*dst, so 0.5 = clear glass),
-- which CActor fills from the held weapon's collimator_tint. Only the HUD draws more than pass 0
-- (sorted_L1_passes), so the same gun on the ground or in an NPC's hands keeps its plain glass. The
-- reticle draws after all of the HUD's glass (models_collimsight.s, priority 3), so it stays untinted.

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("model_def_lq","model_def_lq")
			: fog		(true)
			: zb		(true,false)
			: blend		(true,blend.srcalpha,blend.invsrcalpha)
			: aref		(true,0)
			: sorting	(2, true)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")

	if string.lower(t_base) == "wpn\\lenses" then
		shader:begin	("model_def_lq","model_collim_tint")
				: fog		(false)
				: zb		(true,false)
				: blend		(true,blend.destcolor,blend.srccolor)
				: aref		(false,0)
	end
end
