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

#include <boost/program_options.hpp>

#include "rdp/RDPPeer.h"
#include "rdp/RDPListener.h"

#include <winpr/crt.h>
#include <winpr/sysinfo.h>

namespace po = boost::program_options;
extern po::variables_map vm;

/* Context activation functions */
static BOOL peer_context_new(freerdp_peer* client, PeerContext* ctx)
{
	rdpSettings* settings = client->settings;

	settings->ColorDepth = 32;
	settings->NSCodec = FALSE;
	settings->RemoteFxCodec = TRUE;
	settings->BitmapCacheV3Enabled = TRUE;
	settings->SupportGraphicsPipeline = FALSE;

	settings->FrameMarkerCommandEnabled = TRUE;
	settings->SurfaceFrameMarkerEnabled = TRUE;

	settings->DrawAllowSkipAlpha = TRUE;
	settings->DrawAllowColorSubsampling = TRUE;
	settings->DrawAllowDynamicColorFidelity = TRUE;

	settings->CompressionLevel = PACKET_COMPR_TYPE_RDP61;

	settings->SuppressOutput = TRUE;
	settings->RefreshRect = TRUE;

	settings->RdpSecurity = TRUE;
	settings->TlsSecurity = TRUE;
	settings->NlaSecurity = FALSE;

	settings->EncryptionLevel = ENCRYPTION_LEVEL_CLIENT_COMPATIBLE;

	ctx->vcm = WTSOpenServerA((LPSTR) client->context);

	if (!ctx->vcm || ctx->vcm == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	ctx->encoder = rdpmux_encoder_new(settings);

	if (!ctx->encoder)
		return FALSE;

	region16_init(&ctx->invalidRegion);

	InitializeCriticalSectionAndSpinCount(&(ctx->lock), 4000);

	ctx->stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	ctx->minFrameRate = 1;
	ctx->maxFrameRate = 30;
	ctx->frameRate = ctx->maxFrameRate;

	return TRUE;
}

static void peer_context_free(freerdp_peer* client, PeerContext* ctx)
{
	if (!ctx)
		return;

	rdpmux_encoder_free(ctx->encoder);

	region16_uninit(&ctx->invalidRegion);

	WTSCloseServer((HANDLE) ctx->vcm);

	if (ctx->surface)
	{
		rdpmux_surface_free(ctx->surface);
		ctx->surface = NULL;
	}

	DeleteCriticalSection(&(ctx->lock));

	CloseHandle(ctx->stopEvent);
}

static BOOL peer_capabilities(freerdp_peer *client)
{
	return TRUE;
}

static BOOL peer_post_connect(freerdp_peer *client)
{
	UINT32 ColorDepth;
	UINT32 DesktopWidth;
	UINT32 DesktopHeight;
	rdpSettings* settings = client->settings;
	PeerContext* ctx = (PeerContext*) client->context;
	RDPListener* listener = ctx->peerObj->GetListener();

	DesktopWidth = listener->GetWidth();
	DesktopHeight = listener->GetHeight();
	ColorDepth = 32;

	if (settings->ColorDepth == 24)
		settings->ColorDepth = 16; /* disable 24bpp */

	if (settings->MultifragMaxRequestSize < 0x3F0000)
		settings->NSCodec = FALSE; /* NSCodec compressor does not support fragmentation yet */

	fprintf(stderr, "Client requested desktop: %dx%dx%d\n",
			settings->DesktopWidth, settings->DesktopHeight, settings->ColorDepth);

	if ((DesktopWidth != settings->DesktopWidth) || (DesktopHeight != settings->DesktopHeight)
			|| (ColorDepth != settings->ColorDepth))
	{
		fprintf(stderr, "Resizing desktop to %dx%dx%d\n", DesktopWidth, DesktopHeight, ColorDepth);

		settings->DesktopWidth = DesktopWidth;
		settings->DesktopHeight = DesktopHeight;
		settings->ColorDepth = ColorDepth;

		client->update->DesktopResize(client->update->context);
	}

	ctx->encoder->frameAck = settings->SurfaceFrameMarkerEnabled;

	return TRUE;
}

static BOOL peer_activate(freerdp_peer *client)
{
	PeerContext* ctx = (PeerContext*) client->context;
	rdpMuxEncoder* encoder = ctx->encoder;
	rdpSettings* settings = client->settings;
	RDPListener* listener = ctx->peerObj->GetListener();

	fprintf(stderr, "PeerActivate\n");

	if (settings->ClientDir && (strcmp(settings->ClientDir, "librdp") == 0))
	{
		/* Hack for Mac/iOS/Android Microsoft RDP clients */

		settings->RemoteFxCodec = FALSE;

		settings->NSCodec = FALSE;
		settings->NSCodecAllowSubsampling = FALSE;

		settings->SurfaceFrameMarkerEnabled = FALSE;
	}

	auto DesktopWidth = listener->GetWidth();
	auto DesktopHeight = listener->GetHeight();

	ctx->activated = TRUE;

	VLOG(3) << std::dec << "PEER: client->settings->Desktop{Width,Height}: " << DesktopWidth << " " << DesktopHeight;

	ctx->peerObj->FullDisplayUpdate(DesktopWidth, DesktopHeight, ctx->peerObj->GetListener()->GetFormat());

	return TRUE;
}

/* Peer callback handlers */

static BOOL peer_keyboard_event(rdpInput *input, uint16_t flags, uint16_t code)
{
	PeerContext* context = (PeerContext*) input->context->peer->context;

	RDPPeer* peer = context->peerObj;
	peer->ProcessKeyboardMsg(flags, code);

	return TRUE;
}

static BOOL peer_mouse_event(rdpInput *input, uint16_t flags, uint16_t x, uint16_t y)
{
	PeerContext* context = (PeerContext *) input->context->peer->context;

	RDPPeer* peer = context->peerObj;
	peer->ProcessMouseMsg(flags, x, y);

	return TRUE;
}

static BOOL peer_synchronize_event(rdpInput* input, uint32_t flags)
{
	return TRUE;
}

static BOOL peer_refresh_rect(rdpContext* context, uint8_t count, RECTANGLE_16* areas)
{
	RECTANGLE_16 invalidRect;
	PeerContext* ctx = (PeerContext*) context;

	for (size_t i = 0; i < count; i++)
	{
		VLOG(2) << "PEER: Client requested to refresh [(" << areas[i].left << ", " << areas[i].top << "), " << areas[i].right << ", " << areas[i].bottom << "]";

		invalidRect.left = areas[i].left;
		invalidRect.top = areas[i].top;
		invalidRect.right = areas[i].right;
		invalidRect.bottom = areas[i].bottom;

		EnterCriticalSection(&ctx->lock);

		region16_union_rect(&ctx->invalidRegion, &ctx->invalidRegion, &invalidRect);

		LeaveCriticalSection(&ctx->lock);
	}

	return TRUE;
}

static BOOL peer_suppress_output(rdpContext *context, uint8_t allow, RECTANGLE_16 *areas)
{
	if (allow > 0) {
		VLOG(2) << "PEER: Client requested to restore output";
	} else {
		VLOG(2) << "PEER: Client requested to suppress output";
	}

	return TRUE;
}

BOOL peer_surface_frame_acknowledge(rdpContext* context, UINT32 frameId)
{
	PeerContext* ctx = (PeerContext*) context;

	ctx->encoder->lastAckframeId = frameId;

	return TRUE;
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

RDPPeer::RDPPeer(freerdp_peer *client, RDPListener *listener) : client(client),
                                                                shm_buffer_region(listener->shm_buffer),
                                                                listener(listener),
                                                                stop(false)
{
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

	client->Capabilities = peer_capabilities;
	client->PostConnect = peer_post_connect;
	client->Activate = peer_activate;
	client->input->SynchronizeEvent = peer_synchronize_event;
	client->input->KeyboardEvent = peer_keyboard_event;
	client->input->MouseEvent = peer_mouse_event;

	client->update->RefreshRect = peer_refresh_rect;
	client->update->SuppressOutput = peer_suppress_output;
	client->update->SurfaceFrameAcknowledge = peer_surface_frame_acknowledge;

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
}

RDPPeer::~RDPPeer()
{
    freerdp_peer_context_free(client);
    freerdp_peer_free(client);
    LOG(INFO) << "PEER " << this << ": DESTRUCTING ACHTUNG WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING";
}

void RDPPeer::RunThread(freerdp_peer* client)
{
	int status;
	DWORD nCount;
	UINT64 currTime;
	DWORD dwTimeout;
	DWORD dwInterval;
	UINT64 frameTime;
	DWORD waitStatus;
	HANDLE events[64];
	PeerContext* ctx = (PeerContext*) client->context;

	this->listener->registerPeer(this);

	dwInterval = 1000 / ctx->frameRate;
	frameTime = GetTickCount64() + dwInterval;

	while (1)
	{
		nCount = 0;
		events[nCount++] = ctx->stopEvent;
		events[nCount++] = client->GetEventHandle(client);
		events[nCount++] = WTSVirtualChannelManagerGetEventHandle(ctx->vcm);

		currTime = GetTickCount64();
		dwTimeout = (DWORD) ((currTime > frameTime) ? 1 : frameTime - currTime);

		dwTimeout = 5; // set it to 5 ms for now

        // check if we are terminating
        {
            std::unique_lock<std::mutex> lock(stopMutex);
            if (stop) break;
        }

		waitStatus = WaitForMultipleObjects(nCount, events, FALSE, dwTimeout);

		if (waitStatus == WAIT_FAILED) {
			VLOG(1) << "PEER: Wait failed.";
			break;
		}

		if (WaitForSingleObject(ctx->stopEvent, 0) == WAIT_OBJECT_0)
			break;

		if (client->CheckFileDescriptor(client) != TRUE) {
			VLOG(1) << "PEER: Client closed connection.";
			break;
		}

		if (WTSVirtualChannelManagerCheckFileDescriptor(ctx->vcm) != TRUE) {
			VLOG(1) << "PEER: Virtual channel connection closed.";
			break;
		}

#if 0
		if (GetTickCount64() >= frameTime)
		{
			if (ctx->activated)
			{
				/* send update here */

				status = SendSurfaceUpdate(0, 0, 0, 0);

				if (status <= 0)
				{
					SetEvent(ctx->stopEvent);
					break;
				}
			}

			dwInterval = 1000 / ctx->frameRate;
			frameTime += dwInterval;
		}
#endif
	}

    if (!stop)
	    this->listener->unregisterPeer(this);
}

void RDPPeer::CloseClient()
{
    std::unique_lock<std::mutex> lock(stopMutex);
    stop = true;
}


void *RDPPeer::PeerThread(void *arg)
{
	auto tuple = (std::tuple<freerdp_peer*, RDPListener *> *) arg;
	auto tuple_obj = *tuple;

	freerdp_peer *client = std::get<0>(tuple_obj);
	RDPListener *listener = std::get<1>(tuple_obj);

	auto peer = make_unique<RDPPeer>(client, listener);

	client->Initialize(client);

	peer->RunThread(client);

	client->Disconnect(client);
	VLOG(1) << "PEER: Client disconnected.";
	delete tuple; // should be safe to do, and helps it not leak
	return NULL;
}

void RDPPeer::ProcessMouseMsg(uint16_t flags, uint16_t x, uint16_t y)
{
	std::vector<uint16_t> vec;
	vec.push_back(MOUSE);
	vec.push_back(x);
	vec.push_back(y);
	vec.push_back(flags);

	//VLOG(2) << "PEER: Now sending RDP mouse client message to the QEMU VM";
	listener->processOutgoingMessage(vec);
}

void RDPPeer::ProcessKeyboardMsg(uint16_t flags, uint16_t keycode)
{
	std::vector<uint16_t> vec;
	vec.push_back(KEYBOARD);
	vec.push_back(keycode);
	vec.push_back(flags);

	VLOG(2) << "PEER: Now sending RDP keyboard client message to the QEMU VM";
	listener->processOutgoingMessage(vec);
}

void RDPPeer::PartialDisplayUpdate(uint32_t x_coord, uint32_t y_coord, uint32_t width, uint32_t height)
{
	if (0)
	{
		RECTANGLE_16 invalidRect;
		PeerContext* ctx = (PeerContext*) client->context;

		invalidRect.left = (UINT16) x_coord;
		invalidRect.top = (UINT16) y_coord;
		invalidRect.right = (UINT16) (x_coord + width);
		invalidRect.bottom = (UINT16) (y_coord + height);

		EnterCriticalSection(&ctx->lock);

		region16_union_rect(&ctx->invalidRegion, &ctx->invalidRegion, &invalidRect);

		LeaveCriticalSection(&ctx->lock);
	}
	else
	{
		SendSurfaceUpdate((int) x_coord, (int) y_coord, (int) width, (int) height);
	}
}

PIXEL_FORMAT RDPPeer::GetPixelFormatForPixmanFormat(pixman_format_code_t f)
{
	switch (f)
	{
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
		case PIXMAN_r5g6b5:
		case PIXMAN_x1r5g5b5:
		default:
			return PIXEL_FORMAT_INVALID;
	}
}

void RDPPeer::CreateSurface(int width, int height, PIXEL_FORMAT r)
{
	PeerContext* ctx = (PeerContext*) client->context;

	buf_width = (size_t) width;
	buf_height = (size_t) height;

	fprintf(stderr, "CreateSurface: %dx%d\n", (int) buf_width, (int) buf_height);

	switch (r)
	{
		case PIXEL_FORMAT_r8g8b8a8:
			VLOG(2) << "PEER: Launching R8G8B8A8 Displaybuffer with dimensions " << buf_width << "x" << buf_height;
			ctx->sourceBpp = 4;
			ctx->sourceFormat = PIXEL_FORMAT_XBGR32;
			ctx->encodeFormat = PIXEL_FORMAT_XBGR32;
			break;

		case PIXEL_FORMAT_a8r8g8b8:
			VLOG(2) << "PEER: Launching A8R8G8B8 Displaybuffer with dimensions " << buf_width << "x" << buf_height;
			ctx->sourceBpp = 4;
			ctx->sourceFormat = PIXEL_FORMAT_XRGB32;
			ctx->encodeFormat = PIXEL_FORMAT_XRGB32;
			break;

		case PIXEL_FORMAT_r8g8b8:
			VLOG(2) << "PEER: Launching R8G8B8 Displaybuffer with dimensions " << buf_width << "x" << buf_height;
			ctx->sourceBpp = 3;
			ctx->sourceFormat = PIXEL_FORMAT_BGR24;
			ctx->encodeFormat = PIXEL_FORMAT_XRGB32;
			break;

		case PIXEL_FORMAT_b8g8r8:
			VLOG(2) << "PEER: Launching R8G8B8 Displaybuffer to deal with BGR data with dimensions " << buf_width << "x" << buf_height;
			ctx->sourceBpp = 3;
			ctx->sourceFormat = PIXEL_FORMAT_RGB24;
			ctx->encodeFormat = PIXEL_FORMAT_XRGB32;
			break;

		default:
			LOG(ERROR) << "UNKNOWN PIXEL FORMAT ERROR ERROR THIS SHOULD NEVER HAPPEN ERROR";
			break;
	}

	if (ctx->surface)
	{
		rdpmux_surface_free(ctx->surface);
		ctx->surface = NULL;
	}

	ctx->surface = rdpmux_surface_new(0, 0, buf_width, buf_height);

	rdpmux_encoder_set_pixel_format(ctx->encoder, ctx->encodeFormat);
	rdpmux_encoder_reset(ctx->encoder, buf_width, buf_height);
}

void RDPPeer::FullDisplayUpdate(uint32_t displayWidth, uint32_t displayHeight, pixman_format_code_t f)
{
	RECTANGLE_16 invalidRect;
	PIXEL_FORMAT displayFormat;
	PeerContext* ctx = (PeerContext*) client->context;
	rdpSettings* settings = client->settings;

	fprintf(stderr, "FullDisplayUpdate: %dx%d\n", displayWidth, displayHeight);

	displayFormat = GetPixelFormatForPixmanFormat(f);

	if (displayFormat == PIXEL_FORMAT_INVALID) {
		LOG(WARNING) << "Invalid pixel format received!";
		return;
	}

	if (!ctx->surface || (displayWidth != settings->DesktopWidth) || (displayHeight != settings->DesktopHeight))
	{
		buf_width = displayWidth;
		buf_height = displayHeight;
		buf_format = displayFormat;

		VLOG(3) << "PEER " << this << ": Attempting to take lock on surface to recreate";
		{
			std::lock_guard<std::mutex> lock(surface_lock);
			VLOG(3) << "PEER " << this << ": Locked surface to recreate";
			CreateSurface(buf_width, buf_height, displayFormat);
		}
		VLOG(3) << "PEER " << this << ": Recreated display surface object; lock released";

		if ((displayWidth != settings->DesktopWidth) || (displayHeight != settings->DesktopHeight))
		{
			settings->DesktopWidth = displayWidth;
			settings->DesktopHeight = displayHeight;

			client->update->DesktopResize(client->update->context);

			ctx->activated = FALSE;
		}
	}

	invalidRect.left = 0;
	invalidRect.top = 0;
	invalidRect.right = displayWidth;
	invalidRect.bottom = displayHeight;

	EnterCriticalSection(&ctx->lock);

	region16_union_rect(&ctx->invalidRegion, &ctx->invalidRegion, &invalidRect);

	LeaveCriticalSection(&ctx->lock);
}

int RDPPeer::SendSurfaceBits(int nXSrc, int nYSrc, int nWidth, int nHeight)
{
	int i;
	BOOL first;
	BOOL last;
	wStream* s;
	int nSrcStep;
	BYTE* pSrcData;
	int numMessages;
	UINT32 frameId = 0;
	rdpUpdate* update;
	rdpContext* context;
	rdpSettings* settings;
	rdpMuxEncoder* encoder;
	rdpMuxSurface* surface;
	SURFACE_BITS_COMMAND cmd;
	PeerContext* ctx = (PeerContext*) client->context;

	context = (rdpContext*) client->context;
	update = context->update;
	settings = context->settings;

	encoder = ctx->encoder;
	surface = ctx->surface;

	freerdp_image_copy(surface->data, ctx->encodeFormat, surface->scanline, nXSrc, nYSrc,
		nWidth, nHeight, (BYTE*) shm_buffer_region, ctx->sourceFormat, buf_width * ctx->sourceBpp, nXSrc, nYSrc, NULL);

	pSrcData = surface->data;
	nSrcStep = surface->scanline;

	if (encoder->frameAck)
		frameId = rdpmux_encoder_create_frame_id(encoder);

	if (settings->RemoteFxCodec)
	{
		RFX_RECT rect;
		RFX_MESSAGE* messages;
		RFX_RECT* messageRects = NULL;

		rdpmux_encoder_prepare(encoder, FREERDP_CODEC_REMOTEFX);

		s = encoder->bs;

		rect.x = nXSrc;
		rect.y = nYSrc;
		rect.width = nWidth;
		rect.height = nHeight;

		if (!(messages = rfx_encode_messages(encoder->rfx, &rect, 1, pSrcData,
				settings->DesktopWidth, settings->DesktopHeight, nSrcStep, &numMessages,
				settings->MultifragMaxRequestSize)))
		{
			return 0;
		}

		cmd.codecID = settings->RemoteFxCodecId;

		cmd.destLeft = 0;
		cmd.destTop = 0;
		cmd.destRight = settings->DesktopWidth;
		cmd.destBottom = settings->DesktopHeight;

		cmd.bpp = 32;
		cmd.width = settings->DesktopWidth;
		cmd.height = settings->DesktopHeight;
		cmd.skipCompression = TRUE;

		if (numMessages > 0)
			messageRects = messages[0].rects;

		for (i = 0; i < numMessages; i++)
		{
			Stream_SetPosition(s, 0);

			if (!rfx_write_message(encoder->rfx, s, &messages[i]))
			{
				while (i < numMessages)
				{
					rfx_message_free(encoder->rfx, &messages[i++]);
				}
				break;
			}
			rfx_message_free(encoder->rfx, &messages[i]);

			cmd.bitmapDataLength = Stream_GetPosition(s);
			cmd.bitmapData = Stream_Buffer(s);

			first = (i == 0) ? TRUE : FALSE;
			last = ((i + 1) == numMessages) ? TRUE : FALSE;

			if (!encoder->frameAck)
				IFCALL(update->SurfaceBits, update->context, &cmd);
			else
				IFCALL(update->SurfaceFrameBits, update->context, &cmd, first, last, frameId);
		}

		free(messageRects);
		free(messages);
	}
	else if (settings->NSCodec)
	{
		rdpmux_encoder_prepare(encoder, FREERDP_CODEC_NSCODEC);

		s = encoder->bs;
		Stream_SetPosition(s, 0);

		pSrcData = &pSrcData[(nYSrc * nSrcStep) + (nXSrc * 4)];

		nsc_compose_message(encoder->nsc, s, pSrcData, nWidth, nHeight, nSrcStep);

		cmd.bpp = 32;
		cmd.codecID = settings->NSCodecId;
		cmd.destLeft = nXSrc;
		cmd.destTop = nYSrc;
		cmd.destRight = cmd.destLeft + nWidth;
		cmd.destBottom = cmd.destTop + nHeight;
		cmd.width = nWidth;
		cmd.height = nHeight;

		cmd.bitmapDataLength = Stream_GetPosition(s);
		cmd.bitmapData = Stream_Buffer(s);
		cmd.skipCompression = TRUE;

		first = TRUE;
		last = TRUE;

		if (!encoder->frameAck)
			IFCALL(update->SurfaceBits, update->context, &cmd);
		else
			IFCALL(update->SurfaceFrameBits, update->context, &cmd, first, last, frameId);
	}

	return 1;
}

int RDPPeer::SendBitmapUpdate(int nXSrc, int nYSrc, int nWidth, int nHeight)
{
	BYTE* data;
	BYTE* buffer;
	int yIdx, xIdx, k;
	int rows, cols;
	int nSrcStep;
	BYTE* pSrcData;
	UINT32 DstSize;
	UINT32 SrcFormat;
	BITMAP_DATA* bitmap;
	rdpUpdate* update;
	rdpContext* context;
	rdpSettings* settings;
	UINT32 maxUpdateSize;
	UINT32 totalBitmapSize;
	UINT32 updateSizeEstimate;
	BITMAP_DATA* bitmapData;
	BITMAP_UPDATE bitmapUpdate;
	rdpMuxEncoder* encoder;
	rdpMuxSurface* surface;
	PeerContext* ctx = (PeerContext*) client->context;

	context = (rdpContext*) client->context;
	update = context->update;
	settings = context->settings;

	encoder = ctx->encoder;
	surface = ctx->surface;

	freerdp_image_copy(surface->data, ctx->encodeFormat, surface->scanline, nXSrc, nYSrc,
		nWidth, nHeight, (BYTE*) shm_buffer_region, ctx->sourceFormat, buf_width * ctx->sourceBpp, nXSrc, nYSrc, NULL);

	maxUpdateSize = settings->MultifragMaxRequestSize;

	if (settings->ColorDepth < 32)
		rdpmux_encoder_prepare(encoder, FREERDP_CODEC_INTERLEAVED);
	else
		rdpmux_encoder_prepare(encoder, FREERDP_CODEC_PLANAR);

	pSrcData = surface->data;
	nSrcStep = surface->scanline;
	SrcFormat = ctx->encodeFormat;

	if ((nXSrc % 4) != 0)
	{
		nWidth += (nXSrc % 4);
		nXSrc -= (nXSrc % 4);
	}

	if ((nYSrc % 4) != 0)
	{
		nHeight += (nYSrc % 4);
		nYSrc -= (nYSrc % 4);
	}

	rows = (nHeight / 64) + ((nHeight % 64) ? 1 : 0);
	cols = (nWidth / 64) + ((nWidth % 64) ? 1 : 0);

	k = 0;
	totalBitmapSize = 0;

	bitmapUpdate.count = bitmapUpdate.number = rows * cols;

	if (!(bitmapData = (BITMAP_DATA*) malloc(sizeof(BITMAP_DATA) * bitmapUpdate.number)))
		return -1;

	bitmapUpdate.rectangles = bitmapData;

	if ((nWidth % 4) != 0)
	{
		nWidth += (4 - (nWidth % 4));
	}

	if ((nHeight % 4) != 0)
	{
		nHeight += (4 - (nHeight % 4));
	}

	for (yIdx = 0; yIdx < rows; yIdx++)
	{
		for (xIdx = 0; xIdx < cols; xIdx++)
		{
			bitmap = &bitmapData[k];

			bitmap->width = 64;
			bitmap->height = 64;
			bitmap->destLeft = nXSrc + (xIdx * 64);
			bitmap->destTop = nYSrc + (yIdx * 64);

			if ((int) (bitmap->destLeft + bitmap->width) > (nXSrc + nWidth))
				bitmap->width = (nXSrc + nWidth) - bitmap->destLeft;

			if ((int) (bitmap->destTop + bitmap->height) > (nYSrc + nHeight))
				bitmap->height = (nYSrc + nHeight) - bitmap->destTop;

			bitmap->destRight = bitmap->destLeft + bitmap->width - 1;
			bitmap->destBottom = bitmap->destTop + bitmap->height - 1;
			bitmap->compressed = TRUE;

			if ((bitmap->width < 4) || (bitmap->height < 4))
				continue;

			if (settings->ColorDepth < 32)
			{
				int bitsPerPixel = settings->ColorDepth;
				int bytesPerPixel = (bitsPerPixel + 7) / 8;

				DstSize = 64 * 64 * 4;
				buffer = encoder->grid[k];

				interleaved_compress(encoder->interleaved, buffer, &DstSize, bitmap->width, bitmap->height,
						pSrcData, SrcFormat, nSrcStep, bitmap->destLeft, bitmap->destTop, NULL, bitsPerPixel);

				bitmap->bitmapDataStream = buffer;
				bitmap->bitmapLength = DstSize;
				bitmap->bitsPerPixel = bitsPerPixel;
				bitmap->cbScanWidth = bitmap->width * bytesPerPixel;
				bitmap->cbUncompressedSize = bitmap->width * bitmap->height * bytesPerPixel;
			}
			else
			{
				int dstSize;

				buffer = encoder->grid[k];
				data = &pSrcData[(bitmap->destTop * nSrcStep) + (bitmap->destLeft * 4)];

				buffer = freerdp_bitmap_compress_planar(encoder->planar, data, SrcFormat,
						bitmap->width, bitmap->height, nSrcStep, buffer, &dstSize);

				bitmap->bitmapDataStream = buffer;
				bitmap->bitmapLength = dstSize;
				bitmap->bitsPerPixel = 32;
				bitmap->cbScanWidth = bitmap->width * 4;
				bitmap->cbUncompressedSize = bitmap->width * bitmap->height * 4;
			}

			bitmap->cbCompFirstRowSize = 0;
			bitmap->cbCompMainBodySize = bitmap->bitmapLength;

			totalBitmapSize += bitmap->bitmapLength;
			k++;
		}
	}

	bitmapUpdate.count = bitmapUpdate.number = k;

	updateSizeEstimate = totalBitmapSize + (k * bitmapUpdate.count) + 16;

	if (updateSizeEstimate > maxUpdateSize)
	{
		int i, j;
		UINT32 updateSize;
		UINT32 newUpdateSize;
		BITMAP_DATA* fragBitmapData = NULL;

		if (k > 0)
			fragBitmapData = (BITMAP_DATA*) malloc(sizeof(BITMAP_DATA) * k);

		if (!fragBitmapData)
		{
			free(bitmapData);
			return -1;
		}
		bitmapUpdate.rectangles = fragBitmapData;

		i = j = 0;
		updateSize = 1024;

		while (i < k)
		{
			newUpdateSize = updateSize + (bitmapData[i].bitmapLength + 16);

			if ((newUpdateSize < maxUpdateSize) && ((i + 1) < k))
			{
				CopyMemory(&fragBitmapData[j++], &bitmapData[i++], sizeof(BITMAP_DATA));
				updateSize = newUpdateSize;
			}
			else
			{
				if ((i + 1) >= k)
				{
					CopyMemory(&fragBitmapData[j++], &bitmapData[i++], sizeof(BITMAP_DATA));
					updateSize = newUpdateSize;
				}

				bitmapUpdate.count = bitmapUpdate.number = j;
				IFCALL(update->BitmapUpdate, context, &bitmapUpdate);
				updateSize = 1024;
				j = 0;
			}
		}

		free(fragBitmapData);
	}
	else
	{
		IFCALL(update->BitmapUpdate, context, &bitmapUpdate);
	}

	free(bitmapData);

	return 1;
}

int RDPPeer::SendSurfaceUpdate(int x, int y, int width, int height)
{
	int l, r, t, b;
	int status = -1;
	rdpContext* context;
	rdpSettings* settings;
	rdpMuxSurface* surface;
	RECTANGLE_16 surfaceRect;
	RECTANGLE_16 invalidRect;
	const RECTANGLE_16* extents;
	PeerContext* ctx = (PeerContext*) client->context;

	//fprintf(stderr, "SendSurfaceUpdate: x: %d y: %d width: %d height: %d activated: %d surface: %p\n",
	//		x, y, width, height, ctx->activated, ctx->surface);

	context = (rdpContext*) client->context;
	settings = context->settings;
	surface = ctx->surface;

	invalidRect.left = x;
	invalidRect.top = y;
	invalidRect.right = x + width;
	invalidRect.bottom = y + height;

	if ((width * height) > 0)
	{
		EnterCriticalSection(&ctx->lock);

		region16_union_rect(&ctx->invalidRegion, &ctx->invalidRegion, &invalidRect);

		if (surface)
		{
			surfaceRect.left = 0;
			surfaceRect.top = 0;
			surfaceRect.right = surface->width;
			surfaceRect.bottom = surface->height;

			region16_intersect_rect(&ctx->invalidRegion, &ctx->invalidRegion, &surfaceRect);
		}

		LeaveCriticalSection(&ctx->lock);
	}

	if (!ctx->activated || !ctx->surface)
		return 1;

	if (!surface)
		return 1;

	EnterCriticalSection(&ctx->lock);

	if (region16_is_empty(&ctx->invalidRegion))
	{
		LeaveCriticalSection(&ctx->lock);
		return 1;
	}

	extents = region16_extents(&ctx->invalidRegion);

	l = extents->left;
	r = extents->right;
	t = extents->top;
	b = extents->bottom;

	region16_clear(&ctx->invalidRegion);

	LeaveCriticalSection(&ctx->lock);

	if (l % 16)
		l -= (l % 16);

	if (t % 16)
		t -= (t % 16);

	if (r % 16)
		r += 16 - (r % 16);

	if (b % 16)
		b += 16 - (b % 16);

	if (r > (int) buf_width)
		r = (int) buf_width;

	if (b > (int) buf_height)
		b = (int) buf_height;

	x = l;
	y = t;
	width = r - l;
	height = b - t;

	if (0)
	{
		x = 0;
		y = 0;
		width = (int) buf_width;
		height = (int) buf_height;
	}

	fprintf(stderr, "SendSurfaceUpdate: x: %d y: %d width: %d height: %d\n", x, y, width, height);

	if (settings->RemoteFxCodec || settings->NSCodec)
	{
		status = SendSurfaceBits(x, y, width, height);
	}
	else
	{
		status = SendBitmapUpdate(x, y, width, height);
	}

	return status;
}

