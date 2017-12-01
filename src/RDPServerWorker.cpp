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

RDPServerWorker::RDPServerWorker(uint16_t port, bool auth)
        : starting_port(3901),
          initialized(false),
          context(1), // todo: explore the possibility of needing more than one thread
          zsocket(context, ZMQ_ROUTER),
          authenticating(auth)
{
    std::string path = "ipc://@/tmp/rdpmux";
    zsocket.setsockopt(ZMQ_ROUTER_MANDATORY, 1);
    zsocket.bind(path);
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

bool RDPServerWorker::RegisterNewVM(std::string uuid, int id, std::string auth, uint16_t port = 0)
{
    std::lock_guard<std::mutex> lock(container_lock); // take lock on both ports and listener_map
    uint16_t used_port = 0;
    std::shared_ptr<RDPListener> l;

    if (port == 0) {
        // find the next available port
        // if this gets slow for you, consider not running as many VMs on the same computer
        for (uint16_t i = starting_port; i < 65535; i++) {
            if (ports.count(i) == 0) {
                // check if port is available to be bound
                struct addrinfo hints, *res;
                int sockfd;

                memset(&hints, 0, sizeof(hints));
                hints.ai_family = AF_INET; // IPv4
                hints.ai_socktype = SOCK_STREAM; // TCP stream socket
                getaddrinfo("0.0.0.0", std::to_string(i).c_str(), &hints,
                            &res); // check for usage across all interfaces

                sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

                // try binding
                int ret = bind(sockfd, res->ai_addr, res->ai_addrlen);
                free(res); // free the linked list of results no matter what
                if (ret < 0) { // failed, continue the loop
                    continue;
                }
                // cleanup and setting the port if we suceeded
                close(sockfd);
                used_port = i;
                break;
            }
        }
    } else {
        used_port = port;
    }

    if (used_port == 0 || used_port > 65535) {
        LOG(WARNING) << "Invalid server port number: " << used_port;
        return false;
    }

    ports.insert(used_port);

    try {
        l = std::make_shared<RDPListener>(uuid, id, used_port, this, auth, dbus_conn);
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
    listener_map.erase(uuid); // rip server
}

void RDPServerWorker::sendMessage(std::vector<uint16_t> vec, std::string uuid)
{
    zmq::multipart_t msg;

    try {
        msg.addstr(connection_map.at(uuid));
    } catch (std::out_of_range &e) {
        LOG(ERROR) << "Could not find connection id for UUID " << uuid;
        return;
    }

    msg.addstr(uuid);

    msgpack::sbuffer sbuf;
    msgpack::pack(&sbuf, vec);

    msg.addmem(sbuf.data(), sbuf.size());

    if (!msg.send(zsocket) || !msg.empty()) {
        LOG(ERROR) << "Unable to send message " << vec;
    }
}

void RDPServerWorker::queueOutgoingMessage(QueueItem item)
{
    out_queue.enqueue(std::move(item));
}

void RDPServerWorker::run()
{
    int ret = -1;
    zmq::pollitem_t item = {
            (void *) zsocket,
            0,
            ZMQ_POLLIN,
            0
    };

    while (true) {
        // check if we are terminating
        std::lock_guard<std::mutex> lock(stop_mutex);
        if (stop) {
            LOG(INFO) << "ServerWorker loop terminating on stop";
            initialized = false;
            return;
        }

        // send outgoing messages first
        while (!out_queue.isEmpty()) {
            QueueItem msg = out_queue.dequeue();
            auto vec = std::get<0>(msg);
            try {
                sendMessage(vec, std::get<1>(msg));
            } catch (zmq::error_t &ex) {
                LOG(WARNING) << "ZMQ EXCEPTION: " << ex.what();
                break;
            }
        }

        try {
            ret = zmq::poll(&item, 1, 5); // todo : determine reasonable poll interval
        } catch (zmq::error_t &ex) {
            LOG(WARNING) << "ZMQ EXCEPTION: " << ex.what();
            continue;
        }

        if (ret > 0) {

            if (item.revents & ZMQ_POLLIN) {
                zmq::multipart_t multi(zsocket);

                if (multi.size() != 3) {
                    LOG(WARNING) << "Possibly invalid message received! Message is: " << multi.str();
                    continue;
                }

                //VLOG(3) << multi.str();

                std::string id = multi.popstr();
                std::string uuid = multi.popstr();
                std::string data = multi.popstr();

                msgpack::unpacked unpacked;
                msgpack::unpack(&unpacked, data.data(), data.size());

                // deserialize msgpack message and pass to correct server
                try {
                    // so these two lines have to be in this order. if listener_map.at() fails, it'll skip the
                    // connection_map line, which will silently create and/or update if nothing exists.
                    auto server = listener_map.at(uuid);
                    connection_map[uuid] = id;

                    msgpack::object obj = unpacked.get();
                    std::vector<uint32_t> vec;
                    obj.convert(&vec);
                    server->processIncomingMessage(vec);
                } catch (std::out_of_range &e) {
                    LOG(WARNING) << "Listener with UUID " << uuid << " does not exist in map!";
                } catch (std::exception &e) {
                    LOG(ERROR) << "Msgpack conversion failed: " << e.what();
                    LOG(ERROR) << "Offending buffer is " << unpacked.get();
                }
            }
        } else if (ret == -1) {
            LOG(WARNING) << "Error polling socket: " << ret;
        }
    }
}
