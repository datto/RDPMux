/** @file */
#include "msgpack.h"

/**
 * @brief Initializes a new nnStr struct.
 *
 * The nnStr struct basically wraps a byte buffer in an iterator. We use this to aid in the serialization and
 * de-serialization of msgpack-encoded information. This function takes in a byte buffer and the size of that byte buffer,
 * creates a new nnStr object around them, and places the output in msg.
 *
 * You can also use this function to reset an nnStr's iterator, if you like.
 *
 * @param msg A pointer to an nnStr struct.
 * @param buf Pointer to the byte buffer you want to be stored in here.
 * @param size The size in bytes of the byte buffer.
 */
static void mux_nnstr_init(nnStr *msg, void *buf, int size)
{
    msg->size = size;
    msg->buf = buf;
    msg->pos = 0;
    return;
}

/**
 * @brief Write some serialized data to the internal string buffer.
 *
 * This function is passed to the c-msgpack library to be used as its write() function. It transparently handles
 * re-allocing the internal buffer in case it runs of space, and updating the iterator to point to the end of the buffer
 * in memory.
 *
 * @returns Number of bytes written
 *
 * @param ctx The cmp struct that called the function
 * @param data The data to be written
 * @param count The size of data in bytes
 */
static size_t mux_msg_writer(cmp_ctx_t *ctx, const void *data, size_t count)
{
    nnStr *msg = (nnStr *) ctx->buf;

    if (msg->buf == NULL) {
        // if buf needs to be newly allocated to hold our serialization
        // This typically happens when we start creating our serialized data
        // and this is the first invocation of mux_msg_writer.
        mux_printf("Allocating new buffer");
        void *new_buf = g_malloc0(count * sizeof(uint8_t));
        if (new_buf) {
            msg->buf = new_buf;
            msg->size = malloc_usable_size(new_buf); // I have ruined portability forever.
        } else {
            mux_printf_error("Cannot allocate buffer");
            return 0;
        }
    } else {
        if ((msg->pos + count) > msg->size) {
            // we need to realloc the size of our string
            // g_realloc() will abort on error, but this is still good practice
            // in case the implementation changes or we change to a different
            // realloc later on.
            void *new_buf = g_realloc(msg->buf, msg->size * 2); // more efficient
            if (new_buf) {
                msg->buf = new_buf;
                msg->size = msg->size * 2;
            } else {
                // error reallocing, return null without changing the original string
                mux_printf_error("Error reallocing buffer, returning 0");
                return 0;
            }
        }
    }
    uint8_t *serialized = (uint8_t *) msg->buf;
    uint8_t *begin = serialized + msg->pos;

    memcpy(begin, data, count);
    msg->pos += count;
    return count;
}

/**
 * @brief Read some serialized data out of the internal buffer
 *
 * This function is passed to the c-msgpack library to serve as its read() function. It transparently handles updating
 * the iterator and bounds-checking. It reads up to limit number of bytes from the internal cmp buffer, and places them
 * into data.
 *
 * @returns Whether the read succeeded.
 *
 * @param ctx The cmp struct that called the function.
 * @param data The data that was read out.
 * @param limit The maximum number of bytes to read.
 */
static bool mux_msg_reader(cmp_ctx_t *ctx, void *data, size_t limit)
{
    // reads size_t bytes of data out of ctx->buf.
    nnStr *msg = (nnStr *) ctx->buf;

    // check if we have overextended our bounds :(
    if ((msg->pos + limit) > msg->size) {
        return false;
    }

    uint8_t *serialized = (uint8_t *) msg->buf;
    uint8_t *begin = serialized + msg->pos;

    memcpy(data, begin, limit); // since we treat everything as bytes, i think this works.
    msg->pos += limit;
    return true;
}

/**
 * @brief Deserializes keyboard messages and fires the mux_receive_kb() callback with the data.
 *
 * Keyboard messages are encoded as a two-item msgpack array of two uint32_ts, keycode at index 0, flags at index 1.
 *
 * @param cmp The cmp struct that holds the serialized msgpack buffer
 * @param msg Unused, here for historical reasons.
 * @todo Remove msg parameter, unused
 */
static void mux_process_incoming_kb_msg(cmp_ctx_t *cmp, nnStr *msg)
{
    uint32_t flags, keycode;

    if (!cmp_read_uint(cmp, &keycode)) {
        mux_printf_error("flags wasn't read properly");
        return;
    }

    if (!cmp_read_uint(cmp, &flags)) {
        mux_printf_error("keycode wasn't read properly");
        return;
    }

    callbacks.mux_receive_kb(keycode, flags);
}

/**
 * @brief Deserializes mouse messages and fires the mux_receive_mouse() callback with the decoded data.
 *
 * Mouse messages are encoded as a 3-item msgpack array of uint32_ts, ordered as such: mouse_x, mouse_y, flags.
 *
 * @param cmp The cmp struct that holds the serialized msgpack buffer.
 * @param msg Unused, here for historical reasons.
 * @todo Remove msg parameter, unused
 */
static void mux_process_incoming_mouse_msg(cmp_ctx_t *cmp, nnStr *msg)
{
    uint32_t flags, mouse_x, mouse_y;

    if (!cmp_read_uint(cmp, &mouse_x)) {
        mux_printf_error("mouse_x wasn't read properly");
        return;
    }

    if (!cmp_read_uint(cmp, &mouse_y)) {
        mux_printf_error("mouse_y wasn't read properly");
        return;
    }

    if (!cmp_read_uint(cmp, &flags)) {
        mux_printf_error("flags uint wasn't read properly");
        return;
    }

    callbacks.mux_receive_mouse(mouse_x, mouse_y, flags);
}

