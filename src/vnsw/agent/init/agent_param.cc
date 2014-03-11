/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

/*
 * Agent parameters are derived from 3 entities in increasing priority,
 * - System information
 * - Configuration file
 * - Parameters
 */

#include <fstream>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>

#include <sys/stat.h>

#include <boost/asio/ip/host_name.hpp>
#include <boost/program_options.hpp>
#include <base/contrail_ports.h>

#include <base/logging.h>
#include <base/misc_utils.h>
#include <base/util.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>

#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <vgw/cfg_vgw.h>

#include <uve/agent_stats_collector.h>
#include <uve/flow_stats_collector.h>

using namespace std;
using boost::optional;
namespace opt = boost::program_options;

template <typename ValueType>
void AgentParam::GetOptValue(ValueType &var, std::string val) {

    // Check if the value is present.
    if (var_map_.count(val)) {
        var = var_map_[val].as<ValueType>();
    }
}

bool AgentParam::GetIpAddress(const string &str, Ip4Address *addr) {
    boost::system::error_code ec;
    if (str.empty()) {
        return false;
    }
    Ip4Address tmp = Ip4Address::from_string(str, ec);
    if (ec.value() != 0) {
        return false;
    }
    *addr = tmp;
    return true;
}

bool AgentParam::ConfigToIpAddress(const string &key, Ip4Address *addr) {
    string ip_str;
    GetOptValue<string>(ip_str, key);
    return GetIpAddress(ip_str, addr);
}

#if 0
static void ParseFlowTimeout(const ptree &node,
                             const string &config_file,
                             uint32_t *flow_cache_timeout) {
    try {
        optional<unsigned int> opt_str;
        if (opt_str = node.get_optional<unsigned int>
                      ("config.agent.flow-cache.timeout")) {
            *flow_cache_timeout = opt_str.get();
        } else {
            *flow_cache_timeout = Agent::kDefaultFlowCacheTimeout;
        }
    } catch (exception &e) {
        LOG(ERROR, "Error reading \"flow-cache\" node in config file <"
            << config_file << ">. Error <" << e.what() << ">");
    }
}
#endif

// Initialize hypervisor mode based on system information
// If "/proc/xen" exists it means we are running in Xen dom0
void AgentParam::InitFromSystem() {
    boost::system::error_code error;
    host_name_ = boost::asio::ip::host_name(error);

    struct stat fstat;
    if (stat("/proc/xen", &fstat) == 0) {
        mode_ = MODE_XEN;
        LOG(INFO, "Found file /proc/xen. Initializing mode to XEN");
    }
    xen_ll_.addr_ = Ip4Address::from_string("169.254.0.1");
    xen_ll_.plen_ = 16;

    return;
}

