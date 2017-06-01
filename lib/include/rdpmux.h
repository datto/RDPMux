#ifndef SHIM_EXTERNAL_H
#define SHIM_EXTERNAL_H

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <glib.h>
#include <pixman.h>

typedef struct InputEventCallbacks {
    void (*mux_receive_kb)(uint32_t keycode, uint32_t flags);
    void (*mux_receive_mouse)(uint32_t x, uint32_t y, uint32_t flags);
} InputEventCallbacks;

typedef struct mux_display MuxDisplay;

void mux_display_update(int x, int y, int w, int h);
void mux_display_switch(pixman_image_t *surface);
uint32_t mux_display_refresh();

void *mux_mainloop(void *arg);
void mux_out_loop();
void *mux_display_buffer_update_loop(void *arg);

void mux_register_event_callbacks(InputEventCallbacks cb);
MuxDisplay *mux_init_display_struct(const char *uuid);
bool mux_connect(const char *path);
bool mux_get_socket_path(const char *name, const char *obj, char **out_path, int id);
void mux_cleanup(MuxDisplay *display);

#endif //SHIM_EXTERNAL_H
