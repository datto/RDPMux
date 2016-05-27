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

#include <new>
#include <iostream>
#include <tuple>
#include <sstream>
#include <cstring>
#include <string>

#include <boost/program_options.hpp>

#include <msgpack.hpp>
#include <rdp/DisplayBuffer.h>
#include <malloc.h>

#include "rdp/formats/DisplayBuffer_r8g8b8a8.h"
#include "rdp/formats/DisplayBuffer_r8g8b8.h"
#include "rdp/formats/DisplayBuffer_a8r8g8b8.h"

#include "rdp/RDPPeer.h"
#include "util/logging.h"
#include "common.h"

#define RDP_SERVER_DEFAULT_HEIGHT 768
#define RDP_SERVER_DEFAULT_WIDTH 1024

namespace po = boost::program_options;
extern po::variables_map vm;

/* Context activation functions */
static BOOL peer_context_new(freerdp_peer *client, PeerContext *context)
{
    // creates the initial context object. Things which are typically hardcoded configuration variables are initialized
    // here.
    if (!(context->rfx_context = rfx_context_new(TRUE))) {
        goto fail_rfx_context;
    }

    context->rfx_context->mode = RLGR3;
    context->rfx_context->width = RDP_SERVER_DEFAULT_WIDTH;
    context->rfx_context->height = RDP_SERVER_DEFAULT_HEIGHT;
    rfx_context_set_pixel_format(context->rfx_context, RDP_PIXEL_FORMAT_R8G8B8A8);

    if (!(context->nsc_context = nsc_context_new())) {
        goto fail_nsc_context;
    }
    nsc_context_set_pixel_format(context->nsc_context, RDP_PIXEL_FORMAT_R8G8B8A8);

    if (!(context->s = Stream_New(NULL, 65536))) {
        goto fail_stream_new;
    }

    context->icon_x = -1;
    context->icon_y = -1;

    context->vcm = WTSOpenServerA((LPSTR) client->context);

    if (!context->vcm || context->vcm == INVALID_HANDLE_VALUE) {
        goto fail_open_server;
    }
    return TRUE;

fail_open_server:
    context->vcm = NULL;
    Stream_Free(context->s, TRUE);
    context->s = NULL;
fail_stream_new:
    nsc_context_free(context->nsc_context);
    context->nsc_context = NULL;
fail_nsc_context:
    rfx_context_free(context->rfx_context);
    context->rfx_context = NULL;
fail_rfx_context:
    return TRUE;
}

static void peer_context_free(freerdp_peer *client, PeerContext *context)
{
    if (context) {
        if (context->debug_channel_thread) {
            SetEvent(context->stopEvent);
            WaitForSingleObject(context->debug_channel_thread, INFINITE);
            CloseHandle(context->debug_channel_thread);
        }

        Stream_Free(context->s, TRUE);
        free(context->icon_data);
        free(context->bg_data);

        if (context->debug_channel) {
            WTSVirtualChannelClose(context->debug_channel);
        }
        if (context->audin) {
            audin_server_context_free(context->audin);
        }

        if (context->rdpsound) {
            rdpsnd_server_context_free(context->rdpsound);
        }

        WTSCloseServer((HANDLE) context->vcm);
    }
}

static BOOL peer_post_connect(freerdp_peer *client)
{
    PeerContext *context = (PeerContext*) client->context;

    context->rfx_context->width = client->settings->DesktopWidth;
    context->rfx_context->height = client->settings->DesktopHeight;

    client->update->DesktopResize(client->update->context);

    if (WTSVirtualChannelManagerIsChannelJoined(context->vcm, "rdpsnd")) {
        VLOG(1) << "PEER: Sound stuff is still a work in progress.";
    }
    return TRUE;
}

