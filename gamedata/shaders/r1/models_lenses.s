-- models\lenses on R1.
--
-- Anything but a collimator's glass keeps the look R1 always gave it: a faint additive light plane.
--
-- The collimator glass (wpn\lenses) gets a lighter cut of what r2/r3 draw (see r3/models_lenses.s):
--   pass 0  the glass's texture alpha-blended, as the stock MODEL blender draws it, its alpha faded by
--           the aim (model_collim_glass, m_collim_glass);
--   pass 1  the coating tint, m_collim_tint multiplied into the view behind (2*src*dst, 0.5 = clear);
--   pass 2  the weather's sky mirrored in the coating's colour, per vertex (model_collim_reflect).
-- Strictly sorted, so the HUD draws all three (sorted_L1_passes) and the world only pass 0; the reticle
-- (models_collimsight, priority 3 on r2/r3) is left as R1 had it.

function normal    	(shader, t_base, t_second, t_detail)
  if string.lower(t_base) ~= "wpn\\lenses" then
    shader:begin  	("model_def_lplanes","base_lplanes")
        : fog			(false)
        : zb			(true,false)
        : blend   	(true,blend.srcalpha,blend.one)
        : aref    	(true,0)
        : sorting 	(2, false)
    shader:sampler  	("s_base")      :texture  (t_base)
    return
  end

  shader:begin		("model_def_lq","model_collim_glass")
      : fog			(true)
      : zb			(true,false)
      : blend		(true,blend.srcalpha,blend.invsrcalpha)
      : aref		(true,0)
      : sorting		(2, true)
  shader:sampler	("s_base")      :texture  (t_base)

  shader:begin		("model_def_lq","model_collim_tint")
      : fog			(false)
      : zb			(true,false)
      : blend		(true,blend.destcolor,blend.srccolor)
      : aref		(false,0)

  shader:begin		("model_collim_reflect","model_collim_reflect")
      : fog			(false)
      : zb			(true,false)
      : blend		(true,blend.one,blend.one)
      : aref		(false,0)
  shader:sampler	("s_sky0")      :texture  ("$user$sky0")	: clamp() : f_linear()
end
