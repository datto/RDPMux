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

#include "util/MessageQueue.h"

void MessageQueue::enqueue(QueueItem item)
{
    std::unique_lock<std::mutex> lock(mutex_);
    queue_.push(item);
    lock.unlock();
    cond_push_.notify_one();
}

// should block until a message is waiting in the queue,
// so it should be okay to put this in a while loop or something.
const QueueItem MessageQueue::dequeue()
{
    std::unique_lock<std::mutex> lock(mutex_);

    while(queue_.empty()) {
        cond_push_.wait(lock);
    }

    auto item = queue_.front();
    queue_.pop();
    return item;
}