static BOOL peer_activate(freerdp_peer *client)
{
    PeerContext *context = (PeerContext*) client->context;

    auto DesktopWidth = context->peerObj->GetSurfaceWidth();
    auto DesktopHeight = context->peerObj->GetSurfaceHeight();

    rfx_context_reset(context->rfx_context);
    context->activated = TRUE;

    client->settings->CompressionLevel = PACKET_COMPR_TYPE_RDP61;

    VLOG(3) << std::dec << "PEER: client->settings->Desktop{Width,Height}: " << DesktopWidth << " " << DesktopHeight;
    context->peerObj->FullDisplayUpdate(context->peerObj->GetListener()->GetFormat());
    return TRUE;
}

/* Peer callback handlers */

static BOOL peer_keyboard_event(rdpInput *input, uint16_t flags, uint16_t code)
{
    PeerContext *context = (PeerContext *) input->context->peer->context;

    RDPPeer *peer = context->peerObj;
    peer->ProcessKeyboardMsg(flags, code);
    return TRUE;
}

static BOOL peer_mouse_event(rdpInput *input, uint16_t flags, uint16_t x, uint16_t y)
{
    PeerContext *context = (PeerContext *) input->context->peer->context;

    RDPPeer *peer = context->peerObj;
    peer->ProcessMouseMsg(flags, x, y);

    return TRUE;
}

static BOOL peer_synchronize_event(rdpInput *input, uint32_t flags)
{
    VLOG(2) << "PEER: Client sent a synchronize event(0x" << std::hex << flags << ")" << std::dec;
    return TRUE;
}

static BOOL peer_refresh_rect(rdpContext *context, uint8_t count, RECTANGLE_16 *areas)
{
    PeerContext *ctx = (PeerContext *) context;
    for (size_t i = 0; i < count; i++) {
        VLOG(2) << "PEER: Client requested to refresh [(" << areas[i].left << ", " << areas[i].top << "), " << areas[i].right << ", " << areas[i].bottom << "]";
        uint16_t width = areas[i].right - areas[i].left;
        uint16_t height = areas[i].bottom - areas[i].top;

        if (width > ctx->peerObj->GetSurfaceWidth() || height > ctx->peerObj->GetSurfaceHeight())
            return TRUE;

        ctx->peerObj->PartialDisplayUpdate(areas[i].left, areas[i].top, areas[i].right - areas[i].left, areas[i].bottom - areas[i].top);
    }
    return TRUE;
}

static BOOL peer_suppress_output(rdpContext *context, uint8_t allow, RECTANGLE_16 *areas)
{
    if (allow > 0) {
        VLOG(2) << "PEER: Client requested to restore output";
    } else {
        VLOG(2) << "PEER: Client requsted to suppress output";
    }
    return TRUE;
}

/* Region update helpers */

static void begin_new_frame(rdpUpdate *update, PeerContext *context)
{
    SURFACE_FRAME_MARKER *fm = &update->surface_frame_marker;
    fm->frameAction = SURFACECMD_FRAMEACTION_BEGIN;
    fm->frameId = context->frame_id;
    update->SurfaceFrameMarker(update->context, fm);
}

static void end_frame(freerdp_peer *client)
{
    rdpUpdate *update = client->update;
    SURFACE_FRAME_MARKER *fm = &update->surface_frame_marker;
    PeerContext *context = (PeerContext *) client->context;

    fm->frameAction = SURFACECMD_FRAMEACTION_END;
    fm->frameId = context->frame_id;
    update->SurfaceFrameMarker(update->context, fm);
    context->frame_id++;
}

static wStream *wstream_init(PeerContext *context)
{
    Stream_Clear(context->s);
    Stream_SetPosition(context->s, 0);
    return context->s;
}

/* Class methods */

size_t RDPPeer::GetSurfaceWidth()
{
    return listener->GetWidth();
}

size_t RDPPeer::GetSurfaceHeight()
{
    return listener->GetHeight();
}

RDPListener *RDPPeer::GetListener()
{
    return listener;
}