// Update agent parameters from config file
void AgentParam::InitFromCmdLineAndConfig() {
    string xen_ll_ip_address;
    string mode = "kvm";

    GetOptValue<string>(vhost_.name_, "VHOST.name");
    ConfigToIpAddress("VHOST.ip", &vhost_.addr_);
    GetOptValue<int>(vhost_.plen_, "VHOST.ip-prefix");
    ConfigToIpAddress("VHOST.gateway", &vhost_.gw_);
    GetOptValue<string>(eth_port_, "PHYSICAL.name");

    //ParseFlowTimeout(tree, config_file_, &flow_cache_timeout_);

    ConfigToIpAddress("DISCOVERY-SERVER.ip", &dss_server_);
    GetOptValue<int>(xmpp_instance_count_, 
                     "DISCOVERY-SERVER.control-instances");

    if (var_map_.count("DEFAULT.log_local")) {
        log_local_ = true;
    }
    GetOptValue<uint16_t>(http_server_port_, "DEFAULT.http-server-port");
    GetOptValue<string>(host_name_, "DEFAULT.hostname");
    ConfigToIpAddress("DEFAULT.control-ip", &mgmt_ip_);
    GetOptValue<string>(tunnel_type_, "DEFAULT.tunnel-type");
    if ((tunnel_type_ != "MPLSoUDP") && (tunnel_type_ != "VXLAN")) {
        tunnel_type_ = "MPLSoGRE";
    }
    GetOptValue<string>(metadata_shared_secret_, 
                        "DEFAULT.metadata-proxy-secret");
    GetOptValue<uint16_t>(flow_cache_timeout_, "DEFAULT.flow-cache-timeout");

    GetOptValue<string>(log_category_, "DEFAULT.log_category");
    GetOptValue<string>(log_file_, "DEFAULT.log_file");
    GetOptValue<string>(log_level_, "DEFAULT.log_level");

    GetOptValue<uint16_t>(collector_port_, "COLLECTOR.port");
    ConfigToIpAddress("COLLECTOR.server", &collector_);

    ConfigToIpAddress("XMPP-SERVER.ip1", &xmpp_server_1_);
    ConfigToIpAddress("XMPP-SERVER.ip2", &xmpp_server_2_);

    ConfigToIpAddress("DNS-SERVER.ip1", &dns_server_1_);
    ConfigToIpAddress("DNS-SERVER.ip2", &dns_server_2_);

    GetOptValue<uint32_t>(linklocal_system_flows_, 
                          "LINK-LOCAL.max-system-flows");
    GetOptValue<uint32_t>(linklocal_vm_flows_, 
                          "LINK-LOCAL.max-vm-flows");

    GetOptValue<string>(mode, "HYPERVISOR.type");
    if (mode == "xen") {
        GetOptValue<string>(xen_ll_.name_, "HYPERVISOR.xen-ll-port");
        GetOptValue<string>(xen_ll_ip_address, "HYPERVISOR.xen-ll-ip-address");
        GetOptValue<int>(xen_ll_.plen_, "HYPERVISOR.xen-ll-prefix-len");
        mode_ = AgentParam::MODE_XEN;

        if (!GetIpAddress(xen_ll_ip_address, &xen_ll_.addr_)) {
            LOG(ERROR, "Error parsing xen-ll-ip-address");
            exit(EINVAL);
        }

        if (var_map_.count("HYPERVISOR.xen-ll-prefix-len")) {
            if (xen_ll_.plen_ <= 0 || xen_ll_.plen_ >= 32) {
                LOG(ERROR, "Error parsing argument for xen-ll-prefix-len");
                exit(EINVAL);
            }
        }
    } else if (mode == "vmware") {
        GetOptValue<string>(vmware_physical_port_, 
                            "HYPERVISOR.vmware-physical-port");
        mode_ = AgentParam::MODE_VMWARE;
    } else {
        mode_ = AgentParam::MODE_KVM;
    }
    LOG(DEBUG, "Config file <" << config_file_ << "> read successfully.");
    return;
}

// Update linklocal max flows if they are greater than the max allowed for the
// process. Also, ensure that the process is allowed to open upto
// linklocal_system_flows + kMaxOtherOpenFds files
void AgentParam::ValidateLinkLocalFlows() {
    struct rlimit rl;
    int result = getrlimit(RLIMIT_NOFILE, &rl);
    if (result == 0) {
        if (rl.rlim_max <= Agent::kMaxOtherOpenFds + 1) {
            LOG(DEBUG, "Updating linklocal flows configuration to 0");
            linklocal_system_flows_ = linklocal_vm_flows_ = 0;
            return;
        }
        if (linklocal_system_flows_ > rl.rlim_max - Agent::kMaxOtherOpenFds - 1) {
            linklocal_system_flows_ = rl.rlim_max - Agent::kMaxOtherOpenFds - 1;
            LOG(DEBUG, "Updating linklocal-system-flows configuration to : " <<
                linklocal_system_flows_);
        }
        if (rl.rlim_cur < linklocal_system_flows_ + Agent::kMaxOtherOpenFds + 1) {
            struct rlimit new_rl;
            new_rl.rlim_max = rl.rlim_max;
            new_rl.rlim_cur = linklocal_system_flows_ + Agent::kMaxOtherOpenFds + 1;
            result = setrlimit(RLIMIT_NOFILE, &new_rl);
            if (result != 0) {
                if (rl.rlim_cur <= Agent::kMaxOtherOpenFds + 1) {
                    linklocal_system_flows_ = 0;
                } else {
                    linklocal_system_flows_ = rl.rlim_cur - Agent::kMaxOtherOpenFds - 1;
                }
                LOG(DEBUG, "Unable to set Max open files limit to : " <<
                    new_rl.rlim_cur <<
                    " Updating linklocal-system-flows configuration to : " <<
                    linklocal_system_flows_);
            }
        }
        if (linklocal_vm_flows_ > linklocal_system_flows_) {
            linklocal_vm_flows_ = linklocal_system_flows_;
            LOG(DEBUG, "Updating linklocal-vm-flows configuration to : " <<
                linklocal_vm_flows_);
        }
    } else {
        LOG(DEBUG, "Unable to validate linklocal flow configuration");
    }
}

