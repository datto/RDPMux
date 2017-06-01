//
// Created by sramanujam on 2/4/16.
//

#ifndef SHIM_QUEUE_H
#define SHIM_QUEUE_H

#include "common.h"
#include "lib/libqueue.h"

void *mux_queue_dequeue(MuxMsgQueue *q);
void mux_queue_enqueue(MuxMsgQueue *q, MuxUpdate *update);
void mux_queue_clear(MuxMsgQueue *q);
bool mux_queue_check_is_empty(MuxMsgQueue *q);


#endif //SHIM_QUEUE_H
