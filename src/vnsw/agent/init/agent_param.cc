/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

/*
 * Agent parameters are derived from 3 entities in increasing priority,
 * - System information
 * - Configuration file
 * - Parameters
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>
#include <iostream>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>

#include <base/logging.h>
#include <base/misc_utils.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>

#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <vgw/cfg_vgw.h>
#include <uve/agent_stats_collector.h>
#include <uve/flow_stats_collector.h>


using namespace std;
using namespace boost::property_tree;
using boost::optional;
namespace opt = boost::program_options;

template <typename ValueType>
bool AgentParam::GetOptValue
    (const boost::program_options::variables_map &var_map, ValueType &var, 
     const std::string &val) {
    // Check if the value is present.
    if (var_map.count(val)) {
        var = var_map[val].as<ValueType>();
        return true;
    }
    return false;
}

template <typename ValueType>
bool AgentParam::GetValueFromTree(ValueType &var, const std::string &val) {

    optional<ValueType> opt;

    if (opt = tree_.get_optional<ValueType>(val)) {
        var = opt.get();
        return true;
    } 
    return false;
}

bool AgentParam::GetIpAddress(const string &str, Ip4Address *addr) {
    boost::system::error_code ec;
    Ip4Address tmp = Ip4Address::from_string(str, ec);
    if (ec.value() != 0) {
        return false;
    }
    *addr = tmp;
    return true;
}

bool AgentParam::ParseIp(const string &key, Ip4Address *server) {
    optional<string> opt_str;
    if (opt_str = tree_.get_optional<string>(key)) {
        Ip4Address addr;
        if (GetIpAddress(opt_str.get(), &addr) == false) {
            LOG(ERROR, "Error in config file <" << config_file_ 
                    << ">. Error parsing IP address from <" 
                    << opt_str.get() << ">");
            return false;

        } else {
            *server = addr;
        }
    }
    return true;
}

bool AgentParam::ParseServerList(const string &key, Ip4Address *server1,
                                 Ip4Address *server2) {
    optional<string> opt_str;
    Ip4Address addr;
    vector<string> tokens;
    if (opt_str = tree_.get_optional<string>(key)) {
        boost::split(tokens, opt_str.get(), boost::is_any_of(" "));
        if (tokens.size() > 2) {
            LOG(ERROR, "Error in config file <" << config_file_ 
                    << ">. Cannot have more than 2 servers <" 
                    << opt_str.get() << ">");
            return false;
        }
        vector<string>::iterator it = tokens.begin();
        if (it != tokens.end()) {
            if (!GetIpAddress(*it, server1)) {
                return false;
            }
            ++it;
            if (it != tokens.end()) {
                if (!GetIpAddress(*it, server2)) {
                    return false;
                }
            }
        }
    }
    return true;
}

void AgentParam::ParseIpArgument
    (const boost::program_options::variables_map &var_map, Ip4Address &server,
     const string &key) {

    if (var_map.count(key)) {
        Ip4Address addr;
        if (GetIpAddress(var_map[key].as<string>(), &addr)) {
            server = addr;
        }
    }
}

bool AgentParam::ParseServerListArguments
    (const boost::program_options::variables_map &var_map, Ip4Address &server1,
     Ip4Address &server2, const string &key) {

    if (var_map.count(key)) {
        vector<string> value = var_map[key].as<vector<string> >();
        if (value.size() > 2) {
            LOG(ERROR, "Error in Arguments. Cannot have more than 2 servers "
                    "for " << key );
            return false;
        }
        vector<string>::iterator it = value.begin();
        Ip4Address addr;
        if (it !=  value.end()) {
            if (GetIpAddress(*it, &addr)) {
                server1 = addr;
            }
            ++it;
            if (it != value.end()) {
                if (GetIpAddress(*it, &addr)) {
                    server2 = addr;
                }
            }
        }
    }
    return true;
}

void AgentParam::ParseCollector() { 
    ParseIp("COLLECTOR.server", &collector_);
    GetValueFromTree<uint16_t>(collector_port_, "COLLECTOR.port");
}

void AgentParam::ParseVirtualHost() { 
    boost::system::error_code ec;
    optional<string> opt_str;

    GetValueFromTree<string>(vhost_.name_, "VIRTUAL-HOST-INTERFACE.name");

    if (opt_str = tree_.get_optional<string>("VIRTUAL-HOST-INTERFACE.ip")) {
        ec = Ip4PrefixParse(opt_str.get(), &vhost_.addr_, &vhost_.plen_);
        if (ec != 0 || vhost_.plen_ >= 32) {
            LOG(ERROR, "Error in config file <" << config_file_ 
                    << ">. Error parsing vhost ip-address from <" 
                    << opt_str.get() << ">");
            return;
        }
    }

    if (opt_str = tree_.get_optional<string>("VIRTUAL-HOST-INTERFACE.gateway")) {
        if (GetIpAddress(opt_str.get(), &vhost_.gw_) == false) {
            LOG(ERROR, "Error in config file <" << config_file_ 
                    << ">. Error parsing vhost gateway address from <" 
                    << opt_str.get() << ">");
        }
    }

    GetValueFromTree<string>(eth_port_, 
                             "VIRTUAL-HOST-INTERFACE.physical_interface");
}

void AgentParam::ParseDiscovery() {
    ParseIp("DISCOVERY.server", &dss_server_);
    GetValueFromTree<uint16_t>(xmpp_instance_count_, 
                               "DISCOVERY.max_control_nodes");
}

void AgentParam::ParseNetworks() {
    ParseIp("NETWORKS.control_network_ip", &mgmt_ip_);
}

void AgentParam::ParseHypervisor() {
    optional<string> opt_str;
    if (opt_str = tree_.get_optional<string>("HYPERVISOR.type")) {
        // Initialize mode to KVM. Will be overwritten for XEN later
        mode_ = AgentParam::MODE_KVM;

        if (opt_str.get() == "xen") {
            mode_ = AgentParam::MODE_XEN;
            GetValueFromTree<string>(xen_ll_.name_, 
                                     "HYPERVISOR.xen_ll_interface");

            boost::system::error_code ec;
            if (opt_str = tree_.get_optional<string>
                    ("HYPERVISOR.xen_ll_ip")) {
                ec = Ip4PrefixParse(opt_str.get(), &xen_ll_.addr_,
                                    &xen_ll_.plen_);
                if (ec != 0 || xen_ll_.plen_ >= 32) {
                    LOG(ERROR, "Error in config file <" << config_file_ 
                            << ">. Error parsing Xen Link-local ip-address from <" 
                            << opt_str.get() << ">");
                    return;
                }
            }
        } else if (opt_str.get() == "vmware") {
            mode_ = AgentParam::MODE_VMWARE;
            GetValueFromTree<string>(vmware_physical_port_, 
                                     "HYPERVISOR.vmware_physical_interface");
        } else {
            mode_ = AgentParam::MODE_KVM;
        }
    }
}

void AgentParam::ParseDefaultSection() { 
    optional<string> opt_str;
    optional<unsigned int> opt_uint;

    if (!GetValueFromTree<uint16_t>(http_server_port_, 
                                    "DEFAULT.http_server_port")) {
        http_server_port_ = ContrailPorts::HttpPortAgent;
    }

    GetValueFromTree<string>(tunnel_type_, "DEFAULT.tunnel_type");
    if ((tunnel_type_ != "MPLSoUDP") && (tunnel_type_ != "VXLAN"))
        tunnel_type_ = "MPLSoGRE";

    if (!GetValueFromTree<uint16_t>(flow_cache_timeout_, 
                                    "DEFAULT.flow_cache_timeout")) {
        flow_cache_timeout_ = Agent::kDefaultFlowCacheTimeout;
    }
    
    GetValueFromTree<string>(host_name_, "DEFAULT.hostname");

    if (!GetValueFromTree<string>(log_level_, "DEFAULT.log_level")) {
        log_level_ = "SYS_DEBUG";
    }
    if (!GetValueFromTree<string>(log_file_, "DEFAULT.log_file")) {
        log_file_ = Agent::DefaultLogFile();
    }
    GetValueFromTree<string>(log_category_, "DEFAULT.log_category");
    unsigned int log_local = 0, debug_logging = 0;
    if (opt_uint = tree_.get_optional<unsigned int>("DEFAULT.log_local")) {
        log_local = opt_uint.get();
    }
    if (log_local) {
        log_local_ = true;
    } else {
        log_local_ = false;
    }
    if (opt_uint = tree_.get_optional<unsigned int>("DEFAULT.log_local")) {
        debug_logging = opt_uint.get();
    }
    if (debug_logging) {
        debug_ = true;
    } else {
        debug_ = false;
    }
}

void AgentParam::ParseMetadataProxy() { 
    GetValueFromTree<string>(metadata_shared_secret_, 
                             "METADATA.metadata_proxy_secret");
}

void AgentParam::ParseLinklocal() {
    if (!GetValueFromTree<uint16_t>(linklocal_system_flows_, 
        "LINK-LOCAL.max_system_flows")) {
        linklocal_system_flows_ = Agent::kDefaultMaxLinkLocalOpenFds;
    }
    if (!GetValueFromTree<uint16_t>(linklocal_vm_flows_, 
        "LINK-LOCAL.max_vm_flows")) {
        linklocal_vm_flows_ = Agent::kDefaultMaxLinkLocalOpenFds;
    }
}

void AgentParam::ParseCollectorArguments
    (const boost::program_options::variables_map &var_map) {
    ParseIpArgument(var_map, collector_, "COLLECTOR.server");
    GetOptValue<uint16_t>(var_map, collector_port_, "COLLECTOR.port");
}

void AgentParam::ParseVirtualHostArguments
    (const boost::program_options::variables_map &var_map) {
    boost::system::error_code ec;

    GetOptValue<string>(var_map, vhost_.name_, "VIRTUAL-HOST-INTERFACE.name");
    string ip;
    if (GetOptValue<string>(var_map, ip, "VIRTUAL-HOST-INTERFACE.ip")) {
        ec = Ip4PrefixParse(ip, &vhost_.addr_, &vhost_.plen_);
        if (ec != 0 || vhost_.plen_ >= 32) {
            LOG(ERROR, "Error parsing vhost ip argument from <" << ip << ">");
            exit(EINVAL);
        }
    }
    ParseIpArgument(var_map, vhost_.gw_, "VIRTUAL-HOST-INTERFACE.gateway");
    GetOptValue<string>(var_map, eth_port_,
                        "VIRTUAL-HOST-INTERFACE.physical_interface");
}

void AgentParam::ParseDiscoveryArguments
    (const boost::program_options::variables_map &var_map) {
    ParseIpArgument(var_map, dss_server_, "DISCOVERY.server");
    GetOptValue<uint16_t>(var_map, xmpp_instance_count_, 
                          "DISCOVERY.max_control_nodes");
}

void AgentParam::ParseNetworksArguments
    (const boost::program_options::variables_map &var_map) {
    ParseIpArgument(var_map, mgmt_ip_, "NETWORKS.control_network_ip");
}

void AgentParam::ParseHypervisorArguments
    (const boost::program_options::variables_map &var_map) {
    boost::system::error_code ec;
    if (var_map.count("HYPERVISOR.type")) {
        if (var_map["HYPERVISOR.type"].as<string>() == "xen") {
            mode_ = AgentParam::MODE_XEN;
            GetOptValue<string>(var_map, xen_ll_.name_, 
                                "HYPERVISOR.xen_ll_interface");

            if (var_map.count("HYPERVISOR.xen_ll_ip")) {
                string ip = var_map["HYPERVISOR.xen_ll_ip"].as<string>();
                ec = Ip4PrefixParse(ip, &xen_ll_.addr_, &xen_ll_.plen_);
                if (ec != 0 || xen_ll_.plen_ >= 32) {
                    LOG(ERROR, "Error in argument <" << config_file_ 
                            << ">. Error parsing Xen Link-local ip-address from <" 
                            << ip << ">");
                    exit(EINVAL);
                }
            }
        } else if (var_map["HYPERVISOR.type"].as<string>() == "vmware") {
            mode_ = AgentParam::MODE_VMWARE;
            GetOptValue<string>(var_map, vmware_physical_port_, 
                                "HYPERVISOR.vmware_physical_interface");
        } else {
            mode_ = AgentParam::MODE_KVM;
        }
    }
}

void AgentParam::ParseDefaultSectionArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<uint16_t>(var_map, flow_cache_timeout_, 
                          "DEFAULT.flow_cache_timeout");
    GetOptValue<string>(var_map, host_name_, "DEFAULT.hostname");
    GetOptValue<uint16_t>(var_map, http_server_port_, 
                          "DEFAULT.http_server_port");
    GetOptValue<string>(var_map, log_category_, "DEFAULT.log-category");
    GetOptValue<string>(var_map, log_file_, "DEFAULT.log_file");
    GetOptValue<string>(var_map, log_level_, "DEFAULT.log_level");
    if (var_map.count("DEFAULT.log_local")) {
         log_local_ = true;
    }
    if (var_map.count("DEFAULT.debug")) {
        debug_ = true;
    }
}

void AgentParam::ParseMetadataProxyArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<string>(var_map, metadata_shared_secret_,
                        "METADATA.metadata_proxy_secret");
}

void AgentParam::ParseLinklocalArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<uint16_t>(var_map, linklocal_system_flows_, 
                          "LINK-LOCAL.max_system_flows");
    GetOptValue<uint16_t>(var_map, linklocal_vm_flows_, 
                          "LINK-LOCAL.max_vm_flows");
}

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
void AgentParam::InitFromConfig() {
    // Read and parse INI
    try {
        read_ini(config_file_, tree_);
    } catch (exception &e) {
        LOG(ERROR, "Error reading config file <" << config_file_ 
            << ">. INI format error??? <" << e.what() << ">");
        AGENT_CONFIG_PARSE_LOG("Error reading config file ", config_file_, 
                               " INI format error? ", e.what());
        return;
    } 

    ParseCollector();
    ParseVirtualHost();
    ParseServerList("CONTROL-NODE.server", &xmpp_server_1_, &xmpp_server_2_);
    ParseServerList("DNS.server", &dns_server_1_, &dns_server_2_);
    ParseDiscovery();
    ParseNetworks();
    ParseHypervisor();
    ParseDefaultSection();
    ParseMetadataProxy();
    ParseLinklocal();
    LOG(DEBUG, "Config file <" << config_file_ << "> read successfully.");
    return;
}

void AgentParam::InitFromArguments
    (const boost::program_options::variables_map &var_map) {
    ParseCollectorArguments(var_map);
    ParseVirtualHostArguments(var_map);
    ParseServerListArguments(var_map, xmpp_server_1_, xmpp_server_2_, 
                             "CONTROL-NODE.server");
    ParseServerListArguments(var_map, dns_server_1_, dns_server_2_, 
                             "DNS.server");
    ParseDiscoveryArguments(var_map);
    ParseNetworksArguments(var_map);
    ParseHypervisorArguments(var_map);
    ParseDefaultSectionArguments(var_map);
    ParseMetadataProxyArguments(var_map);
    ParseLinklocalArguments(var_map);
    return;
}

// Update linklocal max flows if they are greater than the max allowed for the
// process. Also, ensure that the process is allowed to open upto
// linklocal_system_flows + kMaxOtherOpenFds files
void AgentParam::ComputeLinkLocalFlowLimits() {
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

static bool ValidateInterface(bool test_mode, const std::string &ifname) {
    if (test_mode) {
        return true;
    }
    int fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(fd >= 0);

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname.c_str(), IF_NAMESIZE);
    int err = ioctl(fd, SIOCGIFHWADDR, (void *)&ifr);
    close (fd);

    if (err < 0) {
        LOG(ERROR, "Error reading interface <" << ifname << ">. Error number "
            << errno << " : " << strerror(errno));
        return false;
    }

    return true;
}

void AgentParam::Validate() {
    // Validate vhost interface name
    if (vhost_.name_ == "") {
        LOG(ERROR, "Configuration error. vhost interface name not specified");
        exit(EINVAL);
    }

    // Check if interface is already present
    if (ValidateInterface(test_mode_, vhost_.name_) == false) {
        exit(ENODEV);
    }

    // Validate ethernet port
    if (eth_port_ == "") {
        LOG(ERROR, "Configuration error. eth_port not specified");
        exit(EINVAL);
    }

    // Check if interface is already present
    if (ValidateInterface(test_mode_, eth_port_) == false) {
        exit(ENODEV);
    }

    // Validate physical port used in vmware
    if (mode_ == MODE_VMWARE) {
        if (vmware_physical_port_ == "") {
            LOG(ERROR, "Configuration error. Physical port connecting to "
                "virtual-machines not specified");
            exit(EINVAL);
        }

        if (ValidateInterface(test_mode_, vmware_physical_port_) == false) {
            exit(ENODEV);
        }
    }

}

void AgentParam::InitVhostAndXenLLPrefix() {
    // Set the prefix address for VHOST and XENLL interfaces
    uint32_t mask = vhost_.plen_ ? (0xFFFFFFFF << (32 - vhost_.plen_)) : 0;
    vhost_.prefix_ = Ip4Address(vhost_.addr_.to_ulong() & mask);

    mask = xen_ll_.plen_ ? (0xFFFFFFFF << (32 - xen_ll_.plen_)) : 0;
    xen_ll_.prefix_ = Ip4Address(xen_ll_.addr_.to_ulong() & mask);
}

void AgentParam::Init(const string &config_file, const string &program_name,
                      const boost::program_options::variables_map &var_map) {
    config_file_ = config_file;
    program_name_ = program_name;

    InitFromSystem();
    InitFromConfig();
    InitFromArguments(var_map);
    InitVhostAndXenLLPrefix();

    vgw_config_table_->Init(tree_);
    ComputeLinkLocalFlowLimits();
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

void AgentParam::set_test_mode(bool mode) {
    test_mode_ = mode;
}

AgentParam::AgentParam(Agent *agent) :
        vhost_(), eth_port_(), xmpp_instance_count_(), xmpp_server_1_(),
        xmpp_server_2_(), dns_server_1_(), dns_server_2_(), dss_server_(),
        mgmt_ip_(), mode_(MODE_KVM), xen_ll_(), tunnel_type_(),
        metadata_shared_secret_(), linklocal_system_flows_(),
        linklocal_vm_flows_(), flow_cache_timeout_(), config_file_(), program_name_(),
        log_file_(), log_local_(false), log_level_(), log_category_(),
        collector_(), collector_port_(), http_server_port_(), host_name_(),
        agent_stats_interval_(AgentStatsCollector::AgentStatsInterval), 
        flow_stats_interval_(FlowStatsCollector::FlowStatsInterval),
        vmware_physical_port_(""), test_mode_(false), debug_(false), tree_() {
    vgw_config_table_ = std::auto_ptr<VirtualGatewayConfigTable>
        (new VirtualGatewayConfigTable(agent));
}
AgentParam::~AgentParam()
{
}
