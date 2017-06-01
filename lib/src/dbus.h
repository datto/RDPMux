//
// Created by sramanujam on 2/3/16.
//

#ifndef SHIM_DBUS_H
#define SHIM_DBUS_H

#include "common.h"
#include "lib/connector.h"


bool mux_get_socket_path(const char *name, const char *obj, char **out_path, int id);


#endif //SHIM_DBUS_H
