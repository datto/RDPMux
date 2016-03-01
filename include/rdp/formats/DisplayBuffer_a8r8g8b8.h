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

#ifndef QEMU_RDP_DISPLAYBUFFER_A8R8G8B8_H
#define QEMU_RDP_DISPLAYBUFFER_A8R8G8B8_H

#include "rdp/DisplayBuffer.h"

#define ARGB_PIXMAN_GET_A(f) ( (f & 0xff000000) >> 24 )
#define ARGB_PIXMAN_GET_R(f) ( (f & 0x00ff0000) >> 16 )
#define ARGB_PIXMAN_GET_G(f) ( (f & 0x0000ff00) >> 8 )
#define ARGB_PIXMAN_GET_B(f) ( (f & 0x000000ff) )

class DisplayBuffer_a8r8g8b8 : public DisplayBuffer {

public:
    DisplayBuffer_a8r8g8b8(uint32_t x, uint32_t y, void *shm);
    ~DisplayBuffer_a8r8g8b8() {};
    void FillDirtyRegion(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t *dirty);
    int GetScanline(uint32_t x);
};
#endif //QEMU_RDP_DISPLAYBUFFER_A8R8G8B8_H
