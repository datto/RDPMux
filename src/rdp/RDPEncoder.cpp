
#include "rdp/RDPEncoder.h"

int rdpmux_encoder_preferred_fps(rdpMuxEncoder* encoder)
{
	/* Return preferred fps calculated according to the last
	 * sent frame id and last client-acknowledged frame id.
	 */
	return encoder->fps;
}

UINT32 rdpmux_encoder_inflight_frames(rdpMuxEncoder* encoder)
{
	/* Return inflight frame count =
	 * <last sent frame id> - <last client-acknowledged frame id>
	 * Note: This function is exported so that subsystem could
	 * implement its own strategy to tune fps.
	 */
	return encoder->frameId - encoder->lastAckframeId;
}

UINT32 rdpmux_encoder_create_frame_id(rdpMuxEncoder* encoder)
{
	UINT32 frameId;
	int inFlightFrames;

	inFlightFrames = rdpmux_encoder_inflight_frames(encoder);

	/*
	 * Calculate preferred fps according to how much frames are
	 * in-progress. Note that it only works when subsytem implementation
	 * calls rdpmux_encoder_preferred_fps and takes the suggestion.
	 */

	if (inFlightFrames > 1)
	{
		encoder->fps = (100 / (inFlightFrames + 1) * encoder->maxFps) / 100;
	}
	else
	{
		encoder->fps += 2;

		if (encoder->fps > encoder->maxFps)
			encoder->fps = encoder->maxFps;
	}

	if (encoder->fps < 1)
		encoder->fps = 1;

	frameId = ++encoder->frameId;

	return frameId;
}

int rdpmux_encoder_init_grid(rdpMuxEncoder* encoder)
{
	int i, j, k;
	int tileSize;
	int tileCount;

	encoder->gridWidth = ((encoder->width + (encoder->maxTileWidth - 1)) / encoder->maxTileWidth);
	encoder->gridHeight = ((encoder->height + (encoder->maxTileHeight - 1)) / encoder->maxTileHeight);

	tileSize = encoder->maxTileWidth * encoder->maxTileHeight * 4;
	tileCount = encoder->gridWidth * encoder->gridHeight;

	encoder->gridBuffer = (BYTE*) malloc(tileSize * tileCount);

	if (!encoder->gridBuffer)
		return -1;

	encoder->grid = (BYTE**) malloc(tileCount * sizeof(BYTE*));

	if (!encoder->grid)
		return -1;

	for (i = 0; i < encoder->gridHeight; i++)
	{
		for (j = 0; j < encoder->gridWidth; j++)
		{
			k = (i * encoder->gridWidth) + j;
			encoder->grid[k] = &(encoder->gridBuffer[k * tileSize]);
		}
	}

	return 0;
}

int rdpmux_encoder_uninit_grid(rdpMuxEncoder* encoder)
{
	if (encoder->gridBuffer)
	{
		free(encoder->gridBuffer);
		encoder->gridBuffer = NULL;
	}

	if (encoder->grid)
	{
		free(encoder->grid);
		encoder->grid = NULL;
	}

	encoder->gridWidth = 0;
	encoder->gridHeight = 0;

	return 0;
}

int rdpmux_encoder_init_rfx(rdpMuxEncoder* encoder)
{
	rdpSettings* settings = encoder->settings;

	if (!encoder->rfx)
		encoder->rfx = rfx_context_new(TRUE);

	if (!encoder->rfx)
		goto fail;

	if (!rfx_context_reset(encoder->rfx, encoder->width, encoder->height))
		goto fail;

	encoder->rfx->mode = RLGR3;
	encoder->rfx->width = encoder->width;
	encoder->rfx->height = encoder->height;

	if (encoder->format == PIXEL_FORMAT_XRGB32)
		rfx_context_set_pixel_format(encoder->rfx, PIXEL_FORMAT_RGBA32);
	else
		rfx_context_set_pixel_format(encoder->rfx, PIXEL_FORMAT_BGRA32);

	encoder->fps = 16;
	encoder->maxFps = 32;
	encoder->frameId = 0;
	encoder->lastAckframeId = 0;
	encoder->frameAck = settings->SurfaceFrameMarkerEnabled;

	encoder->codecs |= FREERDP_CODEC_REMOTEFX;

	return 1;

fail:
	rfx_context_free(encoder->rfx);
	return -1;
}