static void mux_process_incoming_complete_msg(cmp_ctx_t *cmp, nnStr *msg)
{
    uint32_t new_framerate, success;

    if (!cmp_read_uint(cmp, &success)) {
        mux_printf_error("success variable didn't work");
        return;
    }

    if (success != 1) {
        mux_printf_error("Unsuccessful update_complete");
        return;
    }

    if (!cmp_read_uint(cmp, &new_framerate)) {
        mux_printf_error("couldn't read framerate");
        return;
    }

    display->framerate = new_framerate;
}

/**
 * @brief Serializes incoming raw data into cmp struct for processing and invokes correct deserialization function
 * for type of message received.
 *
 * @param buf The raw data to be wrapped in a cmp decoding struct.
 * @param nbytes The size of buf.
 */
void mux_process_incoming_msg(void *buf, int nbytes)
{
    // deserialize msg into component parts
    cmp_ctx_t cmp;
    uint32_t msg_type, array_size;

    nnStr msg;
    mux_nnstr_init(&msg, buf, nbytes);
    cmp_init(&cmp, &msg, mux_msg_reader, mux_msg_writer);

//    mux_printf("Now deserializing msgpack array!");

    // read array out
    // we don't care about array size since we have a better way (the type)
    // of checking what the message is.
    if (!cmp_read_array(&cmp, &array_size)) {
        return;
    }

    if (!cmp_read_uint(&cmp, &msg_type)) {
        return;
    }

    switch(msg_type) {
        case MOUSE:
            mux_printf("Processing incoming mouse msg");
            mux_process_incoming_mouse_msg(&cmp, &msg);
            break;
        case KEYBOARD:
            mux_printf("Processing incoming kb msg");
            mux_process_incoming_kb_msg(&cmp, &msg);
            break;
        case DISPLAY_UPDATE_COMPLETE:
            mux_printf("Signaling shm_cond for DISPLAY_UPDATE_COMPLETE wakeup");
            mux_process_incoming_complete_msg(&cmp, &msg);
            pthread_cond_signal(&display->shm_cond);
            break;
        default:
            mux_printf_error("Invalid message type");
            break;
    }
    // clean up msg
    free(msg.buf);
    return;
}

/**
 * @brief Serializes a display update event to a msgpack message.
 *
 * @param cmp The cmp struct that holds the write buffer.
 * @param update The update to serialize.
 */
static void mux_write_outgoing_update_msg(cmp_ctx_t *cmp, MuxUpdate *update)
{
    display_update u = update->disp_update;

    if (!cmp_write_array(cmp, 5))
        mux_printf_error("Something went wrong writing array specifier");

    if (!cmp_write_uint(cmp, update->type))
        mux_printf_error("Something went wrong writing update type");

    if (!cmp_write_uint(cmp, u.x1))
        mux_printf_error("Something went wrong writing x");

    if (!cmp_write_uint(cmp, u.y1))
        mux_printf_error("Something went wrong writing y");

    if (!cmp_write_uint(cmp, (u.x2 - u.x1)))
        mux_printf_error("Something went wrong writing w");

    if (!cmp_write_uint(cmp, (u.y2 - u.y1)))
        mux_printf_error("Something went wrong writing h");
}

/**
 * @brief Serializes a display switch event to a msgpack message.
 *
 * @param cmp The cmp struct that holds the write buffer.
 * @param update The update to serialize.
 */
static void mux_write_outgoing_switch_msg(cmp_ctx_t *cmp, MuxUpdate *update)
{
    display_switch u = update->disp_switch;

    if (!cmp_write_array(cmp, 4))
        mux_printf_error("Something went wrong writing array specifier");

    if (!cmp_write_uint(cmp, update->type))
        mux_printf_error("Something went wrong writing update type");

    if (!cmp_write_uint(cmp, u.format))
        mux_printf_error("Something went wrong writing format");

    if (!cmp_write_uint(cmp, u.w))
        mux_printf_error("Something went wrong writing w");

    if (!cmp_write_uint(cmp, u.h))
        mux_printf_error("Something went wrong writing h");
}

static void mux_write_outgoing_shutdown_msg(cmp_ctx_t *cmp)
{
    if (!cmp_write_array(cmp, 1))
        mux_printf_error("Something went wrong writing array specifier");

    if (!cmp_write_uint(cmp, SHUTDOWN))
        mux_printf_error("Something went wrong writing update type");
}

/**
 * @brief Writes an outgoing event to a msgpack-encoded message.
 *
 * @returns Size of successfully written data in bytes.
 *
 * @param update The update to serialize.
 * @param msg The blank nnStr message to write the message to.
 */
size_t mux_write_outgoing_msg(MuxUpdate *update, nnStr *msg)
{
    // takes a struct and serializes it to a msgpack message.
    //printf("LIBSHIM: Writing a new message now!");
    cmp_ctx_t cmp;
    //nnStr msg;
    mux_nnstr_init(msg, msg->buf, 0);
    cmp_init(&cmp, msg, mux_msg_reader, mux_msg_writer);

    if (update == NULL) {
        mux_write_outgoing_shutdown_msg(&cmp);
        return msg->size;
    }

    if (update->type == DISPLAY_UPDATE) {
        mux_write_outgoing_update_msg(&cmp, update);
    } else if (update->type == DISPLAY_SWITCH) {
        mux_write_outgoing_switch_msg(&cmp, update);
    } else {
        mux_printf_error("Unknown message type queued for writing!");
    }

    size_t len = msg->size;
    return len;
}

