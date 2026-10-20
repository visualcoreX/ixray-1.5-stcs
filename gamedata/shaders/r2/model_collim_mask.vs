// An eyepiece cutting the hud out behind it (models_zoom*.s l_special, drawn by r_dsgraph_render_hud right after
// the hud's deferred pass): the glass's own vertex shader, but on the far plane -- with zfunc always it puts the far
// plane back into the depth, and 0 into the stencil, inside the eyepiece, and the world drawn next fills it as if
// the sight's body were not there. (r3: the same, plus a prism sight's field stop.)
#define COLLIM_FAR_PLANE
#include "model_collim_reflect.vs"