int rdpmux_encoder_init_nsc(rdpMuxEncoder* encoder)
{
	rdpSettings* settings = encoder->settings;

	if (!encoder->nsc)
		encoder->nsc = nsc_context_new();

	if (!encoder->nsc)
		return -1;

	if (encoder->format == PIXEL_FORMAT_XRGB32)
		nsc_context_set_pixel_format(encoder->nsc, PIXEL_FORMAT_RGBA32);
	else
		nsc_context_set_pixel_format(encoder->nsc, PIXEL_FORMAT_RGBA32);

	encoder->fps = 16;
	encoder->maxFps = 32;
	encoder->frameId = 0;
	encoder->lastAckframeId = 0;
	encoder->frameAck = settings->SurfaceFrameMarkerEnabled;

	encoder->nsc->ColorLossLevel = settings->NSCodecColorLossLevel;
	encoder->nsc->ChromaSubsamplingLevel = settings->NSCodecAllowSubsampling ? 1 : 0;
	encoder->nsc->DynamicColorFidelity = settings->NSCodecAllowDynamicColorFidelity;

	encoder->codecs |= FREERDP_CODEC_NSCODEC;

	return 1;
}

int rdpmux_encoder_init_planar(rdpMuxEncoder* encoder)
{
	DWORD planarFlags = 0;
	rdpSettings* settings = encoder->settings;

	if (settings->DrawAllowSkipAlpha)
		planarFlags |= PLANAR_FORMAT_HEADER_NA;

	planarFlags |= PLANAR_FORMAT_HEADER_RLE;

	if (!encoder->planar)
	{
		encoder->planar = freerdp_bitmap_planar_context_new(planarFlags,
				encoder->maxTileWidth, encoder->maxTileHeight);
	}

	if (!encoder->planar)
		return -1;

	encoder->codecs |= FREERDP_CODEC_PLANAR;

	return 1;
}

int rdpmux_encoder_init_interleaved(rdpMuxEncoder* encoder)
{
	if (!encoder->interleaved)
		encoder->interleaved = bitmap_interleaved_context_new(TRUE);

	if (!encoder->interleaved)
		return -1;

	encoder->codecs |= FREERDP_CODEC_INTERLEAVED;

	return 1;
}

int rdpmux_encoder_init(rdpMuxEncoder* encoder, UINT32 width, UINT32 height)
{
	encoder->width = width;
	encoder->height = height;

	encoder->maxTileWidth = 64;
	encoder->maxTileHeight = 64;

	rdpmux_encoder_init_grid(encoder);

	if (!encoder->bs)
		encoder->bs = Stream_New(NULL, encoder->maxTileWidth * encoder->maxTileHeight * 4);

	if (!encoder->bs)
		return -1;

	return 1;
}

int rdpmux_encoder_uninit_rfx(rdpMuxEncoder* encoder)
{
	if (encoder->rfx)
	{
		rfx_context_free(encoder->rfx);
		encoder->rfx = NULL;
	}

	encoder->codecs &= ~FREERDP_CODEC_REMOTEFX;

	return 1;
}

int rdpmux_encoder_uninit_nsc(rdpMuxEncoder* encoder)
{
	if (encoder->nsc)
	{
		nsc_context_free(encoder->nsc);
		encoder->nsc = NULL;
	}

	encoder->codecs &= ~FREERDP_CODEC_NSCODEC;

	return 1;
}

int rdpmux_encoder_uninit_planar(rdpMuxEncoder* encoder)
{
	if (encoder->planar)
	{
		freerdp_bitmap_planar_context_free(encoder->planar);
		encoder->planar = NULL;
	}

	encoder->codecs &= ~FREERDP_CODEC_PLANAR;

	return 1;
}

int rdpmux_encoder_uninit_interleaved(rdpMuxEncoder* encoder)
{
	if (encoder->interleaved)
	{
		bitmap_interleaved_context_free(encoder->interleaved);
		encoder->interleaved = NULL;
	}

	encoder->codecs &= ~FREERDP_CODEC_INTERLEAVED;

	return 1;
}

int rdpmux_encoder_uninit(rdpMuxEncoder* encoder)
{
	rdpmux_encoder_uninit_grid(encoder);

	if (encoder->bs)
	{
		Stream_Free(encoder->bs, TRUE);
		encoder->bs = NULL;
	}

	if (encoder->codecs & FREERDP_CODEC_REMOTEFX)
	{
		rdpmux_encoder_uninit_rfx(encoder);
	}

	if (encoder->codecs & FREERDP_CODEC_NSCODEC)
	{
		rdpmux_encoder_uninit_nsc(encoder);
	}

	if (encoder->codecs & FREERDP_CODEC_PLANAR)
	{
		rdpmux_encoder_uninit_planar(encoder);
	}

	if (encoder->codecs & FREERDP_CODEC_INTERLEAVED)
	{
		rdpmux_encoder_uninit_interleaved(encoder);
	}

	return 1;
}

