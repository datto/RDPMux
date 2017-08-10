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

#include "rdp/RDPListener.h"
#include "RDPServerWorker.h"
#include <fcntl.h>
#include <msgpack/object.hpp>
#include <sys/mman.h>
#include "rdp/subsystem.h"
#include <boost/program_options.hpp>

thread_local RDPListener *rdp_listener_object = NULL;
extern boost::program_options::variables_map vm;

Glib::ustring RDPListener::introspection_xml =
        "<node>"
        "  <interface name='org.RDPMux.RDPListener'>"
        "    <method name='SetCredentialFile'>"
        "      <arg type='s' name='CredentialFile' direction='in' />"
        "    </method>"
        "    <method name='SetAuthentication'>"
        "      <arg type='b' name='auth' direction='in' />"
        "    </method>"
        "    <property type='i' name='Port' access='read' />"
        "    <property type='i' name='NumConnectedPeers' access='read'/>"
        "    <property type='b' name='RequiresAuthentication' access='read'/>"
        "  </interface>"
        "</node>";

RDPListener::RDPListener(std::string uuid, int vm_id, uint16_t port, RDPServerWorker *parent, std::string auth,
                         Glib::RefPtr<Gio::DBus::Connection> conn) : shm_buffer(nullptr),
                                                                     dbus_conn(conn),
                                                                     parent(parent),
                                                                     port(port),
                                                                     uuid(uuid),
                                                                     samfile(),
                                                                     vm_id(vm_id),
                                                                     targetFPS(30),
                                                                     credential_path()
{
    WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi());
    stop = false;

    shadow_subsystem_set_entry(RDPMux_ShadowSubsystemEntry);
    server = shadow_server_new();

    if (!auth.empty())
        samfile = auth;
    this->Authenticating(!auth.empty());

    if (!server) {
        LOG(FATAL) << "LISTENER " << this << ": Shadow server didn't alloc properly, exiting.";
    }
}

RDPListener::~RDPListener()
{
    shadow_server_stop(server);
    dbus_conn->unregister_object(registered_id);
    shadow_server_free(server);
    WSACleanup();
}

void RDPListener::RunServer()
{
    int status = 0;
    DWORD exitCode = 0;
    rdp_listener_object = this; // store a reference to the object in thread-local storage for the shadow server

    std::string config_path = vm["config-path"].as<std::string>();
    this->server->ConfigPath = _strdup(config_path.c_str());

    // dbus setup
    Glib::RefPtr<Gio::DBus::NodeInfo> introspection_data;
    const Gio::DBus::InterfaceVTable vtable(sigc::mem_fun(this, &RDPListener::on_method_call),
                                            sigc::mem_fun(this, &RDPListener::on_property_call));
    // create server name
    Glib::ustring dbus_name = "/org/RDPMux/RDPListener/";
    // sanitize uuid before creating dbus object
    std::string tmp = uuid;
    tmp.erase(std::remove(tmp.begin(), tmp.end(), '-'), tmp.end());
    dbus_name += tmp;

    if (shadow_server_init(this->server) < 0) {
        VLOG(1) << "COULD NOT INIT SHADOW SERVER!!!!!";
        goto cleanup;
    }

    try {
        introspection_data = Gio::DBus::NodeInfo::create_for_xml(introspection_xml);
    } catch (const Glib::Error &ex) {
        LOG(WARNING) << "LISTENER " << this << ": Unable to create introspection data.";
        goto cleanup;
    }
    registered_id = dbus_conn->register_object(dbus_name, introspection_data->lookup_interface(), vtable);

    this->server->port = this->port;

    // Shadow server run loop
    if (shadow_server_start(this->server) < 0) {
        VLOG(1) << "COULD NOT START SHADOW SERVER!!!!!";
        goto cleanup;
    }

    WaitForSingleObject(this->server->thread, INFINITE);

    if (!GetExitCodeThread(this->server->thread, &exitCode)) {
        status = -1;
    } else {
        status = exitCode;
    }

    VLOG(1) << "LISTENER " << this << ": Main loop exited, exit code " << status;
cleanup:
    parent->UnregisterVM(this->uuid, this->port); // this will trigger destruction of the RDPListener object.
}

void RDPListener::processOutgoingMessage(std::vector<uint16_t> vec)
{
    QueueItem item = std::make_tuple(vec, this->uuid);
    parent->queueOutgoingMessage(item);
}

void RDPListener::processIncomingMessage(std::vector<uint32_t> rvec)
{
    // we filter by what type of message it is
    if (rvec[0] == DISPLAY_UPDATE) {
        processDisplayUpdate(rvec);
    } else if (rvec[0] == DISPLAY_SWITCH) {
        VLOG(2) << "LISTENER " << this << ": processing display switch event now";
        processDisplaySwitch(rvec);
    } else if (rvec[0] == SHUTDOWN) {
        VLOG(2) << "LISTENER " << this << ": Shutdown event received!";
        shadow_server_stop(server);
        parent->UnregisterVM(this->uuid, this->port);
    } else {
        // what the hell have you sent me
        LOG(WARNING) << "Invalid message type sent.";
    }
}

std::tuple<uint32_t, uint32_t, uint32_t, uint32_t> RDPListener::GetDirtyRegion()
{
    std::lock_guard<std::mutex> lock(dimMutex);
    return std::make_tuple(x, y, w, h);
}

void RDPListener::processDisplayUpdate(std::vector<uint32_t> msg)
{
    // note that under current calling conditions, this will run in the mainloop of the RDPServerWorker.

    VLOG(3) << "LISTENER " << this << ": Now processing display update message";
    {
        std::lock_guard<std::mutex> lock(dimMutex);
        x = msg.at(1);
        y = msg.at(2);
        w = msg.at(3);
        h = msg.at(4);
    }
}

