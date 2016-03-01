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

#include "rdp/formats/DisplayBuffer_r8g8b8.h"

DisplayBuffer_r8g8b8::DisplayBuffer_r8g8b8(uint32_t x, uint32_t y, void *shm) : DisplayBuffer(x, y, shm)
{

}

int DisplayBuffer_r8g8b8::GetScanline(uint32_t x)
{
    return ALIGN_SCREEN(x, 3) * 3;
}

void DisplayBuffer_r8g8b8::FillDirtyRegion(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t *dirty)
{
    uint8_t *src_ptr;
    uint8_t *shm_data;
    uint8_t *dest_ptr;
    size_t row, col;
    uint8_t *pixel;

    int skip = (w - buf_width) * 3;

    // copies the buffer out of the shmem TODO: make better docstrings
    // TODO: if you ever get non-strip-shaped regions, you will need to change
    // the buf_width parameter in this function to be w
    shm_data = (uint8_t *) this->shm_buffer_region;
    dest_ptr = dirty;
    for (row = 0; row < h; row++) {
        if ((y + row) >= buf_height) {
            break;
        }

        src_ptr = shm_data + ((buf_width * (y + row)) + x) * 3;

        for (col = 0; col < w; col++) {
            pixel = src_ptr + (col * 3);
            *dest_ptr++ = (*pixel++);
            *dest_ptr++ = (*pixel++);
            *dest_ptr++ = (*pixel++);
        }
        dest_ptr += skip;
    }
}