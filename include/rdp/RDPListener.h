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

#ifndef QEMU_RDP_RDPLISTENER_H
#define QEMU_RDP_RDPLISTENER_H

#include "common.h"
#include <freerdp/freerdp.h>
#include <freerdp/listener.h>
#include <pixman.h>
#include <msgpack/sbuffer.hpp>
#include <winpr/winsock.h>

class RDPPeer; // I'm very bad at organizing C++ code.
class RDPServerWorker; // I continue to get worse at organizing C++ code.

extern BOOL start_peerloop(freerdp_listener *instance, freerdp_peer *client);

/**
 * @brief C++ class wrapping the freerdp_listener struct associated with the RDP server.
 *
 * This class manages peer connections, provides RAII abstractions for the setup and teardown of the freerdp_listener
 * struct, provides accessor methods for the state of the framebuffer, etc.
 */
class RDPListener
{
public:
    /**
     * @brief Initializes the listener, given a nanomsg socket to communicate through.
     *
     * @param uuid The UUID of the VM associated with this listener.
     * @param vm_id The unique ID of the VM's framebuffer.
     * @param port The port the listener should bind to.
     * @param parent A pointer to the RDPServerWorker for (LIMITED) use // todo: not so limited
     * @param auth Whether the listener should authenticate peer connections
     * @param conn Reference to the process's DBus connection for exposing the Listener object
     */
    RDPListener(std::string uuid, int vm_id, uint16_t port, RDPServerWorker *parent, bool auth,
                Glib::RefPtr<Gio::DBus::Connection> conn);
    /**
     * @brief Safely cleans up the freerdp_listener struct and frees all WinPR objects.
     */
    ~RDPListener();

    /**
     * @brief starts the listener subroutine and loops forever on incoming connections.
     */
    void RunServer();

    /**
     * @brief Processes outgoing messages from the RDP client to the VM.
     *
     * @param sbuf serialized msgpack object containing update.
     */
    void processOutgoingMessage(std::vector<uint16_t> vec);

    /**
     * @brief Processes incoming messages from the VM.
     *
     * Serves as an entry point for incoming messages from the VM via the RDPServerWorker.
     *
     * @param vec Deserialized vector of uint32_ts comprising the message
     */
    void processIncomingMessage(std::vector<uint32_t> rvec);

    /**
     * @brief Processes display updates and sends them to peers.
     *
     * The listener first retrieves the update parameters from the deserialized message passed in. It then sends a
     * display update to all connected peers (which are sharing the same VM session and framebuffer) in a thread-safe
     * manner. This prevents connection threads from updating the list of connected peers while an update is in-flight.
     * It then sends back the DISPLAY_UPDATE_COMPLETE message to the VM.
     *
     * @param msg The deserialized update message. Should be guaranteed by caller to be from a message of type
     * DISPLAY_UPDATE.
     */
    void processDisplayUpdate(std::vector<uint32_t> msg);

    /**
     * @brief Processes display switch events and sends them to peers.
     *
     * The listener first retrieves the framebuffer format, width, and height from the deserialized message passed in.
     * It then mmaps() the shared memory region containing the new framebuffer if necessary (usually only happens the
     * first time a display switch event is received, during initialization) and notifies all connected peers
     * to perform a full screen refresh using RDPPeer::FullDisplayUpdate.
     *
     * @param msg The deserialized display switch message. Should be guaranteed by caller to be from a message of type
     * DISPLAY_SWITCH.
     * @param vm_id The ID of the VM.
     */
    void processDisplaySwitch(std::vector<uint32_t> msg);

    /**
     * @brief Registers a peer with the RDPListener.
     *
     * Registers a peer with its parent listener in a thread-safe manner. The listener passes down display events
     * to the registered peers for encoding and transmission to their connected clients.
     *
     * @param peer Peer to be registered.
     */
    void registerPeer(RDPPeer *peer);

    /**
     * @brief Unregisters a Peer.
     *
     * @param peer Peer to be un-registered.
     */
    void unregisterPeer(RDPPeer *peer);

    /**
     * @brief Checks authentication of the credential pair passed in.
     *
     * @returns Whether the credentials are valid.
     */
    bool CheckAuthentication(const char *username, const char *password);

    /**
     * @brief Gets the width of the framebuffer.
     *
     * @returns The width of the framebuffer.
     */
    size_t GetWidth();

    /**
     * @brief Gets the height of the framebuffer.
     *
     * @returns The height of the framebuffer.
     */
    size_t GetHeight();

    /**
     * @brief Gets the pixel format of the framebuffer.
     *
     * @returns The pixel format of the framebuffer.
     */
    pixman_format_code_t GetFormat();

    /**
     * @brief See whether the listener was configured to authenticate connections
     *
     * @returns authentication configuration status
     */
    bool GetAuthenticating();

    /**
     * @brief The freerdp_listener struct this object manages.
     *
     * This is the actual FreeRDP listener struct that holds most of the real internal state of the RDP listener itself.
     * This class is essentially an overgrown wrapper for this object.
     */
    freerdp_listener *listener;

    /**
     * @brief The shared memory region containing the framebuffer. Always 32 MB big.
     */
    void *shm_buffer;

private:

    WSADATA wsadata;

    /**
     * @brief dbus introspection xml
     */
    static Glib::ustring introspection_xml;

    /**
     * dbus connection
     */
    Glib::RefPtr<Gio::DBus::Connection> dbus_conn;

    /**
     * @brief Reference to the parent ServerWorker. NO GUARANTEES USE AT OWN RISK. //todo : guarantee
     *
     * @warning NEVER CALL DELETE ON THIS EVER
     */
    RDPServerWorker *parent;

    /**
     * @brief Port number to listen on.
     */
    uint16_t port;

    /**
     * @brief UUID of the VM associated with the listener.
     */
    std::string uuid;

    /**
     * @brief Unique ID of VM framebuffer.
     */
    int vm_id;

    /**
     * @brief Mutex guarding access to the peer list.
     */
    std::mutex peerlist_mutex;

    /**
     * @brief List of registered peers.
     */
    std::vector<RDPPeer *> peerlist;

    /**
     * @brief The width of the framebuffer. Accessed via GetWidth().
     */
    size_t width;

    /**
     * @brief The height of the framebuffer. Accessed via GetHeight().
     */
    size_t height;

    /**
     * @brief The pixel format of the framebuffer. Accessed via GetFormat().
     */
    pixman_format_code_t format;

    std::mutex stopMutex;
    bool stop;
    guint registered_id = 0;

    /**
     * @brief whether the listener is configured to authenticate peers
     */
    bool authenticating;

    /**
     * @brief Map holding set of authentication credentials
     */
    std::map<std::string, std::string> credentials;

    /**
     * @brief Salt for password authentication.
     */
    std::string salt;

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
     * @brief Method called when a DBus property is queried.
     */
    void on_property_call(Glib::VariantBase& property,
                          const Glib::RefPtr<Gio::DBus::Connection>&,
                          const Glib::ustring&,
                          const Glib::ustring&,
                          const Glib::ustring&,
                          const Glib::ustring& property_name);
};

#endif //QEMU_RDP_RDPLISTENER_H
