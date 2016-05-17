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


#include <glibmm/thread.h>
#include <glibmm/dispatcher.h>

#include "util/MessageQueue.h"
#include "nanomsg.h"
#include "rdp/RDPListener.h"


// TODO: add path validation checking (if the path given is to a writable directory, if it's a valid path, etc.)
/**
 * @brief The RDPServerWorker class manages the lifetime of the nanomsg socket and the RDP server and connection associated
 * with the VM.
 *
 * RDPServerWorkers are spawned when a VM registers with the RDPMux server, and manage the lifecycle of the communication
 * socket associated with the VM and the RDP server spawned to interact with the VM and client. They exist for as long as
 * the VM associated with the connection is alive, and are closed when the VM itself closes.
 *
 * RDPServerworkers manage the deserialization of messages from the VM, and dispatching messages to the RDP listener.
 */
class RDPServerWorker
{
public:
    /**
     * @brief Creates a new RDPServerworker object.
     *
     * @param path File path to the nanomsg socket in the filesystem
     * @param vm_id Internal VM ID.
     * @param uuid The external UUID of the VM.
     */
    RDPServerWorker(const Glib::ustring &path, int vm_id, uint16_t port, Glib::ustring uuid) : out_thread(0),
                                                                                               stop(false)
    {
        socket_path = path;
        this->vm_id = vm_id;
        this->port = port;
        this->uuid = uuid;
    }

    /**
     * @brief starts the run loop.
     */
    void start() {
        out_thread = Glib::Thread::create(sigc::mem_fun(*this, &RDPServerWorker::run), true);
    }

    /**
     * @brief Safely takes down the server worker.
     *
     * Sets stop to true to indicate to the worker thread to stop processing, then joins the thread and waits for
     * completion.
     */
    ~RDPServerWorker()
    {
        {
            Glib::Mutex::Lock lock(mutex);
            stop = true;
        }
        if (out_thread)
            out_thread->join(); // here we block and wait for the thread to actually complete
    }

    /**
     * @brief Signal that socket creation has completed.
     */
    Glib::Dispatcher socket_creation_done; // signal that socket creation is complete

    /**
     * @brief Integer denoting the next port to start a listener on.
     */
    uint16_t port;

    /**
     * @brief Reference to the process's DBus connection for registering ServerWorker object.
     */
    Glib::RefPtr<Gio::DBus::Connection> dbus_conn;

    /**
    * @brief Introspection XML for the DBus object on the bus.
    */
    static Glib::ustring introspection_xml;

    /**
     * @brief Processes incoming messages and dispatches them to the listener appropriately.
     *
     * This function is called once the deserializes incoming messages into an stl::vector and sends that vector
     * to the RDP listener.
     *
     * @param item The item to deserialize.
     */
    void processIncomingMessage(const QueueItem *item);

    /**
     * @brief Sets the current DBus connection for internal usage.
     *
     * @param conn Reference to the DBus connection object.
     */
    void setDBusConnection(Glib::RefPtr<Gio::DBus::Connection> conn);

protected:
    /**
     * @brief Reference to worker thread spinning on the out loop.
     */
    Glib::Thread *out_thread;
    /**
     * @brief Lock guarding stop.
     */
    Glib::Mutex mutex;
    /**
     * @brief Variable set to indicate when the RDPServerWorker is stopping.
     */
    bool stop;
    /**
     * @brief Path to the socket in the filesystem.
     */
    Glib::ustring socket_path;
    /**
     * @brief UUID of the VM associated with this ServerWorker.
     */
    Glib::ustring uuid;
    /**
     * @brief Pointer to the nanomsg socket.
     */
    nn::socket *sock;
    /**
     * @brief ID of the VM associated with this RDPServerWorker object.
     */
    int vm_id;

    /**
     * @brief Reference to the RDPListener object spawned by this VM.
     */
    RDPListener *l;

    /**
     * @brief Main loop function that receives messages and processes them for dispatch to the RDP listener.
     */
    void run();

    /**
     * @brief Method called when a DBus method call is invoked.
     */
    void on_method_call(const Glib::RefPtr<Gio::DBus::Connection> &,
                        const Glib::ustring &,
                        const Glib::ustring &,
                        const Glib::ustring &,
                        const Glib::ustring &method_name,
                        const Glib::VariantContainerBase &parameters,
                        const Glib::RefPtr<Gio::DBus::MethodInvocation> &invocation);

    /**
     * @brief Method called when a DBus property is needed.
     */
    void on_property_call(Glib::VariantBase& property,
                                 const Glib::RefPtr<Gio::DBus::Connection>&,
                                 const Glib::ustring&,
                                 const Glib::ustring&,
                                 const Glib::ustring&,
                                 const Glib::ustring& property_name);

    /**
     * @brief ID of the registered DBus object.
     */
    guint registered_id = 0;
};


#endif //QEMU_RDP_RDPSERVERWORKER_H
