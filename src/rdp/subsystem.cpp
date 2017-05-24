//
// Created by sramanujam on 5/23/17.
//

#include <winpr/sysinfo.h>
#include "rdp/subsystem.h"

#define TAG SERVER_TAG("rdpmux.subsystem")

extern thread_local RDPListener *rdp_listener_object;

void rdpmux_synchronize_event(rdpmuxShadowSubsystem *system, rdpShadowClient *client, UINT32 flags)
{
    WLog_INFO(TAG, "SYNCHRONIZE -- Flags: %#04x (%u)", flags, flags);
}

void rdpmux_unicode_keyboard_event(rdpmuxShadowSubsystem *subsystem,
                                                   rdpShadowClient *client, UINT16 flags, UINT16 code)
{
    WLog_INFO(TAG, "KEYBOARD UNICODE -- Flags: %#04x (%u), code: %#04x (%u)", flags, flags, code, code);
}

void rdpmux_extended_mouse_event(rdpmuxShadowSubsystem *subsystem,
                                                 rdpShadowClient *client, UINT16 flags, UINT16 x, UINT16 y)
{
    WLog_INFO(TAG, "MOUSE EXTENDED -- Flags: %#04x (%u), x: %u, y: %u", flags, flags, x, y);
}

void rdpmux_keyboard_event(rdpmuxShadowSubsystem *subsystem,
                                           rdpShadowClient *client, UINT16 flags, UINT16 code)
{
    WLog_INFO(TAG, "KEYBOARD -- Flags: %#04x (%u), code: %#04x (%u)", flags, flags, code, code);
}

void rdpmux_mouse_event(rdpmuxShadowSubsystem *subsystem,
                                        rdpShadowClient *client, UINT16 flags, UINT16 x, UINT16 y)
{
    WLog_INFO(TAG, "MOUSE -- Flags: %#04x (%u), x: %u, y: %u", flags, flags, x, y);
}

int rdpmux_subsystem_process_message(rdpmuxShadowSubsystem *system, wMessage *message)
{
    switch(message->id) {
	case SHADOW_MSG_IN_REFRESH_REQUEST_ID:
	    shadow_subsystem_frame_update((rdpShadowSubsystem *) system);
	    break;
	default:
        WLog_ERR(TAG, "Unprocessed message: %u", message->id);
        break;
    }

    if (message->Free) {
	    message->Free(message);
    }

    return 1;
}

void rdpmux_subsystem_update_frame(rdpmuxShadowSubsystem *system, BOOL full)
{
    rdpShadowServer *server = system->server;
    rdpShadowSurface *surface = server->surface;
    RECTANGLE_16 invalidRect, surfaceRect;
    const RECTANGLE_16 *extents = NULL;

    if (ArrayList_Count(server->clients) < 1) {
        return;
    }
    printf("Trying to update\n");
    auto formats = system->listener->GetRDPFormat();
    auto source_format = std::get<0>(formats);
    auto dest_format = std::get<1>(formats);
    auto source_bpp = std::get<2>(formats);

    if (source_format < 0) {
        printf("Invalid source format\n");
        return; // invalid buffer type, don't make the copy
    }

    printf("source format: 0x%08x, dest format: 0x%08x, source bpp: %d\n", source_format, dest_format, source_bpp);

    // for now, force fulls no matter what

    invalidRect.left = 0;
    invalidRect.top = 0;
    invalidRect.right = (UINT16) system->listener->GetWidth();
    invalidRect.bottom = (UINT16) system->listener->GetHeight();

    printf("Invalid rect: %dx%d (%dx%d)\n", invalidRect.left, invalidRect.top, invalidRect.right, invalidRect.bottom);

    surfaceRect.top = 0;
    surfaceRect.left = 0;
    surfaceRect.right = (UINT16) surface->width;
    surfaceRect.bottom = (UINT16) surface->height;

    printf("Surface rect: %dx%d (%dx%d)\n", surfaceRect.left, surfaceRect.top, surfaceRect.right, surfaceRect.bottom);

    region16_union_rect(&(surface->invalidRegion), &(surface->invalidRegion), &invalidRect);
    region16_intersect_rect(&(surface->invalidRegion), &(surface->invalidRegion), &surfaceRect);

    if (!region16_is_empty(&(surface->invalidRegion))) {
        extents = region16_extents(&(surface->invalidRegion));

        auto x = extents->left;
        auto y = extents->top;
        auto width = extents->right - extents->left;
        auto height = extents->bottom - extents->top;

        printf("Debug: %d %d %d %d\n", x, y, width, height);
        WLog_INFO(TAG, "Invalid region: x = %d, y = %d, width = %d, height = %d", x, y, width, height);


        freerdp_image_copy(surface->data,                             /* destination surface */
                           dest_format,                               /* destination surface pixel format */
                           surface->scanline,                         /* destination surface scanline */
                           extents->left,                             /* x coordinate of top left corner of region to copy */
                           extents->top,                              /* y coordinate of top left corner of region to copy */
                           extents->right - extents->left,            /* x coordinate of bottom right corner of region to copy */
                           extents->bottom - extents->top,            /* y coordinate of bottom right corner of region to copy */
                           (BYTE *) system->listener->shm_buffer,     /* source surface to copy data from */
                           source_format,                             /* source surface pixel format */
                           system->listener->GetWidth() * source_bpp, /* scanline of source surface */
                           extents->left,                             /* x coord of top left corner of dirty part of source buffer */
                           extents->top,                              /* y coord of top left corner of dirty part of source buffer */
                           NULL,                                      /* GDI palette to use */
                           FREERDP_FLIP_NONE                          /* transformations to apply */
        );
        shadow_subsystem_frame_update((rdpShadowSubsystem *) system);
        region16_clear(&(surface->invalidRegion));
    } else {
        printf("Debug: empty extents\n");
    }
}

