/*
 * Copyright 2016 Datto Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef QEMU_RDP_MESSAGEQUEUE_H
#define QEMU_RDP_MESSAGEQUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>

#include <glibmm/threads.h>
#include <bits/unique_ptr.h>
#include <nanomsg/nn.h>

/**
 * @brief Object to hold a message in a queue until it is processed.
 */
class QueueItem
{
public:
    /**
     * @brief Creates a new QueueItem.
     *
     * @param i The message to hold.
     * @param size The size of the message.
     */
    QueueItem(void *i, int size) {
        item_size = size;
        item = i;
    }

    /**
     * @brief Frees the held message safely.
     */
    ~QueueItem() {
        nn_freemsg(item);
    };

    /**
     * @brief The held message.
     */
    void *item;

    /**
     * @brief The size of item.
     */
    int item_size;
};

/**
 * @brief A synchronized FIFO queue backed by an std::queue to hold messages for processing.
 */
class MessageQueue
{
public:
    MessageQueue() {};
    ~MessageQueue() {};

    /**
     * @brief Checks if the queue is empty.
     *
     * @returns Whether the queue is empty.
     */
    bool isEmpty() {
        return queue_.empty();
    }

    /**
     * @brief Enqueues an item on the queue
     *
     * @param item The item to place in the queue.
     */
    void enqueue(QueueItem *item);

    /**
     * @brief Dequeue the next item in the queue.
     *
     * @returns Reference to the dequeued item.
     */
    const QueueItem* dequeue();

private:
    std::mutex mutex_;
    std::condition_variable cond_push_;
    std::queue<QueueItem *> queue_;
};

#endif //QEMU_RDP_MESSAGEQUEUE_H
