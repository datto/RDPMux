/** @file */
#include "queue.h"

/**
 * @brief Checks if the queue is empty.
 *
 * @returns Whether the queue is empty.
 * @param q The queue to check.
 */
bool mux_queue_check_is_empty(MuxMsgQueue *q)
{
    return SIMPLEQ_EMPTY(&q->updates);
}

/**
 * @brief Dequeues an update.
 *
 * @returns Pointer to the update.
 * @param q The queue to dequeue from.
 */
void *mux_queue_dequeue(MuxMsgQueue *q)
{
    void *ret;
    // take lock on the mutex
    pthread_mutex_lock(&q->lock);
    // while the queue is empty, wait
    while (mux_queue_check_is_empty(q)) {
        pthread_cond_wait(&q->cond, &q->lock);
    }
    // remove the head of the queue
    ret = (void *) SIMPLEQ_FIRST(&q->updates);
    SIMPLEQ_REMOVE_HEAD(&q->updates, next);
    // unlock
    pthread_mutex_unlock(&q->lock);
    // return value
    return ret;
}

/**
 * @brief Enqueues an update.
 *
 * @param q The queue to stick the update on.
 * @param update The update to stick on the queue.
 */
void mux_queue_enqueue(MuxMsgQueue *q, MuxUpdate *update)
{
    pthread_mutex_lock(&q->lock);
    SIMPLEQ_INSERT_TAIL(&q->updates, update, next);
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
}

/**
 * @brief Clears a queue of all updates.
 *
 * @param q The queue to clear.
 */
void mux_queue_clear(MuxMsgQueue *q)
{
    MuxUpdate *update;
    pthread_mutex_lock(&q->lock);
    while ((update = SIMPLEQ_FIRST(&q->updates)) != NULL) {
        SIMPLEQ_REMOVE(&q->updates, update, MuxUpdate, next);
        g_free(update);
    }
    pthread_mutex_unlock(&q->lock);
}
