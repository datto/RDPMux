/** @file */
#include "0mq.h"
#include "common.h"

/**
 * @brief Receives data through the given 0mq socket and sticks it into buf.
 *
 * This function is blocking.
 *
 * @returns Number of bytes read.
 *
 * @param buf Pointer to a void* buffer.
 */
int mux_0mq_recv_msg(void **buf)
{
    zmsg_t *msg = NULL;
    zframe_t *identity = NULL;
    zframe_t *data = NULL;
    int len = -1;

    mux_printf("Now blocking on recv");

    if ((msg = zmsg_recv(display->zmq.socket)) == NULL) {
        mux_printf_error("Could not receive message from socket!");
        return -1;
    }

    //zmsg_print(msg);
    identity = zmsg_pop(msg);
    //zframe_print(identity, "F: ");

    if (!zframe_streq(identity, display->uuid)) {
        char *wrong = zframe_strdup(identity);
        mux_printf_error("Incorrect UUID: %s", wrong);
        free(wrong);
        zframe_destroy(&identity);
        return -1;
    }

    data = zmsg_pop(msg);
    //zframe_print(data, "F: ");
    len = zframe_size(data);
    *buf = calloc(1, zframe_size(data) + 1);
    memcpy(*buf, zframe_data(data), zframe_size(data));

    zframe_destroy(&identity);
    zframe_destroy(&data);

    return len;
}

/**
 * @brief Send a message through the 0mq socket.
 *
 * This function is blocking.
 *
 * @returns The number of bytes sent.
 *
 * @param buf The data to send.
 * @param len The length of buf.
 */
int mux_0mq_send_msg(void *buf, size_t len)
{
    zmsg_t *msg = zmsg_new();

    mux_printf("Now attempting to send message!");
    zmsg_addstr(msg, display->uuid);
    zmsg_addmem(msg, buf, len);

    if (zmsg_size(msg) != 2) {
        mux_printf_error("Something went wrong building zmsg");
        return -1;
    }

    zmsg_send(&msg, display->zmq.socket); // TODO: figure out how return code works for this

    return len;
}


static void mux_handler(int signal_value)
{
    mux_printf("ZSYS signal handler called");
    zctx_interrupted = 1;
    zsys_interrupted = 1;
}

/**
 * @brief Connects to the 0mq socket on path.
 *
 * Connects to the 0mq socket located on the file path passed in, then stores that socket in the global display struct
 * upon success.
 *
 * @returns Whether the connection succeeded.
 *
 * @param path The path to the 0mq socket in the filesystem.
 */
__PUBLIC bool mux_connect(const char *path)
{
    display->zmq.path = path;
    display->zmq.socket = zsock_new_dealer(path);
    zsys_handler_set(mux_handler);
    if (display->zmq.socket == NULL) {
        mux_printf_error("0mq socket creation failed");
        return false;
    }

    if (zsock_connect(display->zmq.socket, "%s", path) == -1) {
        mux_printf_error("0mq connect failed");
        return false;
    }
    mux_printf("Bound to %s", path);

    display->zmq.poller = zpoller_new(display->zmq.socket, NULL);
    if (display->zmq.poller == NULL) {
        mux_printf_error("Could not initialize socket poller");
        return false;
    }

    return true;
}
