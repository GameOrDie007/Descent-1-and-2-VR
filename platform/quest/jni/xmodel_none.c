/* Descent 1 & 2 VR, Android: the high-res model replacement (xmodel) is
 * desktop OpenGL; on OpenGL ES 1 there are none, so the game draws its own
 * models everywhere. */
#include "xmodel.h"

void xmodel_load_all(void) {}
void xmodel_free_all(void) {}
void xmodel_load_gl_all(void) {}
void xmodel_free_gl_all(void) {}
int xmodel_show_if_loaded(int modelnum, vms_vector *pos, vms_matrix *orient, int mpcolor, g3s_lrgb *light)
{
	(void)modelnum; (void)pos; (void)orient; (void)mpcolor; (void)light;
	return 0;	/* not loaded: the caller draws the original model */
}