void *rdpmux_subsystem_thread(rdpmuxShadowSubsystem *system)
{
    DWORD nCount = 0;
    HANDLE events[32];
    HANDLE stopEvent = system->server->StopEvent;
    wMessagePipe *msgPipe = system->MsgPipe;
    wMessage message;
    DWORD interval;
    UINT64 frametime;

    events[nCount++] = stopEvent;
    events[nCount++] = MessageQueue_Event(msgPipe->In);

    system->captureFrameRate = 30;
    interval = (DWORD) (1000 / system->captureFrameRate);
    frametime = GetTickCount64() + interval;

    while(1) {
        WaitForMultipleObjects(nCount, events, FALSE, 0);

        if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
            WLog_INFO(TAG, "Server is stopping");
            break;
        }

        if (WaitForSingleObject(MessageQueue_Event(msgPipe->In), 0) == WAIT_OBJECT_0) {
            if (MessageQueue_Peek(msgPipe->In, &message, TRUE)) {
                if (message.id == WMQ_QUIT) {
                    break;
                }
                rdpmux_subsystem_process_message(system, &message);
            }
        }

        if (GetTickCount64() >= frametime)
            rdpmux_subsystem_update_frame(system, FALSE);
    }

    return NULL;
}

int rdpmux_subsystem_start(rdpmuxShadowSubsystem *system)
{
    if (!system)
        return -1;

    if (!CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) rdpmux_subsystem_thread, (void *) system, 0, NULL))
        return -1;

    return 1;
}

int rdpmux_subsystem_stop(rdpmuxShadowSubsystem *system)
{
    WLog_INFO(TAG, "Stop rdpmux subsystem");
    return 1;
}

int rdpmux_subsystem_enum_monitors(MONITOR_DEF *monitors, int maxMonitors)
{
    int numMonitors = 1;
    MONITOR_DEF *monitor = &monitors[0];

    monitor->left = 0;
    monitor->top = 0;
    monitor->right = 4096;
    monitor->bottom = 2048;
    monitor->flags = 1;

    return numMonitors;
}

int rdpmux_subsystem_init(rdpmuxShadowSubsystem *system)
{
    system->numMonitors = rdpmux_subsystem_enum_monitors(system->monitors, 1);
    MONITOR_DEF *virtualScreen = &(system->virtualScreen);

    virtualScreen->left = 0;
    virtualScreen->top = 0;
    virtualScreen->right = 1024;
    virtualScreen->bottom = 768;
    virtualScreen->flags = 1;

    printf("subsystem selectedMonitor is %d\n", system->selectedMonitor);

    return 1;
}

int rdpmux_subsystem_uninit(rdpmuxShadowSubsystem *system)
{
    WLog_INFO(TAG, "Uninit rdpmux subsystem");
    return 1;
}

rdpmuxShadowSubsystem *rdpmux_subsystem_new()
{
    rdpmuxShadowSubsystem *system = static_cast<rdpmuxShadowSubsystem*>(calloc(1, sizeof(rdpmuxShadowSubsystem)));
    if (!system)
	    return NULL;

    system->SynchronizeEvent = (pfnShadowSynchronizeEvent) rdpmux_synchronize_event;
    system->KeyboardEvent = (pfnShadowKeyboardEvent) rdpmux_keyboard_event;
    system->UnicodeKeyboardEvent = (pfnShadowUnicodeKeyboardEvent) rdpmux_unicode_keyboard_event;
    system->ExtendedMouseEvent = (pfnShadowExtendedMouseEvent) rdpmux_extended_mouse_event;
    system->MouseEvent = (pfnShadowMouseEvent) rdpmux_mouse_event;

    system->listener = rdp_listener_object;

    return system;
}

void rdpmux_subsystem_free(rdpmuxShadowSubsystem *system)
{
    free(system);
}

FREERDP_API int RDPMux_ShadowSubsystemEntry(RDP_SHADOW_ENTRY_POINTS *pEntryPoints)
{
    pEntryPoints->New = (pfnShadowSubsystemNew) rdpmux_subsystem_new;
    pEntryPoints->Free = (pfnShadowSubsystemFree) rdpmux_subsystem_free;

    pEntryPoints->Init = (pfnShadowSubsystemInit) rdpmux_subsystem_init;
    pEntryPoints->Uninit = (pfnShadowSubsystemUninit) rdpmux_subsystem_uninit;

    pEntryPoints->Start = (pfnShadowSubsystemStart) rdpmux_subsystem_start;
    pEntryPoints->Stop = (pfnShadowSubsystemStop) rdpmux_subsystem_stop;

    pEntryPoints->EnumMonitors = (pfnShadowEnumMonitors) rdpmux_subsystem_enum_monitors;

    return 1;
}
