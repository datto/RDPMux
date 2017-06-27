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

#include "common.h"
#include <boost/program_options.hpp>
#include "RDPServerWorker.h"

namespace po = boost::program_options;
po::variables_map vm;

INITIALIZE_EASYLOGGINGPP

std::unique_ptr<RDPServerWorker> broker;

namespace {
    static Glib::RefPtr<Gio::DBus::NodeInfo> introspection_data;

    static Glib::ustring introspection_xml =
            "<node>"
            "  <interface name='org.RDPMux.RDPMux'>"
            "    <method name='Register'>"
            "      <arg type='i' name='id' direction='in'/>"
            "      <arg type='i' name='version' direction='in' />"
            "      <arg type='s' name='uuid' direction='in' />"
            "      <arg type='q' name='port' direction='in' />"
            "      <arg type='s' name='socket_path' direction='out'/>"
            "    </method>"
            "    <property type='ai' name='SupportedProtocolVersions' access='read' />"
            "  </interface>"
            "</node>";
    guint registered_id = 0;
} // anonymous namespace

/* Signal handlers */

void handle_SIGINT(int sig)
{
    // clean up RDPServerWorker instance, this will trigger cleanup of all listeners
    LOG(INFO) << "SIGINT received, cleaning up";
    if (broker)
        broker.reset();

    // re-raise signal
    signal(sig, SIG_DFL);
    raise(sig);
}

// handles all method call invocations. Basically if you have more than one
// you need to use a giant if/else if tree to handle them. Ugly.
static void on_method_call(const Glib::RefPtr<Gio::DBus::Connection>& conn,
        const Glib::ustring&, // sender
        const Glib::ustring&, // object_path
        const Glib::ustring&, // interface_name
        const Glib::ustring& method_name,
        const Glib::VariantContainerBase& parameters,
        const Glib::RefPtr<Gio::DBus::MethodInvocation>& invocation)
{
    if (method_name == "Register") {
        Glib::Variant<int> id_variant;
        Glib::Variant<int> ver_variant;
        Glib::Variant<std::string> uuid_variant;
        Glib::Variant<uint16_t> port_variant;

        parameters.get_child(id_variant, 0);
        parameters.get_child(ver_variant, 1);
        parameters.get_child(uuid_variant, 2);
        parameters.get_child(port_variant, 3);
        int vm_id = id_variant.get();
        int ver = ver_variant.get();
        std::string uuid = uuid_variant.get();
        uint16_t port = port_variant.get();

        if (ver != RDPMUX_PROTOCOL_VERSION) {
            invocation->return_value(
                    Glib::VariantContainerBase::create_tuple(
                            Glib::Variant<Glib::ustring>::create("")
                    )
            );
            LOG(INFO) << "Client tried to connect using unsupported protocol version, ignoring";
            return;
        }

        if (!broker->RegisterNewVM(uuid, vm_id, port)) {
            LOG(WARNING) << "VM Registration failed!";
            invocation->return_value(
                    Glib::VariantContainerBase::create_tuple(
                            Glib::Variant<Glib::ustring>::create("")
                    )
            );
            return;
        }

        Glib::ustring g_res = "ipc://@/tmp/rdpmux";
        const auto response_variant = Glib::Variant<Glib::ustring>::create(g_res);
        Glib::VariantContainerBase response = Glib::VariantContainerBase::create_tuple(response_variant);

        invocation->return_value(response);
    }
}

// handles all property requests. again with the giant if/else statements, oy.
static void on_property_call(Glib::VariantBase& property,
        const Glib::RefPtr<Gio::DBus::Connection>&,
        const Glib::ustring&, // sender
        const Glib::ustring&, // object path
        const Glib::ustring&, // interface_name
        const Glib::ustring& property_name)
{
    if (property_name == "SupportedProtocolVersions") {
        // pretty inextensible way to do this, but since we have no other versions, it'll suffice for now.
        auto versions = std::vector<int>();
        versions.push_back(RDPMUX_PROTOCOL_VERSION);
        auto ver_var = Glib::Variant<std::vector<int>>::create(versions);
        property = ver_var;
    }
}

// TODO: learn how this works so that we can expose multiple methods and/or multiple objects
const Gio::DBus::InterfaceVTable interface_vtable(sigc::ptr_fun(&on_method_call), sigc::ptr_fun(&on_property_call));

