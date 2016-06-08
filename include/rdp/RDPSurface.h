
#ifndef RDPMUX_SURFACE_H
#define RDPMUX_SURFACE_H

#include <winpr/crt.h>
#include <winpr/stream.h>

#include <freerdp/freerdp.h>
#include <freerdp/codecs.h>

struct rdpmux_surface
{
	int x;
	int y;
	int width;
	int height;
	int scanline;
	size_t size;
	BYTE* data;
};
typedef struct rdpmux_surface rdpMuxSurface;

#ifdef __cplusplus
extern "C" {
#endif

rdpMuxSurface* rdpmux_surface_new(int x, int y, int width, int height);
void rdpmux_surface_free(rdpMuxSurface* surface);

#ifdef __cplusplus
}
#endif

#endif /* RDPMUX_SURFACE_H */
