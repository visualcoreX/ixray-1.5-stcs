// A prism sight's eyepiece, cutting the hud out behind it (models_lenses.s l_special, drawn by
// r_dsgraph_render_hud right after the hud's deferred pass): the glass's own vertex shader, but on the far
// plane -- with zfunc always it puts the far plane back into the depth, and 0 into the stencil, inside the
// eyepiece, and the world drawn next fills it as if the sight's body were not there. The pixel shader
// needs the glass's position and the scope's axes for the field stop.
#define COLLIM_FAR_PLANE
#include "model_collim_reflect.vs"