void AgentParam::Validate() {
    // Validate vhost_name
    if (vhost_.name_ == "") {
        LOG(ERROR, "Configuration error. vhost interface name not specified");
        exit(EINVAL);
    }

    // Validate ethernet port
    if (eth_port_ == "") {
        LOG(ERROR, "Configuration error. eth_port not specified");
        exit(EINVAL);
    }

    // Set the prefix address for VHOST and XENLL interfaces
    uint32_t mask = vhost_.plen_ ? (0xFFFFFFFF << (32 - vhost_.plen_)) : 0;
    vhost_.prefix_ = Ip4Address(vhost_.addr_.to_ulong() & mask);

    mask = xen_ll_.plen_ ? (0xFFFFFFFF << (32 - xen_ll_.plen_)) : 0;
    xen_ll_.prefix_ = Ip4Address(xen_ll_.addr_.to_ulong() & mask);

    ValidateLinkLocalFlows();
}

void AgentParam::Init(int argc, char *argv[]) {
    program_name_ = argv[0];

    try {
        Parse(argc, argv);
        InitFromSystem();
        InitFromCmdLineAndConfig();
        Validate();
        vgw_config_table_->Init(var_map_);
    } catch (boost::program_options::error &e) {
        cout << "Error " << e.what() << endl;
    } catch (...) {
        cout << "Options Parser: Caught fatal unknown exception" << endl;
    }

    LogConfig();
}

void AgentParam::LogConfig() const {
    LOG(DEBUG, "vhost interface name        : " << vhost_.name_);
    LOG(DEBUG, "vhost IP Address            : " << vhost_.addr_.to_string() 
        << "/" << vhost_.plen_);
    LOG(DEBUG, "vhost gateway               : " << vhost_.gw_.to_string());
    LOG(DEBUG, "Ethernet port               : " << eth_port_);
    LOG(DEBUG, "XMPP Server-1               : " << xmpp_server_1_);
    LOG(DEBUG, "XMPP Server-2               : " << xmpp_server_2_);
    LOG(DEBUG, "DNS Server-1                : " << dns_server_1_);
    LOG(DEBUG, "DNS Server-2                : " << dns_server_2_);
    LOG(DEBUG, "Discovery Server            : " << dss_server_);
    LOG(DEBUG, "Controller Instances        : " << xmpp_instance_count_);
    LOG(DEBUG, "Tunnel-Type                 : " << tunnel_type_);
    LOG(DEBUG, "Metadata-Proxy Shared Secret: " << metadata_shared_secret_);
    LOG(DEBUG, "Linklocal Max System Flows  : " << linklocal_system_flows_);
    LOG(DEBUG, "Linklocal Max Vm Flows      : " << linklocal_vm_flows_);
    LOG(DEBUG, "Flow cache timeout          : " << flow_cache_timeout_);
    if (mode_ == MODE_KVM) {
    LOG(DEBUG, "Hypervisor mode             : kvm");
        return;
    }

    if (mode_ == MODE_XEN) {
    LOG(DEBUG, "Hypervisor mode             : xen");
    LOG(DEBUG, "XEN Link Local port         : " << xen_ll_.name_);
    LOG(DEBUG, "XEN Link Local IP Address   : " << xen_ll_.addr_.to_string()
        << "/" << xen_ll_.plen_);
    }

    if (mode_ == MODE_VMWARE) {
    LOG(DEBUG, "Hypervisor mode             : vmware");
    LOG(DEBUG, "Vmware port                 : " << vmware_physical_port_);
    }
}

