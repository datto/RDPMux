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

#include <stdint.h>
#include "rdp/formats/DisplayBuffer_r8g8b8a8.h"

DisplayBuffer_r8g8b8a8::DisplayBuffer_r8g8b8a8(uint32_t x, uint32_t y, void *shm) : DisplayBuffer(x, y, shm)
{

}

int DisplayBuffer_r8g8b8a8::GetScanline(uint32_t x)
{
    return ALIGN_SCREEN(x, 4) * 4;
}

void DisplayBuffer_r8g8b8a8::FillDirtyRegion(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t *dirty)
{
    uint32_t *src_ptr;
    uint32_t *shm_data;
    uint8_t *dest_ptr;
    size_t row, col;
    struct rdp_pixel *rdp;
    uint32_t pixel;

    int skip = (w - buf_width) * 4;

    // copies the buffer out of the shmem TODO: make better docstrings
    shm_data = (uint32_t *) this->shm_buffer_region;
    dest_ptr = dirty;
    for (row = 0; row < h; row++) {
        if ((y + row) >= buf_height) {
            break;
        }

        src_ptr = shm_data + (buf_width * (y + row)) + x;

        for (col = 0; col < buf_width; col++) {
            rdp = (struct rdp_pixel *) dest_ptr;
            pixel = *(src_ptr + col);
            rdp->r = PIXMAN_GET_R(pixel);
            rdp->g = PIXMAN_GET_G(pixel);
            rdp->b = PIXMAN_GET_B(pixel);
            dest_ptr += 4;
        }
        dest_ptr += skip;
    }
}