RDPPeer::RDPPeer(std::tuple<freerdp_peer*, nn::socket*, RDPListener *> tuple)
{
    freerdp_peer *client = std::get<0>(tuple);
    RDPListener *listener = std::get<2>(tuple);

    this->tuple = tuple;
    this->client = client;
    this->listener = listener;
    this->surface = nullptr;
    this->shm_buffer_region = listener->shm_buffer;


    client->ContextSize = sizeof(PeerContext);
    client->ContextNew = (psPeerContextNew) peer_context_new;
    client->ContextFree = (psPeerContextFree) peer_context_free;

    if (!freerdp_peer_context_new(client)) {
        freerdp_peer_free(client);
        LOG(WARNING) << "Could not allocate peer context!";
        throw std::bad_alloc();
    }

    // dynamic context initialization functions.
    PeerContext *context = (PeerContext *) client->context;
    context->peerObj = this;

    client->PostConnect = peer_post_connect;
    client->Activate = peer_activate;
    client->input->SynchronizeEvent = peer_synchronize_event;
    client->input->KeyboardEvent = peer_keyboard_event;
    client->input->MouseEvent = peer_mouse_event;

    client->update->RefreshRect = peer_refresh_rect;
    client->update->SuppressOutput = peer_suppress_output;

    std::string cert_dir = vm["certificate-dir"].as<std::string>();
    if (cert_dir.compare("") == 0) {
        LOG(FATAL) << "Certificate dir option was not passed properly, aborting";
        exit(12);
    }

    std::string key_path = cert_dir + "/server.key";
    std::string crt_path = cert_dir + "/server.crt";
    VLOG(3) << "key path is " << key_path;
    VLOG(3) << "crt path is " << crt_path;
    // TODO: set these dynamically, probably via command line options or a config file or something
    client->settings->CertificateFile = _strdup(crt_path.c_str());
    client->settings->PrivateKeyFile = _strdup(key_path.c_str());
    client->settings->RdpKeyFile = _strdup(key_path.c_str());

    client->settings->RdpSecurity = TRUE;
    client->settings->TlsSecurity = TRUE;
    client->settings->NlaSecurity = FALSE;
    client->settings->EncryptionLevel = ENCRYPTION_LEVEL_CLIENT_COMPATIBLE;
    client->settings->RemoteFxCodec = TRUE;
    client->settings->ColorDepth = 32;
    client->settings->SuppressOutput = TRUE;
    client->settings->RefreshRect = TRUE;

    client->settings->MultifragMaxRequestSize = 0xFFFFFFFF;
}

RDPPeer::~RDPPeer()
{
    freerdp_peer_context_free(client);
    freerdp_peer_free(client);
    LOG(INFO) << "PEER " << this << ": DESTRUCTING ACHTUNG WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING";
}

void RDPPeer::RunThread(freerdp_peer *client)
{
    HANDLE handles[32];
    DWORD count, status;
    PeerContext *context = (PeerContext *) client->context;

    this->listener->registerPeer(this);

    while (1) {
        count = 0;
        handles[count++] = client->GetEventHandle(client);
        handles[count++] = WTSVirtualChannelManagerGetEventHandle(context->vcm);

        status = WaitForMultipleObjects(count, handles, FALSE, INFINITE);

        if (status == WAIT_FAILED) {
            VLOG(1) << "PEER: Wait failed.";
            break;
        }

        if (client->CheckFileDescriptor(client) != TRUE) {
            VLOG(1) << "PEER: Client closed connection.";
            break;
        }

        if (WTSVirtualChannelManagerCheckFileDescriptor(context->vcm) != TRUE) {
            VLOG(1) << "PEER: Virtual channel connection closed.";
            break;
        }
    }

    this->listener->unregisterPeer(this);
}

void *RDPPeer::PeerThread(void *arg)
{
    auto tuple = (std::tuple<freerdp_peer*, nn::socket*, RDPListener *> *) arg;

    auto tuple_obj = *tuple;

    freerdp_peer *client = std::get<0>(tuple_obj);

    std::unique_ptr<RDPPeer> peer(new RDPPeer(*tuple));

    client->Initialize(client);

    peer->RunThread(client);

    client->Disconnect(client);
    VLOG(1) << "PEER: Client disconnected.";

    return NULL;
}

