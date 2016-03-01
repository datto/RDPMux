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

#include <iostream>
#include <tuple>
#include <functional>
#include <cstring>

#include <sys/mman.h>
#include <fcntl.h>

#include <msgpack/sbuffer.hpp>
#include <msgpack/object.hpp>
#include <freerdp/channels/channels.h>

#include "nanomsg.h"
#include "rdp/RDPListener.h"
#include "util/logging.h"
#include "common.h"

thread_local nn::socket *my_nn_socket = NULL;
thread_local RDPListener *rdp_listener_object = NULL;

/**
 * @brief starts up the run loop of an RDPPeer connection.
 *
 * This function marshals the arguments needed to start up an RDPPeer thread, and passes them into the new thread
 * function. The thread created runs RDPPeer::PeerThread as a detached thread. For reasons lost to the mists of time,
 * this function actually uses the winRP API to start the thread instead of straight pthreads.
 *
 * @returns Success of the thread creation
 *
 * @param instance The freerdp_listener struct containing the listener's state.
 * @param client The newly-initialized freerdp_peer struct containing all the state needed for the peer connection.
 */
BOOL StartPeerLoop(freerdp_listener *instance, freerdp_peer *client)
{
    HANDLE hThread;

    // get our args together, and start the thread
    auto arg_tuple = new std::tuple<freerdp_peer*, const nn::socket*, RDPListener *>(client, my_nn_socket, rdp_listener_object);
    if (!(hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) RDPPeer::PeerThread, (void *) arg_tuple, 0, NULL))) {
        return FALSE;
    }
    CloseHandle(hThread);
    return TRUE;
}

RDPListener::RDPListener(nn::socket *sock) : shm_buffer(0)
{
    WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi());
    this->sock = sock;
    my_nn_socket = sock;
    stop = false;

    listener = freerdp_listener_new();
    rdp_listener_object = this; // store a reference to the object in thread-local storage for the peer connections

    if (!listener) {
        LOG(FATAL) << "LISTENER " << this << ": Listener didn't alloc properly, exiting.";
    }

    if (WSAStartup(MAKEWORD(2,2), &wsadata) != 0) {
        freerdp_listener_free(listener);
        throw std::runtime_error("WSAStartup failed");
    }

    listener->PeerAccepted = StartPeerLoop;
}

RDPListener::~RDPListener()
{
    {
        std::unique_lock<std::mutex> lock(stopMutex);
        stop = true;
    }
    freerdp_listener_free(listener);
    WSACleanup();
}

void RDPListener::RunServer(uint16_t port)
{
    void *connections[32];
    DWORD count = 0;

    if (listener->Open(listener, NULL, port)) {
        VLOG(1) << "LISTENER " << this << ": Listener started successfully.";

        count = listener->GetEventHandles(listener, connections, 32);

        if (count < 1) {
            throw std::runtime_error("Failed to get event handles.");
        }

        /* Enter main server loop */
        while (1) {

            // check if we are terminating
            {
                std::unique_lock<std::mutex> lock(stopMutex);
                if (stop) break;
            }

            size_t status = WaitForMultipleObjects(count, connections, false, INFINITE);

            if (status == WAIT_FAILED) {
                VLOG(1) << "LISTENER " << this << ": Wait failed.";
                break;
            }

            /* Validate incoming TCP/IP connections */
            if (listener->CheckFileDescriptor(listener) != TRUE) {
                VLOG(1) << "LISTENER " << this << ": Failed to validate TCP/IP connection.";
                break;
            }
        }
    }
    VLOG(1) << "LISTENER " << this << ": Main loop exited";
    // after the main loop, we should do nothing. In a perfect world, the destructor is called automatically
    // when the object goes out of scope, which should be soonish after the resolution of this function.
}

