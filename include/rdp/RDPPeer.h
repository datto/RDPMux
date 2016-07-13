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
     * @brief Creates a new RDPPeer object.
     *
     * @param client FreeRDP client struct for this particular peer connection.
     * @param listener Pointer to the parent RDPListener object.
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
     * @param displayWidth New display width.
     * @param displayHeight New display height.
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
     * @brief Gets the capture fps
     *
     * @returns The fps.
     */
    int GetCaptureFps();

    /**
     * @brief Returns a reference to the RDPListener associated with this peer.
     *
     * @returns RDPListener reference.
     */
    RDPListener *GetListener();

    /**
     * @brief sends a message to stop the client connection.
     */
    void CloseClient();

private:
    /**
     * @brief Pointer to the FreeRDP struct holding client state.
     */
    freerdp_peer *client;
    /**
     * @brief Pointer to the shared memory region of the backend guest.
     */
    void *shm_buffer_region;
    /**
     * @brief Pointer to the listener object associated with this peer.
     */
    RDPListener* listener;
    /**
     * @brief Width of the backend framebuffer.
     */
    size_t buf_width;
    /**
     * @brief height of the backend framebuffer.
     */
    size_t buf_height;
    /**
     * @brief Subpixel layout of the backend framebuffer.
     */
    PIXEL_FORMAT buf_format;

    /**
     * @brief Lock guarding access to the backend surface.
     */
    std::mutex surface_lock;
    /**
     * @brief Mutex guarding stop flag.
     */
    std::mutex stopMutex;
    /**
     * @brief Flag which stops main loop.
     */
    bool stop;

    /**
     * @brief Send region update to client using accelerated codec.
     *
     * Copies the specified region of the framebuffer into a temporary buffer,
     * encodes it using either the RemoteFX or NSC codec, as specified by
     * the RDP client settings struct, and finally sends the update to the client.
     *
     * @param nXSrc x-coordinate of the top-left corner of the framebuffer region.
     * @param nYSrc y-coordinate of the top-left corner of the framebuffer region.
     * @param nWidth width of the framebuffer region.
     * @param nHeight height of the framebuffer region.
     */
    int SendSurfaceBits(int nXSrc, int nYSrc, int nWidth, int nHeight);

    /**
     * @brief Send region update to client using bitmap codec.
     *
     * Copies the specified region of the framebuffer into a temporary buffer,
     * encodes it using the bitmap codec, and finally sends the update to the client.
     *
     * @param nXSrc x-coordinate of the top-left corner of the framebuffer region.
     * @param nYSrc y-coordinate of the top-left corner of the framebuffer region.
     * @param nWidth width of the framebuffer region.
     * @param nHeight height of the framebuffer region.
     */
    int SendBitmapUpdate(int nXSrc, int nYSrc, int nWidth, int nHeight);

    /**
     * @brief Wrapper function for sending region updates.
     *
     * This function acts as a wrapper for SendSurfaceBits() and SendBitmapUpdate(), performing codec-agnostic alignment
     * and preparation for codec-specific processing. It 16-aligns the coordinates from the InvalidRegion struct inside
     * the PeerContext and passes that to the encoding function.
     *
     * @returns status code of the encoding function indicating success.
     */
    int SendSurfaceUpdate();

    /**
     * @brief Function to convert Pixman format codes into RDPMux internal format codes.
     *
     * @param f Pixman format code.
     *
     * @returns RDPMux format code.
     */
    PIXEL_FORMAT GetPixelFormatForPixmanFormat(pixman_format_code_t f);

    /**
     * @brief Initializes a new backend surface and encoder using the parameters passed in.
     *
     * The RDPMux encoder needs to be aware of the backend framebuffer's subpixel layout, width and height. That information
     * is stored in the rdpMuxSurface struct within the PeerContext. When this function is called, it will re-initialize
     * both the encoder and the rdpMuxSurface struct to use the values passed in.
     *
     * @param width The new width of the backing framebuffer.
     * @param height The new height of the backing framebuffer.
     * @param r The new subpixel layout of the backing framebuffer.
     */
    void CreateSurface(int width, int height, PIXEL_FORMAT r);
};

/**
 * @brief Struct holding all context related to the freerdp_peer session.
 */
struct peer_context {
    /**
     * @brief internal rdpContext struct.
     *
     * Internally, the library will cast the pointer to this struct to (rdpContext *) before using it.
     */
    rdpContext _p;
    /**
     * @brief Pointer to the RDPPeer object associated with this PeerContext.
     */
    RDPPeer *peerObj;
    /**
     * @brief bpp of the backing framebuffer.
     */
    UINT32 sourceBpp;
    /**
     * @brief Subpixel layout of the backing framebuffer.
     */
    UINT32 sourceFormat;
    /**
     * @brief Format to encode updates in. Used by the encoder.
     */
    UINT32 encodeFormat;
    /**
     * @brief Current target framerate for the backend.
     */
    UINT32 frameRate;
    /**
     * @brief Minimum allowed backend framerate.
     */
    UINT32 minFrameRate;
    /**
     * @brief Maximum allowed backend framerate.
     */
    UINT32 maxFrameRate;
    /**
     * @brief Pointer to the encoder.
     */
    rdpMuxEncoder* encoder;
    /**
     * @brief Pointer to the surface.
     */
    rdpMuxSurface* surface;
    /**
     * @brief coordinates of the current dirty region to be encoded.
     */
    REGION16 invalidRegion;
    /**
     * @brief Lock guarding invalidRegion.
     */
    CRITICAL_SECTION lock;
    /**
     * @brief Flag for whether the peer is activated (ready to do work)
     */
    BOOL activated;
    /**
     * @brief Currently unused.
     */
    HANDLE event;
    /**
     * @brief Handle for stop events in the peer mainloop.
     */
    HANDLE stopEvent;
    /**
     * @brief Handle to the Virtual Channel Manager. Currently unused.
     */
    HANDLE vcm;
};
typedef struct peer_context PeerContext;
#endif //QEMU_RDP_RDPPEER_H
