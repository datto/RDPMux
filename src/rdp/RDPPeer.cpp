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

namespace po = boost::program_options;
extern po::variables_map vm;

/* Context activation functions */
static BOOL peer_context_new(freerdp_peer *client, PeerContext *context)
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

	settings->CompressionLevel = PACKET_COMPR_TYPE_RDP6;

	settings->SuppressOutput = TRUE;
	settings->RefreshRect = TRUE;

	settings->RdpSecurity = TRUE;
	settings->TlsSecurity = TRUE;
	settings->NlaSecurity = FALSE;

	settings->EncryptionLevel = ENCRYPTION_LEVEL_CLIENT_COMPATIBLE;

	context->vcm = WTSOpenServerA((LPSTR) client->context);

	if (!context->vcm || context->vcm == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	context->encoder = rdpmux_encoder_new(settings);

	if (!context->encoder)
		return FALSE;

	return TRUE;
}

static void peer_context_free(freerdp_peer *client, PeerContext *ctx)
{
	if (!ctx)
		return;

	rdpmux_encoder_free(ctx->encoder);

	WTSCloseServer((HANDLE) ctx->vcm);

	if (ctx->surface)
	{
		rdpmux_surface_free(ctx->surface);
		ctx->surface = NULL;
	}
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

	DesktopWidth = ctx->peerObj->GetSurfaceWidth();
	DesktopHeight = ctx->peerObj->GetSurfaceHeight();
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

	if (settings->ClientDir && (strcmp(settings->ClientDir, "librdp") == 0))
	{
		/* Hack for Mac/iOS/Android Microsoft RDP clients */

		settings->RemoteFxCodec = FALSE;

		settings->NSCodec = FALSE;
		settings->NSCodecAllowSubsampling = FALSE;

		settings->SurfaceFrameMarkerEnabled = FALSE;
	}

	auto DesktopWidth = ctx->peerObj->GetSurfaceWidth();
	auto DesktopHeight = ctx->peerObj->GetSurfaceHeight();

	rdpmux_encoder_reset(encoder, DesktopWidth, DesktopHeight);

	ctx->activated = TRUE;

	VLOG(3) << std::dec << "PEER: client->settings->Desktop{Width,Height}: " << DesktopWidth << " " << DesktopHeight;
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
	PeerContext *ctx = (PeerContext *) input->context;
	ctx->peerObj->FullDisplayUpdate(ctx->peerObj->GetListener()->GetFormat());
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
                                                                listener(listener)
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

void RDPPeer::RunThread(freerdp_peer *client)
{
	HANDLE handles[32];
	DWORD count, status;
	PeerContext *context = (PeerContext *) client->context;

	this->listener->registerPeer(this);

	while (1)
	{
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

	VLOG(2) << "PEER: Now sending RDP mouse client message to the QEMU VM";
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
	UpdateRegion(x_coord, y_coord, width, height, FALSE);
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

void RDPPeer::CreateSurface(PIXEL_FORMAT r)
{
	PeerContext* ctx = (PeerContext*) client->context;

	switch (r)
	{
		case PIXEL_FORMAT_r8g8b8a8:
			VLOG(2) << "PEER: Launching R8G8B8A8 Displaybuffer with dimensions " << buf_width << "x" << buf_height;
			ctx->sourceFormat = PIXEL_FORMAT_XBGR32;
			ctx->encodeFormat = PIXEL_FORMAT_XBGR32;
			break;

		case PIXEL_FORMAT_a8r8g8b8:
			VLOG(2) << "PEER: Launching A8R8G8B8 Displaybuffer with dimensions " << buf_width << "x" << buf_height;
			ctx->sourceFormat = PIXEL_FORMAT_XRGB32;
			ctx->encodeFormat = PIXEL_FORMAT_XRGB32;
			break;

		case PIXEL_FORMAT_r8g8b8:
			VLOG(2) << "PEER: Launching R8G8B8 Displaybuffer with dimensions " << buf_width << "x" << buf_height;
			ctx->sourceFormat = PIXEL_FORMAT_BGR24;
			ctx->encodeFormat = PIXEL_FORMAT_XRGB32;
			break;

		case PIXEL_FORMAT_b8g8r8:
			VLOG(2) << "PEER: Launching R8G8B8 Displaybuffer to deal with BGR data with dimensions " << buf_width << "x" << buf_height;
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
}

void RDPPeer::FullDisplayUpdate(pixman_format_code_t f)
{
	PIXEL_FORMAT r;
	buf_width = listener->GetWidth();
	buf_height = listener->GetHeight();

	r = GetPixelFormatForPixmanFormat(f);

	if (r == PIXEL_FORMAT_INVALID) {
		LOG(WARNING) << "Invalid pixel format received!";
		return;
	}

	VLOG(3) << "PEER " << this << ": Attempting to take lock on surface to recreate";
	{
		std::lock_guard<std::mutex> lock(surface_lock);
		VLOG(3) << "PEER " << this << ": Locked surface to recreate";
		CreateSurface(r);
	}
	VLOG(3) << "PEER " << this << ": Recreated display surface object; lock released";

	UpdateRegion(0, 0, buf_width, buf_height, TRUE);
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

	freerdp_image_copy(surface->data, ctx->encodeFormat, surface->scanline, 0, 0,
		buf_width, buf_height, (BYTE*) shm_buffer_region, ctx->sourceFormat, buf_width * 4, 0, 0, NULL);

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

	freerdp_image_copy(surface->data, ctx->encodeFormat, surface->scanline, 0, 0,
		buf_width, buf_height, (BYTE*) shm_buffer_region, ctx->sourceFormat, buf_width * 4, 0, 0, NULL);

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

int RDPPeer::SendSurfaceUpdate(void)
{
	int status = -1;
	int nXSrc, nYSrc;
	int nWidth, nHeight;
	rdpContext* context;
	rdpSettings* settings;
	PeerContext* ctx = (PeerContext*) client->context;

	context = (rdpContext*) client->context;
	settings = context->settings;

	nXSrc = 0;
	nYSrc = 0;
	nWidth = buf_width;
	nHeight = buf_height;

	if (settings->RemoteFxCodec || settings->NSCodec)
	{
		status = SendSurfaceBits(nXSrc, nYSrc, nWidth, nHeight);
	}
	else
	{
		status = SendBitmapUpdate(nXSrc, nYSrc, nWidth, nHeight);
	}

	return status;
}

void RDPPeer::UpdateRegion(uint32_t x, uint32_t y, uint32_t w, uint32_t h, BOOL fullRefresh)
{
	PeerContext* ctx = (PeerContext*) client->context;

	if (!ctx->activated)
		return;

	if (!ctx->surface)
		return;

	SendSurfaceUpdate();
}
