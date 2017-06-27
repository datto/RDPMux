/** @file */
#include <pixman.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "common.h"
#include "msgpack.h"
#include "0mq.h"

InputEventCallbacks callbacks;
MuxDisplay *display;

/**
 * @func Checks whether the bounding box of the display update needs to be expanded, and does so if necessary.
 *
 * This function is called when more than one display update event is received per refresh tick. It computes the smallest
 * bounding box of the display buffer that contains all the updated screen regions received thus far, and updates the
 * display update with the new coordinates.
 *
 * @param update The update object to update if necessary. Should be verified to be of type DISPLAY_UPDATE by caller.
 * @param x The X-coordinate of the top-left corner of the new region to be included in the update.
 * @param y The Y-coordinate of the top-left corner of the new region to be included in the update.
 * @param w The width of the new region to be included in the update.
 * @param h The height of the new region to be included in the update.
 */
static void mux_expand_rect(MuxUpdate *update, int x, int y, int w, int h)
{
    if (update->type != DISPLAY_UPDATE)
        return;

    display_update *u = &update->disp_update;

    int new_x1 = x;
    int new_y1 = y;
    int new_x2 = x+w;
    int new_y2 = y+h;

    u->x1 = MIN(u->x1, new_x1);
    u->y1 = MIN(u->y1, new_y1);
    u->x2 = MAX(u->x2, new_x2);
    u->y2 = MAX(u->y2, new_y2);
}

/**
 * @func Copies a pixel region from one buffer to another. The two buffers are assumed to have the same subpixel
 * layout and bpp. The function will transfer a given rectangle of certain dimension from the source buffer to
 * a rectangle in the destination buffer with the same width and height, but not necessarily the same coordinates.
 *
 * @param dstData Pointer to the destination buffer. Assumed to be big enough to hold the data being copied into it.
 * @param dstStep Scanline of dstData.
 * @param xDst x-coordinate of the top-left corner of the destination rectangle.
 * @param yDst y-coordinate of the top-left corner of the destination rectangle.
 * @param width width of the rectangle in px.
 * @param height height of the rectangle in px.
 * @param srcData Pointer to the source buffer.
 * @param srcStep Scanline of the source buffer.
 * @param xSrc x-coordinate of the top-left corner of the source rectangle.
 * @param ySrc y-coordinate of the top-left corner of the source rectangle.
 * @param bpp Bits per pixel of the two buffers.
 */
static void mux_copy_pixels(unsigned char *dstData, int dstStep, int xDst, int yDst, int width, int height,
                            unsigned char *srcData, int srcStep, int xSrc, int ySrc, int bpp)
{
    int lineSize;
    int pixelSize;
    unsigned char* pSrc;
    unsigned char* pDst;
    unsigned char* pEnd;

    pixelSize = (bpp + 7) / 8;
    lineSize = width * pixelSize;

    pSrc = &srcData[(ySrc * srcStep) + (xSrc * pixelSize)];
    pDst = &dstData[(yDst * dstStep) + (xDst * pixelSize)];


    // when the source and destination rectangles are both strips
    // of the framebuffer spanning the full width, it's much cheaper
    // to do one memcpy rather than going line-by-line.
    if ((srcStep == dstStep) && (lineSize == srcStep)) {
        memcpy(pDst, pSrc, lineSize * height);
    } else {
        pEnd = pSrc + (srcStep * height);

        while (pSrc < pEnd) {
            memcpy(pDst, pSrc, lineSize);
            pSrc += srcStep;
            pDst += dstStep;
        }
    }
}

/**
 * @func Public API function designed to be called when a region of the framebuffer changes. For example, when a window
 * moves or an animation updates on screen.
 *
 * The function accepts four parameters [(x, y) w x h] that together define the rectangular bounding box of the changed
 * region in pixels.
 *
 * @param x X coordinate of the top-left corner of the changed region.
 * @param y Y-coordinate of the top-left corner of the changed region.
 * @param w Width of the changed region, in px.
 * @param h Height of the changed region, in px.
 */