std::tuple<int, int, int> RDPListener::GetRDPFormat()
{
    switch (this->format)
    {
        case PIXMAN_r8g8b8a8:
        case PIXMAN_r8g8b8x8:
            return std::make_tuple(PIXEL_FORMAT_XBGR32, PIXEL_FORMAT_XBGR32, 4);
        case PIXMAN_a8r8g8b8:
        case PIXMAN_x8r8g8b8:
            return std::make_tuple(PIXEL_FORMAT_XRGB32, PIXEL_FORMAT_XRGB32, 4);
        case PIXMAN_r8g8b8:
            return std::make_tuple(PIXEL_FORMAT_BGR24, PIXEL_FORMAT_XRGB32, 3);
        case PIXMAN_b8g8r8:
            return std::make_tuple(PIXEL_FORMAT_RGB24, PIXEL_FORMAT_XRGB32, 3);
        case PIXMAN_r5g6b5:
             return std::make_tuple(PIXEL_FORMAT_BGR16, PIXEL_FORMAT_XRGB32, 2);
        case PIXMAN_x1r5g5b5:
             return std::make_tuple(PIXEL_FORMAT_ABGR15, PIXEL_FORMAT_XRGB32, 2);
        default:
            return std::make_tuple(-1, -1, -1);
    }
}

void RDPListener::processDisplaySwitch(std::vector<uint32_t> msg)
{
    // note that under current calling conditions, this will run in the thread of the RDPServerWorker associated with
    // the VM.
    VLOG(2) << "LISTENER " << this << ": Now processing display switch event";
    uint32_t displayWidth = msg.at(2);
    uint32_t displayHeight = msg.at(3);
    pixman_format_code_t displayFormat = (pixman_format_code_t) msg.at(1);
    int shim_fd;
    size_t shm_size = 4096 * 2048 * sizeof(uint32_t);

    // TODO: clear all queues if necessary

    // map in new shmem region if it's the first time
    if (!shm_buffer) {
        std::stringstream ss;
        ss << "/" << vm_id << ".rdpmux";

        VLOG(2) << "LISTENER " << this << ": Creating new shmem buffer from path " << ss.str();
        shim_fd = shm_open(ss.str().data(), O_RDONLY, S_IRUSR | S_IRGRP | S_IROTH);
        VLOG(3) << "LISTENER " << this << ": shim_fd is " << shim_fd;

        void *shm_buffer = mmap(NULL, shm_size, PROT_READ, MAP_SHARED, shim_fd, 0);
        if (shm_buffer == MAP_FAILED) {
            LOG(WARNING) << "mmap() failed: " << strerror(errno);
            // todo: send this information to the backend service so it can trigger a retry
            return;
        }
        VLOG(2) << "LISTENER " << this << ": mmap() completed successfully! Yayyyyyy";
        this->shm_buffer = shm_buffer;
    }

    this->width = displayWidth;
    this->height = displayHeight;
    this->format = displayFormat;

    VLOG(2) << "LISTENER " << this << ": Display switch processed successfully!";
}

size_t RDPListener::Width()
{
    return this->width;
}

size_t RDPListener::Height()
{
    return this->height;
}

std::string RDPListener::CredentialPath()
{
    return credential_path;
}

bool RDPListener::Authenticating()
{
    return authenticating;
}

void RDPListener::Authenticating(bool auth)
{
    this->authenticating = auth;
    if (auth) {
        this->server->settings->NlaSecurity = TRUE;
        this->server->settings->TlsSecurity = FALSE;
        if (!this->server->settings->NtlmSamFile)
            this->server->settings->NtlmSamFile = _strdup(this->samfile.c_str());
    } else {
        this->server->settings->NlaSecurity = FALSE;
    }
}

void RDPListener::on_method_call(const Glib::RefPtr<Gio::DBus::Connection> &, /* connection */
                                 const Glib::ustring &, /* sender */
                                 const Glib::ustring &, /* object path */
                                 const Glib::ustring &, /* interface name */
                                 const Glib::ustring &method_name,
                                 const Glib::VariantContainerBase &parameters,
                                 const Glib::RefPtr<Gio::DBus::MethodInvocation> &invocation)
{
    if (method_name == "SetCredentialFile") {
        Glib::Variant<std::string> cred_variant;
        parameters.get_child(cred_variant, 0);

        credential_path = cred_variant.get();

        invocation->return_value(Glib::VariantContainerBase());
    } else if (method_name == "SetAuthentication") {
        Glib::Variant<bool> auth_variant;
        parameters.get_child(auth_variant, 0);
        this->Authenticating(auth_variant.get());
        invocation->return_value(Glib::VariantContainerBase());
    } else {
        Gio::DBus::Error error(Gio::DBus::Error::UNKNOWN_METHOD,
            "Method does not exist.");
        invocation->return_error(error);
    }
}

void RDPListener::on_property_call(Glib::VariantBase &property,
                                   const Glib::RefPtr<Gio::DBus::Connection> &,
                                   const Glib::ustring &,
                                   const Glib::ustring &,
                                   const Glib::ustring &,
                                   const Glib::ustring &property_name)
{
    if (property_name == "Port") {
        property = Glib::Variant<uint16_t>::create(port);
    } else if (property_name == "NumConnectedPeers") {
        property = Glib::Variant<uint32_t>::create(ArrayList_Count(this->server->clients));
    } else if (property_name == "RequiresAuthentication") {
        property = Glib::Variant<bool>::create(authenticating);
    }
}

