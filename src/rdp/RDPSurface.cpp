
#include "rdp/RDPSurface.h"

#define ALIGN_SIZE(size, align) ((size + align - 1) & (~(align - 1)))

rdpMuxSurface* rdpmux_surface_new(int x, int y, int width, int height)
{
	rdpMuxSurface* surface;

	surface = (rdpMuxSurface*) calloc(1, sizeof(rdpMuxSurface));

	if (!surface)
		return NULL;

	surface->x = x;
	surface->y = y;
	surface->width = width;
	surface->height = height;
	surface->scanline = ALIGN_SIZE(surface->width, 16) * 4;

	surface->size = surface->scanline * ALIGN_SIZE(surface->height, 4);
	surface->data = (BYTE*) _aligned_malloc(surface->size, 16);

	if (!surface->data)
	{
		free(surface);
		return NULL;
	}

	ZeroMemory(surface->data, surface->size);

	return surface;
}

void rdpmux_surface_free(rdpMuxSurface* surface)
{
	if (!surface)
		return;

	if (surface->data)
	{
		_aligned_free(surface->data);
		surface->data = NULL;
	}

	free(surface);
}