AgentParam::AgentParam() :
        vhost_(), eth_port_(), xmpp_instance_count_(), xmpp_server_1_(),
        xmpp_server_2_(), dns_server_1_(), dns_server_2_(), dss_server_(),
        mgmt_ip_(), mode_(MODE_KVM), xen_ll_(), tunnel_type_(),
        metadata_shared_secret_(), linklocal_system_flows_(),
        linklocal_vm_flows_(), flow_cache_timeout_(), 
        config_file_(Agent::DefaultConfigFile()), program_name_(),
        log_file_(), log_local_(false), log_level_(), log_category_(),
        collector_(), collector_port_(), http_server_port_(), host_name_(),
        disable_vhost_(false), disable_ksync_(false), disable_services_(false),
        disable_packet_services_(false),
        agent_stats_interval_(AgentStatsCollector::AgentStatsInterval), 
        flow_stats_interval_(FlowStatsCollector::FlowStatsInterval),
        vmware_physical_port_(""), cmdline_options_("Allowed options"), 
        config_file_options_() {

    vgw_config_table_ = std::auto_ptr<VirtualGatewayConfigTable>
        (new VirtualGatewayConfigTable());

    boost::system::error_code error;

    string hostname(boost::asio::ip::host_name(error));
    uint16_t default_collector_port = ContrailPorts::CollectorPort;
    uint16_t default_http_port = ContrailPorts::HttpPortAgent;
    uint32_t default_max_flows = Agent::kDefaultMaxLinkLocalOpenFds;

    // Command line only options.
    opt::options_description generic("Generic options");
    generic.add_options()
        ("conf-file", opt::value<string>()->default_value
         (Agent::DefaultConfigFile()), "Configuration file")
        ("help", "help message")
        ("version", "Display version information")
    ;

    // Command line and config file options.
    opt::options_description config("Configuration options");
    config.add_options()
       ("COLLECTOR.port", opt::value<uint16_t>()->default_value
            (default_collector_port),
            "Port of sandesh collector")
       ("COLLECTOR.server", opt::value<string>()->default_value(""),
            "IP address of sandesh collector")

       ("DEFAULT.hostname", opt::value<string>()->default_value(hostname),
            "Specific Host Name")
       ("DEFAULT.http-server-port",
            opt::value<uint16_t>()->default_value(default_http_port),
            "Sandesh HTTP listener port")
       ("DEFAULT.control-ip", opt::value<string>()->default_value(""),
            "IP address for control channel")
       ("DEFAULT.tunnel-type", opt::value<string>()->default_value(""),
            "Tunnel type for encapsulation")
       ("DEFAULT.metadata-proxy-secret", opt::value<string>()->default_value(""),
            "Metadata Proxy Shared secret")
       ("DEFAULT.flow-cache-timeout",
            opt::value<uint16_t>()->default_value(Agent::kDefaultFlowCacheTimeout),
            "Flow Age time in seconds")

       ("DEFAULT.log_category", opt::value<string>()->default_value(""),
           "Category filter for local logging of sandesh messages")
       ("DEFAULT.log_file", opt::value<string>()->default_value("<stdout>"),
        "Filename for the logs to be written to")
       ("DEFAULT.log_level", opt::value<string>()->default_value("SYS_DEBUG"),
           "Severity level for local logging of sandesh messages")
       ("DEFAULT.log_local", opt::bool_switch(&log_local_),
            "Enable local logging of sandesh messages")

       ("HYPERVISOR.type", opt::value<string>()->default_value("kvm"),
            "Type of hypervisor <kvm|xen>")
       ("HYPERVISOR.xen-ll-port",
           opt::value<string>()->default_value(""), 
           "Port name on host for link-local network")
       ("HYPERVISOR.xen-ll-ip-address",
            opt::value<string>()->default_value(""),
            "IP Address for the link local port")
       ("HYPERVISOR.xen-ll-prefix-len",
            opt::value<int>()->default_value(0),
            "IP Prefix Length for the link local address")
       ("HYPERVISOR.vmware-physical-port",
            opt::value<string>()->default_value(""), 
            "Physical port used to connect to VMs in VMWare environement")

       ("KERNEL.disable-vhost", opt::bool_switch(&disable_vhost_),
            "Disable vhost interface")
       ("KERNEL.disable-ksync", opt::bool_switch(&disable_ksync_),
            "Disable kernel synchronization")
       ("KERNEL.disable-services", opt::bool_switch(&disable_services_),
            "Disable services")
       ("KERNEL.disable-packet", opt::bool_switch(&disable_packet_services_),
            "Disable packet services")
       
       ("VHOST.name", opt::value<string>()->default_value("vhost0"),
            "Name of virtual host interface")
       ("VHOST.ip", opt::value<string>()->default_value(""),
            "IP Address of virtual host interface")
       ("VHOST.ip-prefix", opt::value<int>()->default_value(0),
            "IP Prefix Length of virtual host interface")
       ("VHOST.gateway", opt::value<string>()->default_value(""),
            "Gateway IP Address of virtual host interface")

       ("PHYSICAL.name", opt::value<string>()->default_value(""),
            "Name of physical interface")


       ("DISCOVERY-SERVER.ip", opt::value<string>()->default_value(""),
            "IP Address of Discovery Server")
       ("DISCOVERY-SERVER.control-instances", opt::value<int>()->default_value(0),
            "Control instances count")

       ("DNS-SERVER.ip1", opt::value<string>()->default_value(""),
            "IP Address of Primary DNS Server")
       ("DNS-SERVER.ip2", opt::value<string>()->default_value(""),
            "IP Address of Secondary DNS Server")

       ("XMPP-SERVER.ip1", opt::value<string>()->default_value(""),
            "IP Address of Primary XMPP Server")
       ("XMPP-SERVER.ip2", opt::value<string>()->default_value(""),
            "IP Address of Secondary XMPP Server")

       ("LINK-LOCAL.max-system-flows", opt::value<uint32_t>()->default_value
            (default_max_flows),
            "Maximum link local flows in the system")
       ("LINK-LOCAL.max-vm-flows", opt::value<uint32_t>()->default_value
            (default_max_flows),
            "Maximum link local flows per VM")

       ("GATEWAY.vn-interface-subnet", opt::value< std::vector<std::string> >(),
            "List of Gateway Virtual Network, interface and subnet")
       ("GATEWAY.vn-interface-route", opt::value< std::vector<std::string> >(),
            "List of Gateway Virtual Network, interface and route")
       ;

    config_file_options_.add(config);
    cmdline_options_.add(generic).add(config);
}

void AgentParam::Parse(int argc, char *argv[]) {
    // Process options off command line first.
    opt::store(opt::parse_command_line(argc, argv, cmdline_options_), 
               var_map_);

    if (var_map_.count("help")) {
        std::cout << cmdline_options_ << std::endl;
        exit(0);
    }
  
    if (var_map_.count("version")) {
        string build_info;
        GetBuildInfo(build_info);
        std::cout <<  build_info << std::endl;
        exit(0);
    }

    // Process options off configuration file.
    GetOptValue<string>(config_file_, "conf-file");
    ifstream conf_file_in;
    conf_file_in.open(config_file_.c_str());
    if (conf_file_in.good()) {
        opt::store(opt::parse_config_file(conf_file_in, config_file_options_),
                   var_map_);
    }
    conf_file_in.close();
    opt::notify(var_map_);
}

AgentParam::~AgentParam()
{
}