int rdpmux_encoder_reset(rdpMuxEncoder* encoder, UINT32 width, UINT32 height)
{
	int status;
	UINT32 codecs = encoder->codecs;

	status = rdpmux_encoder_uninit(encoder);

	if (status < 0)
		return -1;

	status = rdpmux_encoder_init(encoder, width, height);

	if (status < 0)
		return -1;

	status = rdpmux_encoder_prepare(encoder, codecs);

	if (status < 0)
		return -1;

	return 1;
}

int rdpmux_encoder_prepare(rdpMuxEncoder* encoder, UINT32 codecs)
{
	int status;

	if ((codecs & FREERDP_CODEC_REMOTEFX) && !(encoder->codecs & FREERDP_CODEC_REMOTEFX))
	{
		status = rdpmux_encoder_init_rfx(encoder);

		if (status < 0)
			return -1;
	}

	if ((codecs & FREERDP_CODEC_NSCODEC) && !(encoder->codecs & FREERDP_CODEC_NSCODEC))
	{
		status = rdpmux_encoder_init_nsc(encoder);

		if (status < 0)
			return -1;
	}

	if ((codecs & FREERDP_CODEC_PLANAR) && !(encoder->codecs & FREERDP_CODEC_PLANAR))
	{
		status = rdpmux_encoder_init_planar(encoder);

		if (status < 0)
			return -1;
	}

	if ((codecs & FREERDP_CODEC_INTERLEAVED) && !(encoder->codecs & FREERDP_CODEC_INTERLEAVED))
	{
		status = rdpmux_encoder_init_interleaved(encoder);

		if (status < 0)
			return -1;
	}

	return 1;
}

void rdpmux_encoder_set_pixel_format(rdpMuxEncoder* encoder, UINT32 format)
{
	encoder->format = format;
}

int rdpmux_encoder_compare(rdpMuxEncoder* encoder, BYTE* pData1, int nStep1,
		int nWidth, int nHeight, BYTE* pData2, int nStep2, RECTANGLE_16* rect)
{
	BOOL equal;
	BOOL allEqual;
	int tw, th;
	int tx, ty, k;
	int nrow, ncol;
	int l, t, r, b;
	int left, top;
	int right, bottom;
	BYTE *p1, *p2;
	BOOL rows[1024];

	allEqual = TRUE;
	FillMemory(rows, sizeof(rows), 0xFF);

	nrow = (nHeight + 15) / 16;
	ncol = (nWidth + 15) / 16;

	l = ncol + 1;
	r = -1;

	t = nrow + 1;
	b = -1;

	for (ty = 0; ty < nrow; ty++)
	{
		th = ((ty + 1) == nrow) ? (nHeight % 16) : 16;

		if (!th)
			th = 16;

		for (tx = 0; tx < ncol; tx++)
		{
			equal = TRUE;

			tw = ((tx + 1) == ncol) ? (nWidth % 16) : 16;

			if (!tw)
				tw = 16;

			p1 = &pData1[(ty * 16 * nStep1) + (tx * 16 * 4)];
			p2 = &pData2[(ty * 16 * nStep2) + (tx * 16 * 4)];

			for (k = 0; k < th; k++)
			{
				if (memcmp(p1, p2, tw * 4) != 0)
				{
					equal = FALSE;
					break;
				}

				p1 += nStep1;
				p2 += nStep2;
			}

			if (!equal)
			{
				rows[ty] = FALSE;

				if (l > tx)
					l = tx;

				if (r < tx)
					r = tx;
			}
		}

		if (!rows[ty])
		{
			allEqual = FALSE;

			if (t > ty)
				t = ty;

			if (b < ty)
				b = ty;
		}
	}

	if (allEqual)
	{
		rect->left = rect->top = 0;
		rect->right = rect->bottom = 0;
		return 0;
	}

	left = l * 16;
	top = t * 16;
	right = (r + 1) * 16;
	bottom = (b + 1) * 16;

	if (right > nWidth)
		right = nWidth;

	if (bottom > nHeight)
		bottom = nHeight;

	rect->left = left;
	rect->top = top;
	rect->right = right;
	rect->bottom = bottom;

	return 1;
}


rdpMuxEncoder* rdpmux_encoder_new(rdpSettings* settings)
{
	rdpMuxEncoder* encoder;

	encoder = (rdpMuxEncoder*) calloc(1, sizeof(rdpMuxEncoder));

	if (!encoder)
		return NULL;

	encoder->settings = settings;

	encoder->fps = 16;
	encoder->maxFps = 32;
	encoder->format = PIXEL_FORMAT_XRGB32;

	return encoder;
}

void rdpmux_encoder_free(rdpMuxEncoder* encoder)
{
	if (!encoder)
		return;

	rdpmux_encoder_uninit(encoder);

	free(encoder);
}
