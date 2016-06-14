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

#ifndef QEMU_RDP_RDPPEER_H
#define QEMU_RDP_RDPPEER_H

#include "common.h"
#include "RDPSurface.h"
#include "RDPEncoder.h"
#include <freerdp/freerdp.h>
#include <atomic>
#include <pixman.h>

#include <freerdp/codec/region.h>
#include <freerdp/server/rdpsnd.h>
#include <freerdp/server/audin.h>

/**
 * @brief Recognized RDPMux pixel format archetypes.
 *
 * These are the overarching general types of pixel format that RDPMux supports. Each archetype has its own
 * format transcoder, under rdp/formats.
 */
enum pixel_formats {
    PIXEL_FORMAT_r8g8b8a8,
    PIXEL_FORMAT_a8r8g8b8,
    PIXEL_FORMAT_r8g8b8,
    PIXEL_FORMAT_b8g8r8,
    PIXEL_FORMAT_INVALID /* always last! */
};
typedef enum pixel_formats PIXEL_FORMAT;

class RDPListener; // I'm very bad at organizing C++ code

/**
 * @brief Manages the connection to a single RDP client.
 *
 * The RDPPeer class wraps a freerdp_peer struct, and manages the lifecycle of that struct, providing an OOP interface
 * to manage freerdp_peer functionality from higher up in the stack.
 */
class RDPPeer
{
public:
    /**
     * @brief Creates a new RDPPeer class.
     */
    RDPPeer(freerdp_peer *client, RDPListener *listener);

    /**
     * @brief Takes care of freeing the underlying peer_context and freerdp_peer structs.
     */
    ~RDPPeer();

    /**
     * @brief Factory method that creates and starts a RDPPeer.
     *
     * Meant to be called as the runnable of a thread.
     *
     * @param arg Obfuscated pointer to the argument. Should be a pointer to an object of type
     * std::tuple<freerdp_peer*, nn::socket*, RDPListener *>.
     */
    static void *PeerThread(void *arg);

    /**
     * @brief Main listen loop of the RDPPeer.
     */
    void RunThread(freerdp_peer *client);

    /**
     * @brief Creates and sends a msgpack-encoded mouse message to the VM.
     *
     * @param flags Any flags associated with the mouse event.
     * @param x The X-coordinate of the mouse cursor.
     * @param y The Y-coordinate of the mouse cursor.
     */
    void ProcessMouseMsg(uint16_t flags, uint16_t x, uint16_t y);

    /**
     * @brief Creates and sends a msgpack-encoded keyboard message to the VM.
     *
     * @param flags Any flags associated with the keyboard event.
     * @param keycode The keycode of the keyboard event.
     */
    void ProcessKeyboardMsg(uint16_t flags, uint16_t keycode);

    /**
     * @brief Encodes and sends a partial display buffer update to the RDP client.
     *
     * @param x_coord The X-coordinate of the top left corner of the updated region.
     * @param y_coord The Y-coordinate of the top left corner of the updated region.
     * @param width The width of the updated region in px.
     * @param height The height of the updated region in px.
     */
    void PartialDisplayUpdate(uint32_t x_coord, uint32_t y_coord, uint32_t width, uint32_t height);

    /**
     * @brief Recreates the surface object associated with this framebuffer and sends a full display
     * update to the client.
     *
     * @param f The display buffer pixel format.
     */
    void FullDisplayUpdate(uint32_t displayWidth, uint32_t displayHeight, pixman_format_code_t f);

    /**
     * @brief Gets the width of the surface.
     *
     * @returns The width.
     */
    size_t GetSurfaceWidth();

    /**
     * @brief Gets the height of the surface.
     *
     * @returns The height.
     */
    size_t GetSurfaceHeight();

    /**
     * @brief Returns a reference to the RDPListener associated with this peer.
     *
     * @returns RDPListener reference.
     */
    RDPListener *GetListener();

private:
    freerdp_peer *client;
    void *shm_buffer_region;
    RDPListener* listener;
    size_t buf_width;
    size_t buf_height;
    PIXEL_FORMAT buf_format;

    std::mutex surface_lock;

    int SendSurfaceBits(int nXSrc, int nYSrc, int nWidth, int nHeight);
    int SendBitmapUpdate(int nXSrc, int nYSrc, int nWidth, int nHeight);
    int SendSurfaceUpdate(int x, int y, int width, int height);

    PIXEL_FORMAT GetPixelFormatForPixmanFormat(pixman_format_code_t f);
    void CreateSurface(int width, int height, PIXEL_FORMAT r);
};

/**
 * @brief Struct holding all context related to the freerdp_peer session. Most of this is currently unused,
 * but is here for future features.
 */
struct peer_context {
    rdpContext _p;
    RDPPeer *peerObj;

    UINT32 sourceBpp;
    UINT32 sourceFormat;
    UINT32 encodeFormat;
    UINT32 frameRate;
    UINT32 minFrameRate;
    UINT32 maxFrameRate;
    rdpMuxEncoder* encoder;
    rdpMuxSurface* surface;
    REGION16 invalidRegion;
    CRITICAL_SECTION lock;
    BOOL activated;
    HANDLE event;
    HANDLE stopEvent;
    HANDLE vcm;
};
typedef struct peer_context PeerContext;
#endif //QEMU_RDP_RDPPEER_H
