-- models\transparent: no script used to exist, the shaders.xr MODEL blender drew it -- its forward path, which pass 0
-- repeats as it was (model_def_lq, alpha blend, aref 0, priority 2 strictly sorted).
--
-- wpn\zero_alpha on it is a collimator's glass, and gets a second pass: the coating on that glass. It multiplies
-- the view behind by m_collim_tint (destcolor*src + dst*srccolor = 2*src*dst, so 0.5 = clear glass),
-- which CActor fills from the held weapon's collimator_tint. Only the HUD draws more than pass 0
-- (sorted_L1_passes), so the same gun on the ground or in an NPC's hands keeps its plain glass. The
-- reticle draws after all of the HUD's glass (models_collimsight.s, priority 3), so it stays untinted.
--
-- A third pass mirrors the weather's sky in the coating's colour (model_collim_reflect, added one/one
-- over the tinted view): m_collim_reflect from the optic's collimator_reflect, 0 = nothing added.

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("model_def_lq","model_def_lq")
			: fog		(true)
			: zb		(true,false)
			: blend		(true,blend.srcalpha,blend.invsrcalpha)
			: aref		(true,0)
			: sorting	(2, true)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")

	if string.lower(t_base) == "wpn\\zero_alpha" then
		shader:begin	("model_def_lq","model_collim_tint")
				: fog		(false)
				: zb		(true,false)
				: blend		(true,blend.destcolor,blend.srccolor)
				: aref		(false,0)

		-- ...and the sky it mirrors, in the coating's colour, added over the tinted view
		shader:begin	("model_collim_reflect","model_collim_reflect")
				: fog		(false)
				: zb		(true,false)
				: blend		(true,blend.one,blend.one)
				: aref		(false,0)
		shader:dx10texture	("s_sky0",	"$user$sky0")
		shader:dx10texture	("s_sky1",	"$user$sky1")
		shader:dx10sampler	("smp_rtlinear")
	end
end
