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

#include <czmq.h>
#include <msgpack/object.hpp>
#include <msgpack/unpack.hpp>
#include "RDPServerWorker.h"

RDPServerWorker::RDPServerWorker(uint16_t port, std::string socket_path)
        : starting_port(port),
          initialized(false),
          socket_path(socket_path),
          socket(nullptr)
{
    socket = zsock_new(ZMQ_ROUTER);

    if (socket == nullptr || socket == NULL) {
        throw new std::runtime_error("Socket creation failed!");
    }

    std::string path = "ipc://" + this->socket_path;

    if (zsock_bind((zsock_t *) socket, path.c_str()) != 0) {
        throw new std::runtime_error("Socket binding failed!");
    }
}

RDPServerWorker::~RDPServerWorker()
{
        Glib::Mutex::Lock lock(stop_mutex);
        stop = true;
}

void RDPServerWorker::setDBusConnection(Glib::RefPtr<Gio::DBus::Connection> conn)
{
    if (!dbus_conn) {
        dbus_conn = conn;
    } else {
        LOG(WARNING) << "Duplicate DBus connection passed in! SHOULD NOT HAPPEN";
    }
}

bool RDPServerWorker::Initialize()
{
    std::thread loop(&RDPServerWorker::run);
    loop.detach();
    initialized = true;
    return initialized;
}

bool RDPServerWorker::RegisterNewVM(std::string uuid)
{
    // find the next available port from the list
    // stdlib already uses binary search, so if this gets slow for you, consider not running as many VMs on the same
    // computer
    std::lock_guard<std::mutex> lock(container_lock); // take lock on both ports and listener_map
    uint16_t port = 0;
    std::shared_ptr<RDPListener> l;

    for (uint16_t i = starting_port; i < 65535; i++) {
        if (ports.find(i + 1) == ports.end()) {
            port = i;
            ports.insert(i);
            break;
        }
    }

    if (port == 0 || port > 65535) {
        LOG(WARNING) << "Invalid server port number: " << port;
        return false;
    }

    try {
        l = std::make_shared<RDPListener>(port, this); // in here go the RDPListener args
        l->RunServer();
    } catch (std::exception &e) {
        return false;
    }

    listener_map.insert(std::make_pair(uuid, l));

    return true;
}

void RDPServerWorker::UnregisterVM(std::string uuid, uint16_t port)
{
    std::lock_guard<std::mutex> lock(container_lock);
    ports.erase(port);
    listener_map.erase(uuid); // rip listener
}

void RDPServerWorker::sendMessage(msgpack::sbuffer sbuf, std::string uuid)
{
    char uuid_c[uuid.size()];
    uuid.copy(uuid_c, uuid.size());
    zframe_t *identity_rep_frame = zframe_new(uuid_c, uuid.size());
    zframe_t *data_rep_frame = zframe_new(sbuf.data(), sbuf.size());
    if (zframe_send(&identity_rep_frame, socket, ZFRAME_MORE) == -1) {
        LOG(ERROR) << "Unable to send identity frame";
        zframe_destroy(&identity_rep_frame);
        zframe_destroy(&data_rep_frame);
        return;
    }

    if (zframe_send(&data_rep_frame, socket, 0) == -1) {
        LOG(ERROR) << "Unable to send data frame";
        zframe_destroy(&data_rep_frame);
        return;
    }
}

void RDPServerWorker::queueOutgoingMessage(QueueItem item)
{
    out_queue.enqueue(std::move(item));
}

void RDPServerWorker::run()
{
    // main QEMU recv loop.
    zmsg_t *msg = nullptr;
    zframe_t *identity = nullptr;
    zframe_t *data = nullptr;
    zpoller_t *poller = zpoller_new(socket);
    while (true) {
        void *ready;

        while((ready = zpoller_wait(poller, 10)) != NULL) { // todo: don't know what a reasonable number would be?
            // check if we are terminating
            Glib::Mutex::Lock lock(stop_mutex);
            if (stop) {
                LOG(INFO) << "ServerWorker loop terminating on stop";
                if (zpoller_remove(poller, socket) < 0) {
                    LOG(ERROR) << "Error removing socket from poller!"; // not sure what to do about this
                }
                zpoller_destroy(&poller);
                initialized = false;
                return;
            }

            // send outgoing messages
            while(!out_queue.isEmpty()) {
                QueueItem item = std::move(out_queue.dequeue());
                auto sbuf_ptr = std::move(std::get<0>(item));
                sendMessage(*sbuf_ptr.get(), std::get<1>(item));
            }
        }

        msg = zmsg_recv(ready); // blocking

        identity = zmsg_pop(msg);
        if (identity == NULL) {
            LOG(WARNING) << "Malformed identity frame from client, ignoring";
            continue;
        }
        data = zmsg_pop(msg);
        if (data == NULL) {
            LOG(WARNING) << "Malformed message frame from client, ignoring";
            continue;
        }

        // we have an incoming message, unpack and route it
        std::string uuid((char *) zframe_data(identity));

        msgpack::unpacked unpacked;
        msgpack::unpack(&unpacked, (char *) zframe_data(data), (size_t) zframe_size(data));

        // clean up zmsg and zframes, as they are no longer needed
        zframe_destroy(&identity);
        identity = nullptr;
        zframe_destroy(&data);
        data = nullptr;
        zmsg_destroy(&msg);
        msg = nullptr;

        // deserialize msgpack message and pass to correct listener
        try {
            auto server = listener_map.at(uuid);
            msgpack::object obj = unpacked.get();
            std::vector<uint32_t> vec;
            obj.convert(&vec);
            server->processIncomingMessage(vec);
        } catch (std::out_of_range &e) {
            LOG(ERROR) << "Listener with UUID " << uuid << " does not exist in map!";
        } catch (std::exception &e) {
            LOG(ERROR) << "Msgpack conversion failed: " << e.what();
            LOG(ERROR) << "Offending buffer is " << unpacked.get();
        }
    }
}