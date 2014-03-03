/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <iostream>
#include <boost/asio/ip/host_name.hpp>

#include "control-node/buildinfo.h"
#include "base/contrail_ports.h"
#include "base/logging.h"
#include "base/misc_utils.h"
#include "base/util.h"

#include "options.h"

using namespace std;
using namespace boost::asio::ip;
namespace opt = boost::program_options;

Options::Options(EventManager &evm) :
       log_disable_(false),
       log_local_(false),
       test_mode_(false) {
    boost::system::error_code error;
    hostname_ = host_name(error);
    host_ip_ = GetHostIp(evm.io_service(), hostname_);
}

bool Options::Parse(int argc, char *argv[]) {
    opt::options_description cmdline_options("Allowed options");
    Initialize(cmdline_options);

    try {
        Process(argc, argv, cmdline_options);
        return true;
    } catch (boost::program_options::error &e) {
        LOG(ERROR, "Error " << e.what());
        cout << "Error " << e.what() << endl;
    } catch (...) {
        LOG(ERROR, "Options Parser: Caught fatal unknown exception");
        cout << "Options Parser: Caught fatal unknown exception" << endl;
    }

    return false;
}

void Options::Initialize(opt::options_description &cmdline_options) {

    opt::options_description generic("Generic options");

    // Command line only options.
    generic.add_options()
        ("conf_file", opt::value<string>()->default_value(
                                            "/etc/contrail/control-node.conf"),
             "Configuration file")
         ("help", "help message")
        ("version", "Display version information")
    ;

    // Command line and config file options.
    opt::options_description config("Configuration options");
    config.add_options()
        ("COLLECTOR.port", opt::value<uint16_t>()->default_value(
                                                ContrailPorts::CollectorPort),
             "Port of sandesh collector")
        ("COLLECTOR.server",
             opt::value<string>()->default_value(collector_server_),
             "IP address of sandesh collector")

        ("DEFAULT.bgp_config_file",
             opt::value<string>()->default_value("bgp_config.xml"),
             "BGP Configuration file")
        ("DEFAULT.bgp_port",
             opt::value<uint16_t>()->default_value(ContrailPorts::ControlBgp),
             "BGP listener port")

        ("DEFAULT.hostip", opt::value<string>(), "IP address of control-node")
        ("DEFAULT.hostname", opt::value<string>()->default_value(hostname_),
             "Hostname of control-node")
        ("DEFAULT.http_server_port",
             opt::value<uint16_t>()->default_value(
                                         ContrailPorts::HttpPortControl),
             "Sandesh HTTP listener port")

        ("DEFAULT.log_category",
             opt::value<string>()->default_value(log_category_),
             "Category filter for local logging of sandesh messages")
        ("DEFAULT.log_disable", opt::bool_switch(&log_disable_),
             "Disable sandesh logging")
        ("DEFAULT.log_file", opt::value<string>()->default_value("<stdout>"),
             "Filename for the logs to be written to")
        ("DEFAULT.log_file_index",
             opt::value<int>()->default_value(10),
             "Maximum log file roll over index")
        ("DEFAULT.log_file_size",
             opt::value<long>()->default_value(10*1024*1024),
             "Maximum size of the log file")
        ("DEFAULT.log_level", opt::value<string>()->default_value("SYS_NOTICE"),
             "Severity level for local logging of sandesh messages")
        ("DEFAULT.log_local", opt::bool_switch(&log_local_),
             "Enable local logging of sandesh messages")
        ("DEFAULT.test-mode", opt::bool_switch(&test_mode_),
             "Enable control-node to run in test-mode")

        ("DEFAULT.xmpp_server_port",
             opt::value<uint16_t>()->default_value(ContrailPorts::ControlXmpp),
             "XMPP listener port")

        ("DISCOVERY.port", opt::value<uint16_t>()->default_value(
                                            ContrailPorts::DiscoveryServerPort),
             "Port of Discovery Server")
        ("DISCOVERY.server",
             opt::value<string>()->default_value(discovery_server_),
             "IP address of Discovery Server")

        ("IFMAP.certs_store", opt::value<string>()->default_value(""),
             "Certificates store to use for communication with IFMAP server")
        ("IFMAP.password", opt::value<string>()->default_value(
                                                     "control_user_passwd"),
             "IFMAP server password")
        ("IFMAP.server_url",
             opt::value<string>()->default_value(ifmap_server_url_),
             "IFMAP server URL")
        ("IFMAP.user", opt::value<string>()->default_value("control_user"),
             "IFMAP server username")
        ;

    config_file_options_.add(config);
    cmdline_options.add(generic).add(config);
}

template <typename ValueType>
void Options::GetOptValue(const boost::program_options::variables_map &var_map,
                          ValueType &var, std::string val, ValueType emptyValue) {

    // Check if the value is present.
    if (var_map.count(val)) {
        var = var_map[val].as<ValueType>();
    }
}

void Options::Process(int argc, char *argv[],
        opt::options_description &cmdline_options) {
    // Process options off command line first.
    opt::variables_map var_map;
    opt::store(opt::parse_command_line(argc, argv, cmdline_options), var_map);

    // Process options off configuration file.
    GetOptValue<string>(var_map, config_file_, "conf_file", "");
    ifstream config_file_in;
    config_file_in.open(config_file_.c_str());
    if (config_file_in.good()) {
        opt::store(opt::parse_config_file(config_file_in, config_file_options_),
                   var_map);
    }
    config_file_in.close();

    opt::notify(var_map);

    if (var_map.count("help")) {
        cout << cmdline_options << endl;
        exit(0);
    }

    if (var_map.count("version")) {
        string build_info;
        cout << MiscUtils::GetBuildInfo(MiscUtils::ControlNode, BuildInfo,
                                        build_info) << endl;
        exit(0);
    }

    GetOptValue<string>(var_map,
            bgp_config_file_, "DEFAULT.bgp_config_file", "");
    GetOptValue<uint16_t>(var_map, bgp_port_, "DEFAULT.bgp_port", 0);
    GetOptValue<uint16_t>(var_map, collector_port_, "COLLECTOR.port", 0);
    GetOptValue<string>(var_map, collector_server_, "COLLECTOR.server", "");

    GetOptValue<string>(var_map, host_ip_, "DEFAULT.hostip", "");
    GetOptValue<string>(var_map, hostname_, "DEFAULT.hostname", "");

    GetOptValue<uint16_t>(var_map, http_server_port_,
            "DEFAULT.http_server_port", 0);

    GetOptValue<string>(var_map, log_category_, "DEFAULT.log_category", "");
    GetOptValue<string>(var_map, log_file_, "DEFAULT.log_file", "");
    GetOptValue<int>(var_map, log_file_index_, "DEFAULT.log_file_index", 0);
    GetOptValue<long>(var_map, log_file_size_, "DEFAULT.log_file_size", 0);
    GetOptValue<string>(var_map, log_level_, "DEFAULT.log_level", "");
    GetOptValue<uint16_t>(var_map, xmpp_port_, "DEFAULT.xmpp_server_port", 0);

    GetOptValue<uint16_t>(var_map, discovery_port_, "DISCOVERY.port", 0);
    GetOptValue<string>(var_map, discovery_server_, "DISCOVERY.server", "");


    GetOptValue<string>(var_map, ifmap_password_, "IFMAP.password", "");
    GetOptValue<string>(var_map, ifmap_server_url_, "IFMAP.server_url", "");
    GetOptValue<string>(var_map, ifmap_user_, "IFMAP.user", "");
    GetOptValue<string>(var_map, ifmap_certs_store_, "IFMAP.certs_store", "");
}
