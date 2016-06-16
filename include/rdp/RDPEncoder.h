
#ifndef RDPMUX_ENCODER_H
#define RDPMUX_ENCODER_H

#include <winpr/crt.h>
#include <winpr/stream.h>

#include <freerdp/freerdp.h>
#include <freerdp/codecs.h>

struct rdpmux_encoder
{
	rdpSettings* settings;

	int width;
	int height;
	UINT32 codecs;
	UINT32 format;

	BYTE** grid;
	int gridWidth;
	int gridHeight;
	BYTE* gridBuffer;
	int maxTileWidth;
	int maxTileHeight;

	wStream* bs;

	RFX_CONTEXT* rfx;
	NSC_CONTEXT* nsc;
	BITMAP_PLANAR_CONTEXT* planar;
	BITMAP_INTERLEAVED_CONTEXT* interleaved;

	int fps;
	int maxFps;
	BOOL frameAck;
	UINT32 frameId;
	UINT32 lastAckframeId;
};
typedef struct rdpmux_encoder rdpMuxEncoder;

#ifdef __cplusplus
extern "C" {
#endif

int rdpmux_encoder_reset(rdpMuxEncoder* encoder, UINT32 width, UINT32 height);
int rdpmux_encoder_prepare(rdpMuxEncoder* encoder, UINT32 codecs);

UINT32 rdpmux_encoder_create_frame_id(rdpMuxEncoder* encoder);

void rdpmux_encoder_set_pixel_format(rdpMuxEncoder* encoder, UINT32 format);

int rdpmux_encoder_compare(rdpMuxEncoder* encoder, BYTE* pData1, int nStep1,
		int nWidth, int nHeight, BYTE* pData2, int nStep2, RECTANGLE_16* rect);

rdpMuxEncoder* rdpmux_encoder_new(rdpSettings* settings);
void rdpmux_encoder_free(rdpMuxEncoder* encoder);

#ifdef __cplusplus
}
#endif

#endif /* RDPMUX_ENCODER_H */
