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

#ifndef QEMU_RDP_RDPSERVERWORKER_H
#define QEMU_RDP_RDPSERVERWORKER_H

#include <giomm/dbusconnection.h>
#include "common.h"
#include "util/MessageQueue.h"
#include "util/zmq_addon.hpp"
#include "rdp/RDPListener.h"

/**
 * @brief The RDPServerWorker class manages the lifetime of the ZeroMQ socket. It also manages the lifetimes of all
 * associated VM connections and RDP listeners.
 *
 * The RDPServerWorker is created and initialized during RDPMux startup. It manages the lifecycle of the ZeroMQ
 * socket. In addition, the RDPServerworker manages the deserialization of messages from the VM, and dispatching
 * messages to and from the appropriate RDP listener.
 */
class RDPServerWorker
{
public:
    /**
     * @brief Creates a new RDPServerWorker.
     *
     * Upon creation, a new ZeroMQ ROUTER socket is created and/or bound to. No events are processed until
     * a VM has registered using RegisterNewVM();
     *
     * @param port The starting port for new RDP listener connections
     * @param socket_path File path to the ZeroMQ socket.
     */
    RDPServerWorker(uint16_t port, std::string socket_path);

    /**
     * @brief Initializes the run loop. After this function returns successfully, the ServerWorker is ready to process
     * messages.
     */
    bool Initialize();

    /**
     * @brief Safely takes down the server worker.
     *
     * Sets stop to true to indicate to the worker thread to stop processing, then joins the thread and waits for
     * completion.
     */
    ~RDPServerWorker();

    /**
     * @brief Registers and initializes new VM connection.
     *
     * Register and initialize a new VM connection. Set up and initialize RDP listener.
     *
     * @returns bool Success
     * @param uuid UUID of incoming VM connection.
     */
    bool RegisterNewVM(std::string uuid);

    /**
     * @brief Unregisters VM
     *
     * Removes listener port from open ports list and removes the shared_ptr wrapping the RDPListener object.
     * Everything will self-destruct as that shared_ptr goes out of scope, so be careful when you invoke this!
     */
    void UnregisterVM(std::string uuid, uint16_t port);

    /**
     * @brief Sets the current DBus connection for internal usage.
     *
     * @param conn Reference to the DBus connection object.
     */
    void setDBusConnection(Glib::RefPtr<Gio::DBus::Connection> conn);

    /**
     * @brief Send a message to the VM with the identity espoused by the UUID.
     *
     * @param sbuf msgpack::sbuffer object containing the serialized data
     * @param uuid the UUID of the VM to send this message to
     */
    void sendMessage(std::vector<uint16_t> vec, std::string uuid);

    /**
     * @brief queues outgoing message
     *
     * @param item QueueItem to be sent.
     */
    void queueOutgoingMessage(QueueItem item);

protected:
    /**
     * @brief starting port for new connections
     */
    uint16_t starting_port;
    /**
     * @brief Lock guarding stop.
     */
    std::mutex stop_mutex;
    /**
     * @brief Variable set to indicate when the RDPServerWorker is stopping.
     */
    bool stop;
    /**
     * @brief Whether the ServerWorker is initialized.
     */
    bool initialized;
    /**
     * @brief Path to the socket in the filesystem.
     */
    std::string socket_path;
    /**
     * @brief Hashmap from UUID to RDPListeners.
     */
    std::map<std::string, std::shared_ptr<RDPListener>> listener_map;

    /**
     * @brief Set containing all in-use ports. Used to intelligently re-use ports as VMs come and go.
     */
    std::set<uint16_t> ports;

    /**
     * @brief mutex on ports so that concurrent accesses are okay.
     */
    std::mutex container_lock;
    /**
    * @brief Reference to the process's DBus connection for registering listeners.
    */
    Glib::RefPtr<Gio::DBus::Connection> dbus_conn;

    MessageQueue out_queue;

    /**
     * @brief ZeroMQ context.
     */
    zmq::context_t context;

    /**
     * @brief ZeroMQ socket.
     */
    zmq::socket_t socket;

    /**
     * @brief Main loop function that receives messages and processes them for dispatch to the RDP listener.
     */
    void run();
};


#endif //QEMU_RDP_RDPSERVERWORKER_H
