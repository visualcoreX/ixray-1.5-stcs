-- models\lenses on R1.
--
-- Anything but a collimator's glass keeps the look R1 always gave it: a faint additive light plane.
--
-- The collimator glass (wpn\lenses) gets a lighter cut of what r2/r3 draw (see r3/models_lenses.s):
--   pass 0  model_collim_glass: the glass's texture (its alpha faded by the aim), the weather's sky mirrored
--           in the coating's colour over it (per vertex), the shader reticle's glow behind it (per pixel)
--           -- one premultiplied-alpha pass;
--   pass 1  model_collim_tint: the coating tint, m_collim_tint multiplied into the view behind the glass.
-- Strictly sorted: the hud draws pass 1 and then pass 0 over it (sorted_L1_passes), the world pass 0 alone,
-- whose binders then give a plain glass. The model reticle (models_collimsight, priority 3) draws after.

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

  -- pass 0: the glass, the sky over it, the reticle's glow behind it (premultiplied alpha)
  shader:begin		("model_collim_reflect","model_collim_glass")
      : fog			(false)
      : zb			(true,false)
      : blend		(true,blend.one,blend.invsrcalpha)
      : aref		(false,0)
      : sorting		(2, true)
  shader:sampler	("s_base")      :texture  (t_base)
  shader:sampler	("s_sky0")      :texture  ("$user$sky0")	: clamp() : f_linear()
  shader:sampler	("s_mark")      :texture  ("$user$collim_mark")	: clamp() : f_linear()

  -- pass 1, drawn first by the hud: the coating's tint over the view behind the glass
  shader:begin		("model_def_lq","model_collim_tint")
      : fog			(false)
      : zb			(true,false)
      : blend		(true,blend.destcolor,blend.srccolor)
      : aref		(false,0)
end