__PUBLIC void mux_display_update(int x, int y, int w, int h)
{
    mux_printf("DCL display update event triggered");
    MuxUpdate *update = &(display->dirty_update);
    if (update->type == MSGTYPE_INVALID) {
        update->type = DISPLAY_UPDATE;
        update->disp_update.x1 = x;
        update->disp_update.y1 = y;
        update->disp_update.x2 = x+w;
        update->disp_update.y2 = y+h;
    } else {
        // update dirty bounding box
        if (update->type != DISPLAY_UPDATE) {
            return;
        }
        mux_expand_rect(update, x, y, w, h);
    }

    mux_printf("Bounding box updated to [(%d, %d), (%d, %d)]", update->disp_update.x1, update->disp_update.x2,
               update->disp_update.x2, update->disp_update.y2);
}

/**
 * @func Public API function, to be called if the framebuffer surface changes in a user-facing way; for example, when the
 * display buffer resolution changes. In here, we create a new shared memory region for the framebuffer if necessary,
 * and do a straight memcpy of the new framebuffer data into the space. We then enqueue a display switch event that
 * contains the new shm region's information and the new dimensions of the display buffer. Finally, we notify the outside
 * about the new target framerate we'd like
 *
 * @param surface The new framebuffer display surface.
 *
 * @returns Target framerate for the VM guest.
 */
__PUBLIC void mux_display_switch(pixman_image_t *surface)
{
    mux_printf("DCL display switch event triggered.");

    // save the pointers in our display struct for further use.
    display->surface = surface;
    uint32_t *framebuf_data = pixman_image_get_data(display->surface);
    int width = pixman_image_get_width(display->surface);
    int height = pixman_image_get_height(display->surface);

    // do all sorts of stuff to get the shmem region opened and ready
    const char *socket_fmt = "/%d.rdpmux";
    char socket_str[20] = ""; // 20 is a magic number carefully chosen
                              // to be the length of INT_MAX plus the characters
                              // in socket_fmt. If you change socket_fmt, make
                              // sure to change this too.
    int shm_size = 4096 * 2048 * sizeof(uint32_t); // rdp protocol has max framebuffer size 4096x2048
    sprintf(socket_str, socket_fmt, display->vm_id);

    // set up the shm region. This path only runs the first time a display switch event is received.
    if (display->shmem_fd < 0) {
        // this is the shm buffer being created! Hooray!
        int shim_fd = shm_open(socket_str,
                               O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IRGRP | S_IROTH);
        if (shim_fd < 0) {
            mux_printf_error("shm_open failed: %s", strerror(errno));
            return;
        }

        // resize the newly created shm region to the size of the framebuffer
        if (ftruncate(shim_fd, shm_size)) {
            mux_printf_error("ftruncate of new buffer failed: %s", strerror(errno));
            return;
        }
        // save our new shm file descriptor for later use
        display->shmem_fd = shim_fd;

        // mmap the shm region into our process space
        void *shm_buffer = mmap(NULL, shm_size, PROT_READ | PROT_WRITE,
                                MAP_SHARED, display->shmem_fd, 0);
        if (shm_buffer == MAP_FAILED) {
            mux_printf_error("mmap failed: %s", strerror(errno));
            return;
        }

        // save the pointer to the buffer for later use
        display->shm_buffer = shm_buffer;
    }

    memcpy(display->shm_buffer, framebuf_data, width * height * sizeof(uint32_t));
    // create the event update

    MuxUpdate *update = &display->out_update;
    pthread_mutex_lock(&display->out_lock);
    //////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////
    //                     CRITICAL SECTION                            //
    ////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////
    update->type = DISPLAY_SWITCH;
    update->disp_switch.shm_fd = display->shmem_fd;
    update->disp_switch.w = width;
    update->disp_switch.h = height;
    update->disp_switch.format = pixman_image_get_format(display->surface);
    display->out_ready = true;
    //////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////
    //                 END CRITICAL SECTION                            //
    ////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////
    pthread_mutex_unlock(&display->out_lock);

    mux_printf("DISPLAY: DCL display switch callback completed successfully.");
}

/**
 * @func Public API function, to be called when the framebuffer display refreshes.
 *
 * This function attempts to lock the shared memory region, and if it succeeds, will sync the framebuffer
 * to the shared memory and copy the current dirty update for transmission.
 */
