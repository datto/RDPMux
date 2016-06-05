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

#include <msgpack/object.hpp>
#include <msgpack/unpack.hpp>
#include "RDPServerWorker.h"

RDPServerWorker::RDPServerWorker(uint16_t port, std::string socket_path)
        : starting_port(port),
          initialized(false),
          socket_path(socket_path),
          context(1), // todo: explore the possibility of needing more than one thread
          socket(context, ZMQ_ROUTER)
{
    std::string path = "ipc://" + this->socket_path;
    socket.bind(path);
}

RDPServerWorker::~RDPServerWorker()
{
    std::lock_guard<std::mutex> lock(stop_mutex);
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
    std::thread loop(&RDPServerWorker::run, this);
    loop.detach();
    initialized = true;
    return initialized;
}

bool RDPServerWorker::RegisterNewVM(std::string uuid)
{
    std::lock_guard<std::mutex> lock(container_lock); // take lock on both ports and listener_map
    uint16_t port = 0;
    std::shared_ptr<RDPListener> l;

    // find the next available port from the list
    // stdlib already uses binary search, so if this gets slow for you, consider not running as many VMs on the same
    // computer
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
        l = std::make_shared<RDPListener>(uuid, port, this, dbus_conn); // in here go the RDPListener args
    } catch (std::exception &e) {
        return false;
    }

    std::thread l_thread([l]() {l->RunServer();}); // i think this properly increments and decrements...?
    l_thread.detach();

    listener_map.insert(std::make_pair(uuid, l));

    return true;
}

void RDPServerWorker::UnregisterVM(std::string uuid, uint16_t port)
{
    std::lock_guard<std::mutex> lock(container_lock);
    ports.erase(port);
    listener_map.erase(uuid); // rip listener
}

void RDPServerWorker::sendMessage(std::vector<uint16_t> vec, std::string uuid)
{
    zmq::multipart_t msg;
    msg.addstr(uuid);

    msgpack::sbuffer sbuf;
    msgpack::pack(&sbuf, vec);

    msg.addmem(sbuf.data(), sbuf.size());

    if (!msg.send(socket) || !msg.empty()) {
        LOG(ERROR) << "Unable to send message " << vec;
    }
}

void RDPServerWorker::queueOutgoingMessage(QueueItem item)
{
    out_queue.enqueue(std::move(item));
}

void RDPServerWorker::run()
{
    zmq::pollitem_t item = {
            (void *) socket,
            0,
            ZMQ_POLLIN | ZMQ_POLLOUT, // can i even do this?
            0
    };

    while (true) {

        zmq::poll(&item, 1, -1); // todo : determine reasonable poll interval

        // check if we are terminating
        std::lock_guard<std::mutex> lock(stop_mutex);
        if (stop) {
            LOG(INFO) << "ServerWorker loop terminating on stop";
            initialized = false;
            return;
        }

        if (item.revents & ZMQ_POLLOUT) {
            // send outgoing messages
            while (!out_queue.isEmpty()) {
                QueueItem msg = out_queue.dequeue();
                auto vec = std::get<0>(msg);
                sendMessage(vec, std::get<1>(msg));
            }
        }

        if (item.revents & ZMQ_POLLIN) {
            zmq::multipart_t multi(socket);

            if (multi.size() != 2) {
                LOG(WARNING) << "Possibly invalid message received! Size not 2";
            }

            std::string uuid = multi.popstr();
            std::string data = multi.popstr();

            msgpack::unpacked unpacked;
            msgpack::unpack(&unpacked, data.data(), data.size());

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
}