void on_bus_acquired(const Glib::RefPtr<Gio::DBus::Connection> &connection, const Glib::ustring& /* name */)
{
    // Export main object to the bus
    VLOG(1) << "Now registering object on system bus";

    try {
        registered_id = connection->register_object("/org/RDPMux/RDPMux",
                introspection_data->lookup_interface(),
                interface_vtable);
    } catch (const Glib::Error &ex) {
        LOG(WARNING) << "DBus registration failed, bailing. Reason: " << ex.what();
        exit(129);
    }
    broker->setDBusConnection(connection);
    LOG(INFO) << "RDPMux initialized successfully!";
    return;
}

void on_name_acquired(const Glib::RefPtr<Gio::DBus::Connection>& /* connection */, const Glib::ustring& /* name */)
{
    // does nothing, nobody knows why this callback even exists
}

void on_name_lost(const Glib::RefPtr<Gio::DBus::Connection> &connection, const Glib::ustring& /* name */)
{
    connection->unregister_object(registered_id);
}

void process_options(const int argc, const char *argv[])
{
    po::basic_command_line_parser<char> parser(argc, argv);
    try {
        po::options_description desc("Usage");

        // set up default path to be in world-rw tmp dir
        std::string suffix = "/rdpmux/rdpmux.sock";
        std::string tmp_dir_path = Glib::get_tmp_dir() + suffix;

        desc.add_options()
                (
                        "help,h",
                        "Show help."
                )
                (
                        "v,v",
                        "Enable verbose output."
                )
                (
                        "port,p",
                        po::value<uint16_t>()->default_value(3901),
                        "Port to begin spawning listeners on."
                )
                (
                        "no-auth,n",
                        po::bool_switch()->default_value(false),
                        "Disable authentication for peer connections"
                )
                (
                        "config-path,c",
                        po::value<std::string>()->default_value("/etc/rdpmux"),
                        "Configuration directory path"
                );
        po::basic_parsed_options<char> parsed = parser.options(desc).allow_unregistered().run();
        po::store(parsed, vm);


        if (vm.count("help")) {
            std::cout << desc << "\n";
            exit(0);
        }

        po::notify(vm);
        std::string config_path = vm["config-path"].as<std::string>();
        LOG(INFO) << "Config path is " << config_path;
    } catch (const std::exception &ex) {
        LOG(WARNING) << ex.what();
        exit(1);
    }
}


int main(int argc, const char* argv[])
{
    process_options(argc, argv);
    START_EASYLOGGINGPP(argc, argv);

    Gio::init();

    if (!Glib::thread_supported())
        Glib::thread_init();

    std::signal(SIGINT, handle_SIGINT);

    try {
        introspection_data = Gio::DBus::NodeInfo::create_for_xml(introspection_xml);
    } catch (const Glib::Error &ex) {
        LOG(FATAL) << "Unable to create introspection data: " << ex.what() << ".";
        return 1;
    }

    auto port = vm["port"].as<uint16_t>();
    bool auth = !vm["no-auth"].as<bool>(); // take the opposite of no-auth to determine whether to auth connections

    // final check to make sure starting port is within bounds
    if (port > 0 && port < 65535) {
        if (port < 1024) {
            LOG(WARNING) << "Port number is low (below 1024), may conflict with other system services!";
        }
        try {
            broker = make_unique<RDPServerWorker>(port, auth); // create broker
        } catch (std::exception &e) {
            LOG(FATAL) << "Error initializing socket: " << e.what();
            return 1;
        }
    } else {
        LOG(FATAL) << "Invalid port number " << port;
        return 1;
    }

    if (!broker->Initialize()) {
        LOG(FATAL) << "Could not initialize RDPServerWorker, exiting";
        return 1;
    }

    // take the well-known name on the specified bus.
    const auto id = Gio::DBus::own_name(Gio::DBus::BUS_TYPE_SYSTEM,
            "org.RDPMux.RDPMux",
            sigc::ptr_fun(&on_bus_acquired),
            sigc::ptr_fun(&on_name_acquired),
            sigc::ptr_fun(&on_name_lost));

    auto loop = Glib::MainLoop::create();
    VLOG(1) << "MAIN: Now starting glib main loop!";
    loop->run();

    Gio::DBus::unown_name(id);
    return EXIT_SUCCESS;
}