__PUBLIC uint32_t mux_display_refresh()
{
    if (display->dirty_update.type == DISPLAY_UPDATE) {
        int pixelSize;
        size_t x = 0;
        size_t y = 0;
        size_t w = 0;
        size_t h = 0;
        display_update *u = &(display->dirty_update.disp_update);
        size_t surfaceWidth = pixman_image_get_width(display->surface);
        size_t surfaceHeight = pixman_image_get_height(display->surface);
        int bpp = PIXMAN_FORMAT_BPP(pixman_image_get_format(display->surface));
        unsigned char *srcData = (unsigned char *) pixman_image_get_data(display->surface);
        unsigned char *dstData = (unsigned char *) display->shm_buffer;

        // align the bounding box to 16 for memory alignment purposes
        if (u->x1 % 16) {
            u->x1 -= (u->x1 % 16);
        }

        if (u->y1 % 16) {
            u->y1 -= (u->y1 % 16);
        }

        if (u->x2 % 16) {
            u->x2 += 16 - (u->x2 % 16);
        }

        if (u->y2 % 16) {
            u->y2 += 16 - (u->y2 % 16);
        }

        if (u->x2 > surfaceWidth) {
            u->x2 = surfaceWidth;
        }

        if (u->y2 > surfaceHeight) {
            u->y2 = surfaceHeight;
        }

        y = u->y1;
        h = u->y2 - u->y1;

        pixelSize = (bpp + 7) / 8;

        // aligning the copy offsets does not yield a good performance gain,
        // but copying contiguous memory blocks makes a huge difference.
        // by forcing copying of full lines on buffers with the same step,
        // we can use a single memcpy rather than one memcpy per line.
        // this may over-copy a bit sometimes, but it's still way cheaper.
        x = 0;
        w = surfaceWidth;

        mux_expand_rect(&display->dirty_update, u->x1, u->y1, u->x2 - u->x1, u->y2 - u->y1);

        if (pthread_mutex_trylock(&display->out_lock) == 0) {
            //////////////////////////////////////////////////////////////////////
            /////////////////////////////////////////////////////////////////////
            //                     CRITICAL SECTION                            //
            ////////////////////////////////////////////////////////////////////
            ////////////////////////////////////////////////////////////////////
            mux_copy_pixels(dstData, w * pixelSize, x, y, w, h, srcData, w * pixelSize, x, y, bpp);

            if (display->out_ready == false &&
                display->out_update.type == MSGTYPE_INVALID) { // we don't have another event queued
                display->out_update = display->dirty_update;
                display->out_ready = true;
                display->dirty_update.type = MSGTYPE_INVALID;
            }
            //////////////////////////////////////////////////////////////////////
            /////////////////////////////////////////////////////////////////////
            //                 END CRITICAL SECTION                            //
            ////////////////////////////////////////////////////////////////////
            ///////////////////////////////////////////////////////////////////
            pthread_mutex_unlock(&display->out_lock);
        }
    } else {
        mux_printf("Refresh deferred");
    }

    return (uint32_t) 30;
}

/*
 * Loops
 */

/**
 * @func Unused, stubbed out until formal removal.
 */
__PUBLIC void *mux_out_loop()
{
    return NULL;
}

/**
 * @func Unused, stubbed out until formal removal.
 *
 * @param arg Not at all used, just there to satisfy pthreads.
 */
__PUBLIC void *mux_display_buffer_update_loop(void *arg)
{
    return NULL;
}


static void mux_send_shutdown_msg()
{
    nnStr msg;
    msg.buf = NULL;
    size_t len = mux_write_outgoing_msg(NULL, &msg); // NULL means shutdown!
    while(mux_0mq_send_msg(msg.buf, len) < 0) {
        mux_printf_error("Failed to send shutdown message!");
    }
    g_free(msg.buf);
    mux_printf("Shutdown message sent!");
}

/**
 * @func This function manages communication to and from the library. It is designed to be a thread runloop, and should
 * be dispatched as a runnable inside a separate thread during library initialization. Its function prototype
 * matches what pthreads et al. expect.
 *
 * @param arg void pointer to anything. Not used by the function in any way, just there to satisfy pthreads.
 */
