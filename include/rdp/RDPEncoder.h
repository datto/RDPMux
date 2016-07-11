
#ifndef RDPMUX_ENCODER_H
#define RDPMUX_ENCODER_H

#include <winpr/crt.h>
#include <winpr/stream.h>

#include <freerdp/freerdp.h>
#include <freerdp/codecs.h>

/**
 * @brief Struct holding metadata about the encoding state.
 */
struct rdpmux_encoder
{
    /**
     * @brief Pointer to internal library settings.
     */
	rdpSettings* settings;

    /**
     * @brief Width of the backing framebuffer region.
     */
	int width;
    /**
     * @brief Height of the backing framebuffer region.
     */
	int height;
    /**
     * @brief Bitfield representing the codecs in use by the encoder.
     */
	UINT32 codecs;
    /**
     * @brief RDP encoder pixel format.
     */
	UINT32 format;

    /**
     * @brief Encoder grid, used to align tiles for raw bitmap updates.
     */
	BYTE** grid;
    /**
     * @brief Width of grid.
     */
	int gridWidth;
    /*
     * @brief Height of grid.
     */
	int gridHeight;
    /**
     * @brief Bitmap representing state of grid.
     */
	BYTE* gridBuffer;
    /**
     * @brief Maximum tile width.
     */
	int maxTileWidth;
    /**
     * @brief Maximum tile height.
     */
	int maxTileHeight;

    /**
     * @brief Encoding stream that holds the encoded tile data.
     */
	wStream* bs;

    /**
     * @brief RFX codec context.
     */
	RFX_CONTEXT* rfx;
    /**
     * @brief NSC codec context.
     */
	NSC_CONTEXT* nsc;
    /**
     * @brief Planar bitmap encoder context.
     */
	BITMAP_PLANAR_CONTEXT* planar;
    /**
     * @brief Interleaved bitmap encoder context.
     */
	BITMAP_INTERLEAVED_CONTEXT* interleaved;

    /**
     * @brief Current target fps.
     */
	int fps;
    /**
     * @brief Max allowed target fps.
     */
	int maxFps;
    /**
     * @brief Flag whether frame acks are enabled.
     */
	BOOL frameAck;
    /**
     * @brief current frame ID.
     */
	UINT32 frameId;
    /**
     * @brief Last acknowleged frame from client.
     */
	UINT32 lastAckframeId;
};
typedef struct rdpmux_encoder rdpMuxEncoder;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Resets the state of the encoder.
 *
 * Reset the state of the encoder using the width and height values passed in.
 *
 * WARNING! Destroys the previous encoder struct.
 *
 * @param encoder The encoder to reset
 * @param width New width.
 * @param height New height.
 *
 * @returns 1 if successful, -1 if not.
 */
int rdpmux_encoder_reset(rdpMuxEncoder* encoder, UINT32 width, UINT32 height);

/**
 * @brief Initializes codecs.
 *
 * Uses the codec bitfield passed in to initialize the proper codecs.
 *
 * @param codecs Bitfield with the codecs to be used.
 *
 * @returns 1 for success, -1 for failure.
 */
int rdpmux_encoder_prepare(rdpMuxEncoder* encoder, UINT32 codecs);

/**
 * @brief Create frame id.
 *
 * Also calculate preferred fps according to how much frames are
 * in-progress. Note that it only works when subsytem implementation
 * calls rdpmux_encoder_preferred_fps() and takes the suggestion.
 *
 * @param encoder Encoder. Yay state!
 *
 * @returns frame ID.
 */
UINT32 rdpmux_encoder_create_frame_id(rdpMuxEncoder* encoder);

/**
 * @brief Return preferred fps calculated according to the last sent frame id and last client-acknowledged frame id.
 *
 * @param encoder Encoder.
 *
 * @returns Preferred fps.
 */
int rdpmux_encoder_preferred_fps(rdpMuxEncoder* encoder);

/**
 * @brief Sets subpixel layout of the encoder.
 *
 * @param encoder Encoder.
 * @param format New format to set.
 */
void rdpmux_encoder_set_pixel_format(rdpMuxEncoder* encoder, UINT32 format);

/**
 * @brief Currently unused.
 */
int rdpmux_encoder_compare(rdpMuxEncoder* encoder, BYTE* pData1, int nStep1,
		int nWidth, int nHeight, BYTE* pData2, int nStep2, RECTANGLE_16* rect);

/**
 * @brief Creates a new encoder.
 *
 * @param settings Library settings struct.
 *
 * @returns Pointer to newly made encoder.
 */
rdpMuxEncoder* rdpmux_encoder_new(rdpSettings* settings);

/**
 * @brief Frees the encoder.
 *
 * @param encoder Encoder to free.
 */
void rdpmux_encoder_free(rdpMuxEncoder* encoder);

#ifdef __cplusplus
}
#endif

#endif /* RDPMUX_ENCODER_H */
