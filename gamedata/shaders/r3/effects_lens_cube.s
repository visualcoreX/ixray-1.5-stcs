-- $user$lens_cube (CRenderTarget::lens_cube_combine, lens_cube.h): one face of the cubemap the hud's glass mirrors,
-- lit from its own small G-buffer into its place in the atlas
function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("stub_notransform_t",	"lens_cube")
			: blend		(false, blend.one, blend.zero)
			: aref		(false, 0)
			: zb		(false, false)
			: fog		(false)
	shader:dx10texture	("s_cube_p",	"$user$cube_p")
	shader:dx10texture	("s_cube_n",	"$user$cube_n")
	shader:dx10texture	("s_cube_c",	"$user$cube_c")
	shader:dx10texture	("s_sky0",		"$user$sky0")
	shader:dx10texture	("s_sky1",		"$user$sky1")
	shader:dx10sampler	("smp_rtlinear")
end