__PUBLIC void *mux_mainloop(void *arg)
{
    mux_printf("Reached qemu shim in loop thread!");
    void *buf = NULL;
    size_t len;
    zpoller_t *poller = display->zmq.poller;
    bool stopping = false;

    // main shim receive loop
    int nbytes;
    while(!stopping) {
        nnStr msg;
        msg.buf = NULL;
        buf = NULL;
        MuxUpdate out;
        bool ready = false;

        pthread_mutex_lock(&display->out_lock);
        //////////////////////////////////////////////////////////////////////
        /////////////////////////////////////////////////////////////////////
        //                     CRITICAL SECTION                            //
        ////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////
        ready = display->out_ready;
        if (ready) {
            mux_printf("Out update is ready, typed %d!", display->out_update.type);
            out = display->out_update;
            display->out_update.type = MSGTYPE_INVALID;
            display->out_ready = false;
        }
        //////////////////////////////////////////////////////////////////////
        /////////////////////////////////////////////////////////////////////
        //                 END CRITICAL SECTION                            //
        ////////////////////////////////////////////////////////////////////
        ///////////////////////////////////////////////////////////////////
        pthread_mutex_unlock(&display->out_lock);

        if (ready) {
            if (out.type != MSGTYPE_INVALID) {
                len = mux_write_outgoing_msg(&out, &msg);
                while (mux_0mq_send_msg(msg.buf, len) < 0)
                    mux_printf_error("Failed to send message");

                g_free(msg.buf);
                memset(&out, 0, sizeof(MuxUpdate));
            }
        }

        // block on receiving messages
        zsock_t *which = (zsock_t *) zpoller_wait(poller, 5); // 5ms timeout
        if (which != display->zmq.socket)  {
            if (zpoller_terminated(poller)) {
                mux_printf_error("Zpoller terminated!");
                stopping = true;
            }
        } else {
            nbytes = mux_0mq_recv_msg(&buf);
            if (nbytes > 0) {
                // successful recv is successful
                mux_process_incoming_msg(buf, nbytes);
            }
        }
    }

    // cleanup
    mux_printf("Cleaning up!");

    zpoller_destroy(&display->zmq.poller);
    mux_send_shutdown_msg();

    zsock_destroy(&display->zmq.socket);
    mux_printf("zsock_destroy has been called!");

    return NULL;
}

/**
 * @func This function initializes the data structures used by the library. It also returns a pointer to the ShimDisplay
 * struct initialized, which is defined as an opaque type in the public header so that client code can't mess with it.
 *
 * You must pass a string containing an UUID into the VM. This UUID will be used to uniquely identify the VM with the
 * frontend server, and will be passed in every message.
 *
 * @param uuid A UUID describing the VM.
 */
__PUBLIC MuxDisplay *mux_init_display_struct(const char *uuid)
{
    display = g_malloc0(sizeof(MuxDisplay));
    display->shmem_fd = -1;
    display->uuid = NULL;
    display->zmq.socket = NULL;
    display->framerate = 30;

    if (uuid != NULL) {
        if (strlen(uuid) != 36) {
            mux_printf_error("Invalid UUID");
            free(display);
            return NULL;
        }
        if ((display->uuid = strdup(uuid)) == NULL) {
            mux_printf_error("String copy failed: %s", strerror(errno));
            free(display);
            return NULL;
        }
    } else {
        display->uuid = NULL;
    }

    pthread_mutex_init(&display->out_lock, NULL);

    return display;
}

/**
 * @func Register mouse and keyboard event callbacks using this function. The function pointers you register will be
 * called when mouse and keyboard events are received for you to handle and process.
 */
__PUBLIC void mux_register_event_callbacks(InputEventCallbacks cb)
{
    callbacks = cb;
}

/**
 * @func Should be called to safely cleanup library state. Note that ZeroMQ threads may (will) hang around forever
 * unless they're cleaned up by this method.
 */
__PUBLIC void mux_cleanup(MuxDisplay *d)
{
}