void RDPPeer::ProcessMouseMsg(uint16_t flags, uint16_t x, uint16_t y)
{
    std::vector<uint16_t> vec;
    vec.push_back(MOUSE);
    vec.push_back(x);
    vec.push_back(y);
    vec.push_back(flags);

    msgpack::sbuffer sbuf;
    msgpack::pack(sbuf, vec);

    nn::socket *sock = std::get<1>(tuple);
    VLOG(2) << "PEER: Now sending RDP mouse client message to the QEMU VM";
    sock->send(sbuf.data(), sbuf.size(), 0);
}

void RDPPeer::ProcessKeyboardMsg(uint16_t flags, uint16_t keycode)
{
    std::vector<uint16_t> vec;
    vec.push_back(KEYBOARD);
    vec.push_back(keycode);
    vec.push_back(flags);

    msgpack::sbuffer sbuf;
    msgpack::pack(sbuf, vec);

    nn::socket *sock = std::get<1>(tuple);
    VLOG(2) << "PEER: Now sending RDP keyboard client message to the QEMU VM";
    sock->send(sbuf.data(), sbuf.size(), 0);
}

void RDPPeer::PartialDisplayUpdate(uint32_t x_coord, uint32_t y_coord, uint32_t width, uint32_t height)
{
    UpdateRegion(x_coord, y_coord, width, height, FALSE);
}

PIXEL_FORMAT RDPPeer::GetPixelFormatForPixmanFormat(pixman_format_code_t f)
{
    switch(f) {
        case PIXMAN_r8g8b8a8:
        case PIXMAN_r8g8b8x8:
            return PIXEL_FORMAT_r8g8b8a8;
        case PIXMAN_a8r8g8b8:
        case PIXMAN_x8r8g8b8:
            return PIXEL_FORMAT_r8g8b8a8;
        case PIXMAN_r8g8b8:
            return PIXEL_FORMAT_r8g8b8;
        case PIXMAN_b8g8r8:
            return PIXEL_FORMAT_b8g8r8;
        default:
            throw new std::invalid_argument("Pixel format not supported");
    }
}

void RDPPeer::CreateSurface(PIXEL_FORMAT r)
{
    PeerContext *context = (PeerContext *) client->context;

    switch(r) {
        case PIXEL_FORMAT_r8g8b8a8:
            VLOG(2) << "PEER: Launching R8G8B8A8 Displaybuffer with dimensions " << buf_width << "x" << buf_height;
            surface = new DisplayBuffer_r8g8b8a8(buf_width, buf_height, shm_buffer_region);
            rfx_context_set_pixel_format(context->rfx_context, RDP_PIXEL_FORMAT_R8G8B8A8);
            break;
        case PIXEL_FORMAT_r8g8b8:
            VLOG(2) << "PEER: Launching R8G8B8 Displaybuffer with dimensions " << buf_width << "x" << buf_height;
            surface = new DisplayBuffer_r8g8b8(buf_width, buf_height, shm_buffer_region);
            // TODO: Ubuntu is a goddamn liar and I don't know what else to do about it.
            rfx_context_set_pixel_format(context->rfx_context, RDP_PIXEL_FORMAT_B8G8R8);
            break;
        case PIXEL_FORMAT_b8g8r8:
            VLOG(2) << "PEER: Launching R8G8B8 Displaybuffer to deal with BGR data with dimensions " << buf_width << "x" << buf_height;
            surface = new DisplayBuffer_r8g8b8(buf_width, buf_height, shm_buffer_region);
            rfx_context_set_pixel_format(context->rfx_context, RDP_PIXEL_FORMAT_B8G8R8);
            break;
        case PIXEL_FORMAT_a8r8g8b8:
            VLOG(2) << "PEER: Launching A8R8G8B8 Displaybuffer with dimensions " << buf_width << "x" << buf_height;
            surface = new DisplayBuffer_a8r8g8b8(buf_width, buf_height, shm_buffer_region);
            rfx_context_set_pixel_format(context->rfx_context, RDP_PIXEL_FORMAT_R8G8B8A8);
        default:
            LOG(ERROR) << "UNKNOWN PIXEL FORMAT ERROR ERROR THIS SHOULD NEVER HAPPEN ERROR";
            break;
    }
}

