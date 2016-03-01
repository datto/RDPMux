/*
 * Copyright 2016 Datto Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef QEMU_RDP_DISPLAYBUFFER_H
#define QEMU_RDP_DISPLAYBUFFER_H

#include <cstdint>
#include <tuple>

#include <pixman.h>


#define ALIGN_SCREEN(size, align) ((size + align - 1) & (~(align - 1)))
#define PIXMAN_GET_R(f) ( (f & 0x00ff0000) >> 16 )
#define PIXMAN_GET_G(f) ( (f & 0x0000ff00) >> 8 )
#define PIXMAN_GET_B(f) ( (f & 0x000000ff) )

/**
 * @brief Base class to hold and manage framebuffers for RDPPeers.
 *
 * This class serves as the base class for the various DisplayBuffer types.
 */
class DisplayBuffer {
public:
    /**
     * @brief Creates a new DisplayBuffer class.
     *
     * @param x The width of the framebuffer.
     * @param y The height of the framebuffer.
     * @param shm Pointer to the shared memory region.
     */
    DisplayBuffer(uint32_t x, uint32_t y, void *shm);
    virtual ~DisplayBuffer() {};

    /**
     * @brief Fills a given buffer with properly-formatted pixel data from the framebuffer.
     *
     * @param x The x-coordinate of the top left corner of the region to fill.
     * @param y The y-coordinate of the top left corner of the region to fill.
     * @param w The width of the region to fill in px.
     * @param h The height of the region to fill in px.
     * @param dirty The buffer to fill. Should be allocated to be a proper size to hold the pixel data.
     */
    virtual void FillDirtyRegion(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t *dirty) =0;

    /**
     * @brief Gets the width of the tile region enclosing the framebuffer.
     *
     * @returns Tile width.
     */
    uint32_t GetTileRegionHeight();

    /**
     * @brief Gets the height of the tile region enclosing the framebuffer.
     *
     * @returns Tile height.
     */
    uint32_t GetTileRegionWidth();

    /**
     * @brief Sets the internal shared memory reference to the value passed in.
     *
     * @param region Pointer to the new shared memory region.
     */
    void SetShmRegion(void *region);

    /**
     * @brief Get the pixel format of the frame buffer.
     *
     * @returns Pixel format.
     */
    pixman_format_code_t GetDisplayBufferFormat();

    /**
     * @brief Get the scanline of a region given its with.
     *
     * @returns Scanline
     *
     * @param x Width of the region.
     */
    virtual int GetScanline(uint32_t x) =0;

protected:
    /**
     * @brief Width of the framebuffer in px.
     */
    uint32_t buf_width;
    /**
     * @brief Height of the framebuffer in px.
     */
    uint32_t buf_height;
    /**
     * @brief Width of the tile region enclosing the framebuffer.
     */
    uint16_t tile_w;
    /**
     * @brief Height of the tile region enclosing the framebuffer.
     */
    uint16_t tile_h;

    /**
     * @brief Pointer to the shared memory region containing the framebuffer.
     */
    void *shm_buffer_region;
    /**
     * @brief The subpixel layout of the framebuffer.
     */
    pixman_format_code_t format;
};

/**
 * @brief Struct modeling the subpixel layout of an RGBA framebuffer.
 */
struct rdp_pixel {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

#endif //QEMU_RDP_DISPLAYBUFFER_H
