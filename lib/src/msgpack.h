//
// Created by sramanujam on 2/3/16.
//

#ifndef SHIM_MSGPACK_H
#define SHIM_MSGPACK_H

#include <malloc.h>

#include "common.h"
#include "lib/c-msgpack.h"

typedef struct nn_str {
    void *buf; // pointer to char data
    size_t size; // size of data buffer in bytes
    int pos; // current read position in buffer
} nnStr;

size_t mux_write_outgoing_msg(MuxUpdate *update, nnStr *msg);
void mux_process_incoming_msg(void *buf, int nbytes);

#endif //SHIM_MSGPACK_H