void RDPPeer::FullDisplayUpdate(pixman_format_code_t f)
{
    PIXEL_FORMAT r;
    buf_width = listener->GetWidth();
    buf_height = listener->GetHeight();

    try {
        r = GetPixelFormatForPixmanFormat(f);
        if (r < 1) {
            LOG(WARNING) << "What is the point of exceptions if they never get caught?";
        }
    } catch (std::invalid_argument &e) {
        // TODO: notify the outside somehow that we have stuff to implement
        VLOG(3) << "PEER: Unknown pixel format received.";
        return;
    }

    VLOG(3) << "PEER " << this << ": Attempting to take lock on surface to recreate";
    {
        std::lock_guard<std::mutex> lock(surface_lock);
        VLOG(3) << "PEER " << this << ": Locked surface to recreate";
        delete surface;
        CreateSurface(r);
    }
    VLOG(3) << "PEER " << this << ": Recreated display surface object; lock released";

    UpdateRegion(0, 0, buf_width, buf_height, TRUE);
}

void RDPPeer::UpdateRegion(uint32_t x, uint32_t y, uint32_t w, uint32_t h, BOOL fullRefresh)
{
    rdpUpdate *update = client->update;
    SURFACE_BITS_COMMAND *cmd = &update->surface_bits_command;
    PeerContext *context = (PeerContext *) client->context;
    wStream *s = nullptr;
    uint8_t *dirty;

    if (!context->activated) {
        VLOG(2) << "PEER: Context isn't activated, skipping";
        return;
    }
    if (surface == nullptr || surface->isDestructed()) {
        VLOG(3) << "PEER: Surface has been destructed, skipping update";
        return;
    }

    VLOG(2) << std::dec << "PEER: Now updating region [(" << (int) x << ", " << (int) y << ") " << (int) w << "x" << (int) h << "], " << (fullRefresh ? "FULL REFRESH" : "PARTIAL REFRESH");

    // create update metadata struct
    cmd->bpp = 32;
    cmd->destLeft = x;
    cmd->destTop = y;
    cmd->destBottom = y+h;
    cmd->destRight = x+w;
    cmd->width = w;
    cmd->height = h;

    dirty = (uint8_t *) calloc(w * h, sizeof(uint32_t));
    if (!dirty) {
        std::cerr << "WAAAAAAAA D:" << std::endl;
        return;
    }

    //VLOG(2) << std::dec << "PEER: calloc() gave us " << malloc_usable_size(dirty) << " bytes";

    // begin the RDP frame
    begin_new_frame(update, context);
    s = wstream_init(context);

    if (client->settings->RemoteFxCodec) {
        RFX_RECT rect;

        rect.x = 0;
        rect.y = 0;
        rect.width = w;
        rect.height = h;

        VLOG(3) << "PEER " << this << ": Attempting to take lock on server surface now for update [(" << (int) x << ", " << (int) y << ") " << (int) w << "x" << (int) h << "];";
        {
            std::lock_guard<std::mutex> lock(surface_lock);
            VLOG(3) << "PEER " << this << ": Lock on server surface taken for update [(" << (int) x << ", " << (int) y << ") " << (int) w << "x" << (int) h << "];";
            surface->FillDirtyRegion(x, y, w, h, dirty);

            int scanline = surface->GetScanline(w);

            rfx_compose_message(context->rfx_context, s, &rect, 1, dirty, rect.width, rect.height, scanline);

            cmd->bitmapDataLength = Stream_GetPosition(s);
            cmd->bitmapData = Stream_Buffer(s);
            cmd->codecID = client->settings->RemoteFxCodecId;
            cmd->skipCompression = TRUE;

            update->SurfaceBits(update->context, cmd);
        }
        VLOG(3) << "PEER " << this << ": Server surface lock released for update [(" << (int) x << ", " << (int) y << ") " << (int) w << "x" << (int) h << "];";
    }

    end_frame(client);
    free(dirty);
    dirty = NULL;
}
