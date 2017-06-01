/** @file */

#ifndef SHIM_COMMON_H
#define SHIM_COMMON_H

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <czmq.h>

#include <glib.h>
#include <pixman.h>

#include "lib/libqueue.h"
#include "lib/c-msgpack.h"

/**
 * @brief Used to define publicly available functions.
 */
#define __PUBLIC __attribute__((visibility("default")))

/**
 * @brief Protocol version.
 */
#define RDPMUX_PROTOCOL_VERSION 3

/**
 * @brief debug output macro
 */
#ifdef USE_DEBUG_OUTPUT
#define mux_printf(x, ...) fprintf(stdout, "DEBUG:   %s:%d: " x "\n", \
                            __func__, __LINE__, ##__VA_ARGS__);
#else
#define mux_printf(x, ...) ;
#endif

/**
 * @brief error output macro
 */
#define mux_printf_error(x, ...) fprintf(stderr, "ERROR:   %s:%d: " x "\n", \
                            __func__, __LINE__, ##__VA_ARGS__);

/**
 * @brief This struct is populated by the code using the library to provide callbacks for mouse and keyboard events.
 *
 * This struct is also exposed in the public header. The implementing code (usually the hypervisor) needs to provide
 * functions to deal with these events and register them into the library using mux_register_event_callbacks().
 */
typedef struct InputEventCallbacks {
    void (*mux_receive_kb)(uint32_t keycode, uint32_t flags);
    void (*mux_receive_mouse)(uint32_t x, uint32_t y, uint32_t flags);
} InputEventCallbacks;

/**
 * @brief The possible types of messages.
 */
typedef enum message_type {
    DISPLAY_UPDATE,
    DISPLAY_SWITCH,
    MOUSE,
    KEYBOARD,
    DISPLAY_UPDATE_COMPLETE,
    SHUTDOWN
} MessageType;

/**
 * @brief Parameters for a display update event.
 *
 * Display updates carry coordinates for a rectangular screen region, denoted as the coordinates of the top left corner
 * and the coordinates of the bottom right corner. All values are in px.
 */
typedef struct display_update {
    /**
     * @brief X-coordinate of top left corner of region
     */
    int x1;
    /**
     * @brief Y-coordinate of top left corner of region
     */
    int y1;
    /**
     * @brief X-coordinate of bottom right corner of region
     */
    int x2;
    /**
     * @brief X-coordinate of bottom right corner of region
     */
    int y2;
} display_update;

/**
 * @brief Parameters for a display switch event.
 */
typedef struct display_switch {
    /**
     * @brief file descriptor for the shared memory region.
     */
    int shm_fd;
    /**
     * @brief format code describing the pixel layout of the framebuffer.
     */
    pixman_format_code_t format;
    /**
     * @brief width of framebuffer in px.
     */
    int w;
    /**
     * @brief height of framebuffer in px.
     */
    int h;
} display_switch;

/**
 * @brief Parameters for a keyboard event.
 */
typedef struct kb_update {
    /**
     * @brief flags associated with the event. Could be anything, really. Commonly used for things like keyup/keydown
     * events and any special flags associated with the keypress.
     */
    uint32_t flags;
    /**
     * @brief The keycode. Could be anything, depends on what the client side application sends.
     */
    uint32_t keycode;
} kb_update;

/**
 * @brief Parameters for a mouse event.
 */
typedef struct mouse_update {
    /**
     * @brief flags associated with the mouse update event. Usually used to denote extended buttons and/or what type
     * of mouse event it is (movement, click, etc.)
     */
    uint32_t flags;
    /**
     * @brief X-coordinate of the mouse cursor in px.
     */
    uint32_t x;
    /**
     * @brief Y-coordinate of the mouse cursor in px.
     */
    uint32_t y;
} mouse_update;

/**
 * @brief Parameters for a update ack event.
 */
typedef struct update_ack {
    /**
     * @brief whether the update succeeded.
     */
    bool success;
    /**
     * @brief new target framerate for the guest.
     */
    uint32_t framerate;
} update_ack;

/**
 * @brief Parameters for a shutdown event.
 */
typedef struct shut_down {
    bool shutting_down;
} shut_down;

/**
 * @brief Object to hold information about the various types of events.
 *
 * The MuxUpdate struct can can hold exactly one of one type of event. They are designed to be held in queues.
 * Use the type field to determine what sort of event it is, rather than any other sort of hackery.
 */
typedef struct MuxUpdate {
    /**
     * @brief pointer to next update in queue.
     */
    SIMPLEQ_ENTRY(MuxUpdate) next;
    /**
     * @brief what kind of update this is.
     */
    MessageType type;
    /**
     * @brief union of the various event types.
     */
    union {
        display_update disp_update;
        display_switch disp_switch;
        kb_update kb;
        mouse_update mouse;
        update_ack ack;
        shut_down shutdown;
    };
} MuxUpdate;

/**
 * @brief queue to hold ShimUpdate objects.
 *
 * This is a very simple queue backed by a linked list. Nothing fancy, gets the job done.
 */
typedef struct MuxMsgQueue {
    pthread_mutex_t lock;
    pthread_cond_t cond; // signals when not empty
    SIMPLEQ_HEAD(, MuxUpdate) updates;
} MuxMsgQueue;

/**
 * @brief Main struct
 *
 * This struct holds all of the state in the library. There is exactly one instance of this, initialized by
 * mux_init_display_struct() and stored as a global variable.
 */
struct mux_display {
    /**
     * @brief pointer to the QEMU framebuffer surface.
     */
    pixman_image_t *surface;

    /**
     * @brief Internal ID of the virtual machine
     */
    int vm_id;
    /**
     * @brief File descriptor of the shared memory region.
     */
    int shmem_fd;
    /**
     * @brief pointer to the shared memory region.
     */
    void *shm_buffer;
    /**
     * @brief Current dirty update
     */
    MuxUpdate *dirty_update;
    /**
     * @brief Current outgoing update
     */
    MuxUpdate *out_update;

    struct {
        zsock_t *socket;
        zpoller_t *poller;
        const char *path;
    } zmq;

    /**
     * @brief Externally passed UUID of the VM.
     */
    const char *uuid;

    /**
     * @brief current framerate target of the VM guest. Comes from the server.
     */
    uint32_t framerate;

    /**
     * @brief Condition variable associated with shared memory region.
     */
    pthread_cond_t shm_cond;
    /**
     * @brief Lock guarding access to shared memory region.
     */
    pthread_mutex_t shm_lock;

    /**
     * @brief Condition variable associated with outgoing update.
     */
    pthread_cond_t update_cond;

    /**
     * @brief Lock guarding access to the stop variable.
     */
    pthread_mutex_t stop_lock;
    bool stop;

    /**
     * @brief Outgoing message queue.
     */
    MuxMsgQueue outgoing_messages;
};
typedef struct mux_display MuxDisplay;

/**
 * @brief global variable of the registered EventCallbacks.
 */
extern InputEventCallbacks callbacks;

/**
 * @brief Global display struct.
 */
extern MuxDisplay *display;

#endif //SHIM_COMMON_H
