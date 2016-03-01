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

#include <nanomsg/pair.h>

#include <memory>
#include <util/MessageQueue.h>
#include <iostream>
#include <msgpack.hpp>
#include <thread>
#include <util/logging.h>
#include <giomm/dbusconnection.h>
#include "nanomsg.h"
#include "RDPServerWorker.h"
#include "rdp/RDPListener.h"
#include "common.h"

Glib::ustring RDPServerWorker::introspection_xml =
        "<node>"
        "  <interface name='org.RDPMux.ServerWorker'>"
        "    <property type='i' name='Port' access='read' />"
        "  </interface>"
        "</node>";

void RDPServerWorker::setDBusConnection(Glib::RefPtr<Gio::DBus::Connection> conn)
{
    if (!dbus_conn)
        dbus_conn = conn;
}

void RDPServerWorker::run()
{
    int to = 1000000;

    // create nanomsg socket using the path passed in.
    // ACHTUNG: remember that the path _must exist_ on the filesystem, otherwise this silently fails!
    // TODO: Make it throw an exception when the file path doesn't exist.
    nn::socket *sock = new nn::socket(AF_SP, NN_PAIR);

    try {
        VLOG(3) << "Socket path is " << this->socket_path.data();
        sock->bind(this->socket_path.data());
        sock->setsockopt(NN_SOL_SOCKET, NN_RCVTIMEO, &to, sizeof(to));
    } catch (nn::exception &ex) {
        LOG(WARNING) << "Socket binding went wrong: " << ex.what();
        return;
    }
    VLOG(2) << "WORKER " << this << ": Socket bound, hopefully it's listening properly. Address: " << sock;

    // TODO: If listener startup fails, ServerWorker won't know about it :(
    VLOG(3) << "WORKER " << this << ": Now passing sock to RDP listener and starting listener thread";
    std::thread listener_thread([this](nn::socket *socket) {
        VLOG(3) << "WORKER " << this << ": Internal socket pointer is " << socket;
        RDPListener *listener;
        try {
            listener = new RDPListener(socket);
        } catch (std::runtime_error &e) {
            LOG(WARNING) << "WORKER " << this << ": Listener startup failed";
            return;
        }
        l = listener;
        listener->RunServer(this->port);
        delete l;
    }, sock);
    listener_thread.detach();

    // now that listener is up we can register the object on the bus
    const Gio::DBus::InterfaceVTable vtable(sigc::mem_fun(*this, &RDPServerWorker::on_method_call), sigc::mem_fun(*this, &RDPServerWorker::on_property_call));
    Glib::ustring worker_dbus_base_name = "/org/RDPMux/ServerWorker/";
    worker_dbus_base_name += std::to_string(vm_id);
    Glib::RefPtr<Gio::DBus::NodeInfo> introspection_data;

    try {
        introspection_data = Gio::DBus::NodeInfo::create_for_xml(introspection_xml);
    } catch (const Glib::Error &ex) {
        LOG(WARNING) << "WORKER " << this << ": Unable to create ServerWorker introspection data.";
        return;
    }

    registered_id = dbus_conn->register_object(worker_dbus_base_name,
                               introspection_data->lookup_interface(), vtable);

    MessageQueue qemu_event_queue;

    // main QEMU recv loop.
    while (true) {
        {
            // check if we are terminating
            Glib::Mutex::Lock lock(mutex);
            if (stop) {
                delete l;
                break;
            }
        }

        // Then we process incoming qemu events and send them to the RDP client
        while (!qemu_event_queue.isEmpty()) {
            processIncomingMessage(qemu_event_queue.dequeue());
        }

        // finally, we block waiting on new qemu events to come to us, and put them on the queue when they do.
        void *buf = nullptr;
        int nbytes = sock->recv(&buf, NN_MSG, 0); // blocking

        if (nbytes > 0) {
            QueueItem *item = new QueueItem(buf, nbytes); // QueueItem is responsible for the buf from this point on.
            qemu_event_queue.enqueue(item);
        }
    }

    dbus_conn->unregister_object(registered_id);
    VLOG(1) << "WORKER " << this << ": Main loop terminated.";
}

void RDPServerWorker::processIncomingMessage(const QueueItem *item)
{
    std::vector<uint32_t> rvec;

    // deserialize message
    msgpack::unpacked msg;

    msgpack::unpack(&msg, (char *) item->item, (size_t) item->item_size);
    VLOG(3) << "WORKER " << this << ": Unpacked object, now converting it to a vector";

    try {
        msgpack::object obj = msg.get();
        obj.convert(&rvec);
    } catch (std::exception& ex) {
        LOG(ERROR) << "Msgpack conversion failed: " << ex.what();
        LOG(ERROR) << "Offending buffer is " << msg.get();
        return;
    }

    VLOG(2) << "WORKER " << this << ": Incoming vector is: " << rvec;
    // now we filter by what type of message it is
    if (rvec[0] == DISPLAY_UPDATE) {
        VLOG(1) << "WORKER " << this << ": DisplayWorker processing display update event now";
        l->processDisplayUpdate(rvec);
    } else if (rvec[0] == DISPLAY_SWITCH) {
        VLOG(2) << "WORKER " << this << ": DisplayWorker processing display switch event now";
        l->processDisplaySwitch(rvec, this->vm_id);
    } else if (rvec[0] == SHUTDOWN) {
        VLOG(2) << "WORKER " << this << ": Shutdown event received!";
        // TODO: process shutdown events
    } else {
        // what the hell have you sent me
        LOG(WARNING) << "Invalid message type sent.";
    }

    // by deleting the item, we also free the buf
    delete item;
}

void RDPServerWorker::on_method_call(const Glib::RefPtr<Gio::DBus::Connection> &,
                                     const Glib::ustring &,
                                     const Glib::ustring &,
                                     const Glib::ustring &,
                                     const Glib::ustring &method_name,
                                     const Glib::VariantContainerBase &parameters,
                                     const Glib::RefPtr<Gio::DBus::MethodInvocation> &invocation)
{
    // no methods for now, but this stub needs to be here
}

void RDPServerWorker::on_property_call(Glib::VariantBase& property,
                      const Glib::RefPtr<Gio::DBus::Connection>&,
                      const Glib::ustring&, // sender
                      const Glib::ustring&, // object path
                      const Glib::ustring&, // interface_name
                      const Glib::ustring& property_name)
{
    if (property_name == "Port") {
        property = Glib::Variant<uint16_t>::create(port);
    }
}