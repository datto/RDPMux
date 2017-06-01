//
// Created by sramanujam on 2/3/16.
//

#ifndef SHIM_NANOMSG_H
#define SHIM_NANOMSG_H

#include "common.h"

int mux_0mq_recv_msg(void **buf);
int mux_0mq_send_msg(void *buf, size_t len);
bool mux_connect(const char *path);

#endif //SHIM_NANOMSG_H
