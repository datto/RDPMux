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

#include "rdp/DisplayBuffer.h"

DisplayBuffer::DisplayBuffer(uint32_t x, uint32_t y, void *shm)
{
    buf_height = y;
    buf_width = x;

    tile_w = (buf_width % 64 != 0) ? (buf_width / 64 + 1) * 64 : buf_width;
    tile_h = (buf_height % 64 != 0) ? (buf_height / 64 + 1) * 64 : buf_height;

    shm_buffer_region = shm;
}

pixman_format_code_t DisplayBuffer::GetDisplayBufferFormat()
{
    return format;
}

uint32_t DisplayBuffer::GetTileRegionWidth()
{
    return tile_w;
}

uint32_t DisplayBuffer::GetTileRegionHeight()
{
    return tile_h;
}

void DisplayBuffer::SetShmRegion(void *region)
{
    this->shm_buffer_region = region;
}
