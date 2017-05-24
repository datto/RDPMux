//
// Created by sramanujam on 5/23/17.
//

#ifndef RDPMUX_SUBSYSTEM_CPP_H
#define RDPMUX_SUBSYSTEM_CPP_H

#include "RDPListener.h"

typedef struct rdpmux_shadow_subsystem {
    RDP_SHADOW_SUBSYSTEM_COMMON();

    RDPListener *listener;
} rdpmuxShadowSubsystem;

FREERDP_API int RDPMux_ShadowSubsystemEntry(RDP_SHADOW_ENTRY_POINTS *pEntryPoints);

#endif //RDPMUX_SUBSYSTEM_CPP_H
