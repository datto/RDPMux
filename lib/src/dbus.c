/** @file */
#include "dbus.h"

/*
 * remember that path has to be at least 4096 or above, even though that
 * really doesn't mean anything. Hopefully nobody makes the server
 * put sockets in a ridiculous place, though that will probably happen sometime.
 */

/**
 * @brief Gets the socket path from the DBus service passed in.
 *
 * This function will negotiate the registration of the VM with the DBus service passed into this function.
 * More information about the DBus service's archetype and such is available at some link that I haven't written yet.
 * @todo Write that stuff about DBus
 *
 * @returns Success
 *
 * @param name The well-known name of the DBus service
 * @param obj The object path of the DBus service
 * @param out_path The path to the VM's private communication socket returned by the DBus service.
 * @param id The ID of the VM.
 */
__PUBLIC bool mux_get_socket_path(const char *name, const char *obj, char **out_path, int id)
{
    if (!obj)
        return false;

    GError *error = NULL;
    GVariant *protocol_versions = NULL,
            *unwrapped_version = NULL;
    GVariantIter *iter = NULL;
    int proto = -1;

    MuxOrgRDPMuxRDPMux *proxy;
    proxy = mux_org_rdpmux_rdpmux_proxy_new_for_bus_sync(
            G_BUS_TYPE_SYSTEM,
            G_DBUS_PROXY_FLAGS_NONE,
            name,
            obj,
            NULL,
            &error);

    if (error != NULL) {
        mux_printf_error("could not instantiate dbus proxy: %s", error->message);
        g_error_free(error);
        return false;
    }
    assert(proxy != NULL);

    // get the list of supported protocol versions, and check if our version is in there
    protocol_versions = mux_org_rdpmux_rdpmux_get_supported_protocol_versions(proxy);
    if (protocol_versions == NULL) {
        mux_printf_error("could not communicate with remote dbus service");
        return false;
    }
    unwrapped_version = g_variant_get_child_value(protocol_versions, 0);
    if (unwrapped_version == NULL) {
        mux_printf_error("could not comumnicate with remote dbus service");
        return false;
    }

    const GVariantType *type = g_variant_get_type(unwrapped_version);

    // we begin now, to extract meaning from our gvariants
    if (g_variant_type_is_array(type)) {
        const GVariantType *ele_type = g_variant_type_element(type);

        if (g_variant_type_equal(ele_type, G_VARIANT_TYPE_INT32)) {

            iter = g_variant_iter_new(unwrapped_version);
            if (iter == NULL) {
                mux_printf_error("Could not parse DBus return message");
                return false;
            }

            GVariant *child;
            while ((child = g_variant_iter_next_value(iter))) {
                if (g_variant_type_equal(child, G_VARIANT_TYPE_INT32)) {
                    if (g_variant_get_int32(child) == RDPMUX_PROTOCOL_VERSION) {
                        proto = g_variant_get_int32(child);
                        goto proto_found;
                    }
                }
            }

            mux_printf_error("DBus protocol not found!\n");
            return false;

        } else {
            mux_printf_error("Don't know how to handle variant type %s, bailing", (char *) ele_type);
            return false;
        }
    } else {
        if (g_variant_type_equal(type, G_VARIANT_TYPE_INT32)) {
            proto = g_variant_get_int32(unwrapped_version);
        }
    }

proto_found:
    if (proto != RDPMUX_PROTOCOL_VERSION) {
        mux_printf_error("Protocol mismatch with RDPMux server, %d vs %d", proto, RDPMUX_PROTOCOL_VERSION);
        return false;
    }

    if (!mux_org_rdpmux_rdpmux_call_register_sync(proxy, id, RDPMUX_PROTOCOL_VERSION, display->uuid,
                                                  out_path, NULL, &error)) {
        mux_printf_error("could not retrieve socket path: %s", error->message);
        g_error_free(error);
        return false;
    }
    assert(*out_path != NULL);
    display->vm_id = id;
    return true;
}