void RDPListener::processDisplayUpdate(std::vector<uint32_t> msg)
{
    // note that under current calling conditions, this will run in the thread of the RDPServerWorker associated with
    // the VM.

    uint32_t x = msg.at(1),
             y = msg.at(2),
             w = msg.at(3),
             h = msg.at(4);

    VLOG(1) << "LISTENER " << this << ": Now taking lock on peerlist to send display update message";
    {
        std::lock_guard<std::mutex> lock(peerlist_mutex);
        if (peerlist.size() > 0) {
            VLOG(2) << std::dec << "LISTENER " << this << ": Now processing display update message [(" << (int) x << ", " << (int) y << ") " << (int) w << ", " << (int) h << "]";
            std::for_each(peerlist.begin(), peerlist.end(), [=](RDPPeer *peer) {
                peer->PartialDisplayUpdate(x, y, w, h);
            });
        }
    }
    VLOG(1) << "LISTENER " << this << ": Lock released successfully! Continuing.";

    // send back display update complete message
    std::vector<uint32_t> vec;
    vec.push_back(DISPLAY_UPDATE_COMPLETE);
    vec.push_back(1);

    msgpack::sbuffer sbuf;
    msgpack::pack(sbuf, vec);

    sock->send(sbuf.data(), sbuf.size(), 0);
    VLOG(1) << "LISTENER " << this << ": Sent ack to QEMU process.";
}

pixman_format_code_t RDPListener::GetFormat()
{
    return format;
}

void RDPListener::processDisplaySwitch(std::vector<uint32_t> msg, int vm_id)
{
    // note that under current calling conditions, this will run in the thread of the RDPServerWorker associated with
    // the VM.
    VLOG(2) << "LISTENER " << this << ": Now processing display switch event";
    uint32_t w = msg.at(2),
             h = msg.at(3);
    pixman_format_code_t format = (pixman_format_code_t) msg.at(1);
    int shim_fd;
    size_t shm_size = 4096 * 2048 * sizeof(uint32_t);

    // TODO: clear all queues if necessary

    // map in new shmem region if it's the first time
    if (!shm_buffer) {
        const Glib::ustring path = Glib::ustring::compose("/%1.rdpmux", vm_id);
        shim_fd = shm_open(path.data(), O_RDONLY, S_IRUSR | S_IRGRP | S_IROTH);
        VLOG(2) << "LISTENER " << this << ": shim_fd is " << shim_fd;

        void *shm_buffer = mmap(NULL, shm_size, PROT_READ, MAP_SHARED, shim_fd, 0);
        if (shm_buffer == MAP_FAILED) {
            LOG(WARNING) << "mmap() failed: " << strerror(errno);
            // todo: send this information to the backend service so it can trigger a retry
            return;
        }
        VLOG(2) << "LISTENER " << this << ": mmap() completed successfully! Yayyyyyy";
        this->shm_buffer = shm_buffer;
    }

    this->width = w;
    this->height = h;
    this->format = format;

    // send full display update to all peers, but only if there are peers connected
    {
        std::lock_guard<std::mutex> lock(peerlist_mutex);
        if (peerlist.size() > 0) {
            std::for_each(peerlist.begin(), peerlist.end(), [=](RDPPeer *peer) {
                VLOG(3) << "LISTENER " << this << ": Sending peer update region request now";
                peer->FullDisplayUpdate(format);
            });
        }
    }

    VLOG(2) << "LISTENER " << this << ": Display switch processed successfully!";
}

void RDPListener::registerPeer(RDPPeer *peer)
{
    // thread-safe member function to register a peer with the parent listener object. The listener uses the peer
    // registered via this method to pass down display update events for encoding and transmission to all RDP clients.
    std::lock_guard<std::mutex> lock(peerlist_mutex);
    peerlist.push_back(peer);
    peer->FullDisplayUpdate(format);
    VLOG(2) << "Registered peer " << peer;
}

void RDPListener::unregisterPeer(RDPPeer *peer)
{
    std::lock_guard<std::mutex> lock(peerlist_mutex);
    auto pos = std::find(peerlist.begin(), peerlist.end(), peer);
    peerlist.erase(pos);
}

size_t RDPListener::GetWidth()
{
    return this->width;
}

size_t RDPListener::GetHeight()
{
    return this->height;
}