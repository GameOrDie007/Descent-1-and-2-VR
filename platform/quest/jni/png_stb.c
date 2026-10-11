/* Descent 1 & 2 VR, Android: read_png for the splash, through stb_image
 * (public domain, Sean Barrett) instead of libpng, from the game's folders
 * (PhysFS) as on PC.  The app's own splash is shown by the picker before the
 * game starts, so the game finds none unless the player puts one there.  The
 * result is 8-bit RGB or RGBA, which is all the splash takes.  Written against pngfile.h. */
#include <stdlib.h>
#include <string.h>
#include <physfs.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#include "pngfile.h"

static unsigned char *read_whole(const char *filename, int *len)
{
	unsigned char *buf = NULL;
	PHYSFS_File *fp = PHYSFS_openRead(filename);
	if (fp) {
		const PHYSFS_sint64 n = PHYSFS_fileLength(fp);
		if (n > 0 && (buf = malloc((size_t)n)) != NULL &&
			PHYSFS_readBytes(fp, buf, (PHYSFS_uint64)n) != n) {
			free(buf);
			buf = NULL;
		}
		PHYSFS_close(fp);
		*len = (int)n;
	}
	return buf;
}

int read_png(const char *filename, png_data *pdata)
{
	int len = 0, w, h, comp;
	unsigned char *file, *pix;
	if (!pdata)
		return 0;
	memset(pdata, 0, sizeof(*pdata));
	if (!filename || !(file = read_whole(filename, &len)))
		return 0;
	if (!stbi_info_from_memory(file, len, &w, &h, &comp)) {
		free(file);
		return 0;
	}
	/* Gray and gray+alpha come out as RGB and RGBA. */
	comp = (comp == 2 || comp == 4) ? 4 : 3;
	pix = stbi_load_from_memory(file, len, &w, &h, NULL, comp);
	free(file);
	if (!pix)
		return 0;
	pdata->width = (unsigned int)w;
	pdata->height = (unsigned int)h;
	pdata->depth = 8;
	pdata->channels = (unsigned int)comp;
	pdata->color = 1;
	pdata->alpha = comp == 4;
	pdata->data = pix;	/* stb allocates with malloc, so the callers' free() fits */
	return 1;
}

int write_png(const char *filename, png_data *pdata)
{
	(void)filename;
	(void)pdata;
	return 0;	/* screenshots go through the engine's own TGA path */
}
