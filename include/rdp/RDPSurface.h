
#ifndef RDPMUX_SURFACE_H
#define RDPMUX_SURFACE_H

#include <winpr/crt.h>
#include <winpr/stream.h>

#include <freerdp/freerdp.h>
#include <freerdp/codecs.h>

/**
 * @brief Struct for a pixel region..
 */
struct rdpmux_surface
{
    /**
     * @brief X-coordinate of top left corner of surface.
     */
	int x;
    /**
     * @brief Y-coordinate of top left corner of surface.
     */
	int y;
    /**
     * @brief Width of surface.
     */
	int width;
    /**
     * @brief Height of surface.
     */
	int height;
    /**
     * @brief Scanline of surface.
     */
	int scanline;
    /**
     * @brief Size of the surface in bytes.
     */
	size_t size;
    /**
     * @brief Pointer to the data associated with the surface.
     */
	BYTE* data;
};
typedef struct rdpmux_surface rdpMuxSurface;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a new rdpmux surface struct using the given parameters.
 *
 * @param x X-coordinate of top left corner of surface.
 * @param y Y-coordinate of top left corner of surface.
 * @param width Width of surface.
 * @param height Height of surface.
 *
 * @returns Pointer to new struct
 */
rdpMuxSurface* rdpmux_surface_new(int x, int y, int width, int height);

/**
 * @brief Frees a rdpmux surface struct.
 *
 * @param surface The surface to free.
 */
void rdpmux_surface_free(rdpMuxSurface* surface);

#ifdef __cplusplus
}
#endif

#endif /* RDPMUX_SURFACE_H */
