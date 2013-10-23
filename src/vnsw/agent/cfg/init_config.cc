/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sys/stat.h>

#include <boost/property_tree/xml_parser.hpp>
#include <boost/uuid/string_generator.hpp>

#include <base/logging.h>
#include <cmn/agent_cmn.h>
#include <db/db.h>
#include <db/db_graph.h>
#include <oper/vn.h>
#include <oper/sg.h>
#include <oper/vm.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/interface.h>
#include <oper/mirror_table.h>
#include <oper/agent_route.h>

#include <cfg/init_config.h>
#include <cfg/interface_cfg.h>
#include <cfg/interface_cfg_listener.h>
#include <cfg/cfg_filter.h>
#include "vnc_cfg_types.h" 
#include "bgp_schema_types.h" 
#include <pugixml/pugixml.hpp>
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "discovery_agent.h"

#include "vgw/vgw_cfg.h"
#include "vgw/vgw.h"

using namespace std;
using namespace autogen;
using namespace boost::property_tree;
using namespace boost::uuids;
using boost::optional;

void IFMapAgentSandeshInit(DB *db);

AgentConfig *AgentConfig::singleton_;

CfgListener         *CfgModule::cfg_listener_;
IFMapAgentParser    *CfgModule::cfg_parser_; 
DBGraph             *CfgModule::cfg_graph_;

static string FileRead(const char *init_file) {
    ifstream ifs(init_file);
    string content ((istreambuf_iterator<char>(ifs) ),
                    (istreambuf_iterator<char>() ));
    return content;
}

static void CreateVhostBcastRoute(const string &vrf_name) {
    boost::system::error_code ec;
    Ip4Address bcast_ip = Ip4Address::from_string("255.255.255.255", ec);
    Inet4MulticastAgentRouteTable *mc_rt_table = 
        static_cast<Inet4MulticastAgentRouteTable *>
                (Agent::GetInstance()->GetVrfTable()->GetRouteTable(vrf_name, 
                                   AgentRouteTableAPIS::INET4_MULTICAST));
    mc_rt_table->AddVHostRecvRoute(vrf_name,
                                   Agent::GetInstance()->GetVirtualHostInterfaceName(),
                                   bcast_ip, false);
}

AgentConfig::AgentConfig(const std::string &vhost_name,
                         const std::string &vhost_addr,
                         const Ip4Address &vhost_prefix,
                         const int vhost_plen,
                         const std::string &vhost_gw,
                         const std::string &eth_port,
                         const std::string &xmpp_server_1,
                         const std::string &xmpp_server_2,
                         const std::string &dns_server_1,
                         const std::string &dns_server_2,
                         const std::string &tunnel_type,
                         const std::string &dss_server,
                         const int dss_xs_instances,
                         const AgentCmdLineParams cmd_line) 
    : vhost_name_(vhost_name), vhost_addr_(vhost_addr),
      vhost_prefix_(vhost_prefix), vhost_plen_(vhost_plen), vhost_gw_(vhost_gw),
      eth_port_(eth_port), xmpp_server_1_(xmpp_server_1),
      xmpp_server_2_(xmpp_server_2), dns_server_1_(dns_server_1),
      dns_server_2_(dns_server_2), 
      dss_server_(dss_server), dss_xs_instances_(dss_xs_instances),
      trigger_(NULL), mode_(MODE_KVM), xen_ll_(),
      tunnel_type_(tunnel_type), cmd_params_(cmd_line) {
    assert(singleton_ == NULL);
    singleton_ = this;
}

AgentConfig::~AgentConfig() { 
    if (trigger_) {
        trigger_->Reset();
        delete trigger_;
    }
};

bool AgentConfig::IsVHostConfigured() {
    if (singleton_ && singleton_->vhost_addr_ != "") {
        return true;
    }
    return false;
}

static void ParsePortInfo(const ptree &node, string &name, string &addr, string &gw) {
    optional<string> opt_str;

    opt_str = node.get_optional<string>("name");
    if (opt_str) {
        name = opt_str.get();
    } 

    opt_str = node.get_optional<string>("ip-address");
    if (opt_str) {
        addr = opt_str.get();
    }

    opt_str = node.get_optional<string>("gateway");
    if (opt_str) {
        gw = opt_str.get();
    }
}

static void ParseVHost(const ptree &node, const string &fname, string &name,
                       string &addr, string &gw) {
    try {
        ParsePortInfo(node, name, addr, gw);
    } catch (exception &e) {
        LOG(ERROR, "Error reading \"vhost\" node in config file <" 
            << fname << ">. Error <" << e.what() << ">");
        exit(EINVAL);
    }
}

static void ParseEthPort(const ptree &node, const string &fname, string &port) {
    optional<string> opt_str;
    try {
        opt_str = node.get_optional<string>("name");
    } catch (exception &e) {
        LOG(ERROR, "Error reading \"eth-port\" node in config file <"
            << fname << ">. Error <" << e.what() << ">");
        exit(EINVAL);
    }
    if (opt_str) {
        port = opt_str.get();
    }
}

static void ParseXmppServer(const ptree &node, const string &fname,
                            string &server1, string &server2) {
    optional<string> opt_str;

    try {
        opt_str = node.get<string>("ip-address");
    } catch (exception &e) {
        LOG(ERROR, "Error reading \"xmpp-server\" node in config "
            "file <" << fname << ">. Error <" << e.what() << ">");
        exit(EINVAL);
    }

    if (server1 == "") {
        server1 = opt_str.get();
    } else if (server2 == "") {
        server2 = opt_str.get();
    }
}

static void ParseDnsServer(const ptree &node, const string &fname,
                           string &server1, string &server2) {
    optional<string> opt_str;

    try {
        opt_str = node.get<string>("ip-address");
    } catch (exception &e) {
        LOG(ERROR, "Error reading \"dns-server\" node in config "
            "file <" << fname << ">. Error <" << e.what() << ">");
        exit(EINVAL);
    }

    if (server1 == "") {
        server1 = opt_str.get();
    } else if (server2 == "") {
        server2 = opt_str.get();
    }
}

static void ParseDiscoveryServer(const ptree &node, const string &fname,
                                 string &server, int &xs_instances) {
    optional<string> opt_str;

    try {
        opt_str = node.get<string>("ip-address");
        if (opt_str) {
            server = opt_str.get();
        }

        opt_str = node.get<string>("control-instances");
        if (opt_str) {
            stringstream str(opt_str.get());
            str >> xs_instances;
        }

    } catch (exception &e) {
        LOG(ERROR, "Error reading \"discovery-server\" node in config "
            "file <" << fname << ">. Error <" << e.what() << ">");
        exit(EINVAL);
    }
}

static void ParseControl(const ptree &node, const string &fname,
                         string &mgmt_ip) {
    optional<string> opt_str;

    try {
        opt_str = node.get<string>("ip-address");
        if (opt_str) {
            mgmt_ip = opt_str.get();
        }
    } catch (exception &e) {
        LOG(ERROR, "Error reading \"control stanza\" in config "
            "file <" << fname << ">. Error <" << e.what() << ">");
        exit(EINVAL);
    }
}

static void ParseHypervisor(ptree::const_iterator &iter, const string &fname, 
                            string &mode, string &ll_name, string &ll_addr) {
    optional<string> opt_str;
    ptree node = iter->second;

    try {
        opt_str = iter->second.get<string>("<xmlattr>.mode");
        if (opt_str) {
            mode = opt_str.get();
        } 

        if (mode == "xen") {
            opt_str = node.get<string>("xen-ll-port");
            if (opt_str) {
                ll_name = opt_str.get();
            }

            opt_str = node.get<string>("xen-ll-ip-address");
            if (opt_str) {
                ll_addr = opt_str.get();
            }
        }

    } catch (exception &e) {
        LOG(ERROR, "Error reading \"hypervisor\" node in config file <"
            << fname << ">. Error <" << e.what() << ">");
        exit(EINVAL);
    }

}

static bool interface_exist(string &name)
{
	struct if_nameindex *ifs = NULL;
	struct if_nameindex *head = NULL;
	bool ret = false;
	string tname = "";

	ifs = if_nameindex();
	if (ifs == NULL) {
		LOG(INFO, "No interface exists!");
		return ret;
	}
	head = ifs;
	while (ifs->if_name && ifs->if_index) {
		tname = ifs->if_name;
		if (string::npos != tname.find(name)) {
			ret = true;
			name = tname;
			break;
		}
		ifs++;
	}
	if_freenameindex(head);
	return ret;
}

void AgentConfig::LogConfig() const {
    LOG(DEBUG, "vhost interface name        : " << vhost_name_);
    LOG(DEBUG, "vhost IP Address            : " << vhost_addr_ << "/" 
        << vhost_plen_);
    LOG(DEBUG, "Ethernet port               : " << eth_port_);
    LOG(DEBUG, "XMPP Server-1               : " << xmpp_server_1_);
    LOG(DEBUG, "XMPP Server-2               : " << xmpp_server_2_);
    LOG(DEBUG, "DNS Server-1                : " << dns_server_1_);
    LOG(DEBUG, "DNS Server-2                : " << dns_server_2_);
    LOG(DEBUG, "Discovery Server            : " << dss_server_);
    LOG(DEBUG, "Controller Instances        : " << dss_xs_instances_);
    LOG(DEBUG, "Tunnel-Type                 : " << tunnel_type_);
    if (mode_ != MODE_XEN) {
    LOG(DEBUG, "Hypervisor mode             : kvm");
        return;
    }
    LOG(DEBUG, "Hypervisor mode             : xen");
    LOG(DEBUG, "XEN Link Local port         : " << xen_ll_.name_);
    LOG(DEBUG, "XEN Link Local IP Address   : " << xen_ll_.addr_.to_string()
        << "/" << xen_ll_.plen_);
}

void AgentConfig::SetXenInfo(const string &ifname, Ip4Address addr,
                             int plen) {
    if (ifname != "") {
        xen_ll_.name_ = ifname;
    }

    if (addr.to_ulong() != 0) {
        xen_ll_.addr_ = addr;
    }

    if (plen >= 0) {
        xen_ll_.plen_ = plen;
    }

    uint32_t prefix = xen_ll_.addr_.to_ulong();
    prefix = (prefix >> xen_ll_.plen_) << xen_ll_.plen_;
    xen_ll_.prefix_ = Ip4Address(prefix);
}

void AgentConfig::InitConfig(const char *init_file, AgentCmdLineParams cmd_line) {
    string vhost_name = "";
    string vhost_gw = "";
    string eth_port = "";
    string addr = "";
    string xs_addr_1 = "";
    string xs_addr_2 = "";
    string dns_addr_1 = "";
    string dns_addr_2 = "";
    string dss_addr = "";
    int xs_instances = -1;
    string mgmt_ip = "";
    string vhost_addr = "";
    string tunnel_str = "";
    optional<string> opt_str;
    Ip4Address vhost_prefix;
    int vhost_plen = -1;
    string mode_str = "";
    Mode mode = MODE_KVM;
    string xen_ll_name = "";
    string xen_ll_addr_str = "";
    boost::system::error_code ec;

    if (init_file == NULL) {
        LOG(ERROR, "No init file specified. Continuing initialization.");
        return;
    }

    struct stat s;
    if (stat(init_file, &s) != 0) {
        LOG(ERROR, "Error opening config file <" << init_file 
            << ">. Error number <" << errno << ">");
        exit(EINVAL);
    }

    string str = FileRead(init_file);
    istringstream sstream(str);
    ptree tree;

    try {
        read_xml(sstream, tree, xml_parser::trim_whitespace);
    } catch (exception &e) {
        LOG(ERROR, "Error reading config file <" << init_file 
            << ">. XML format error??? <" << e.what() << ">");
        exit(EINVAL);
    } 

    ptree config;
    try {
        config = tree.get_child("config");
    } catch (exception &e) {
        LOG(ERROR, "Error reading \"config\" node in config file <" 
            << init_file << ">. Error <" << e.what() << ">");
        exit(EINVAL);
    }

    ptree agent;
    try {
        agent = config.get_child("agent");
    } catch (exception &e) {
        LOG(ERROR, init_file << " : Error reading \"agent\" node in config "
            "file. Error <" << e.what() << ">");
        exit(EINVAL);
    }

    for (ptree::const_iterator iter = agent.begin(); iter != agent.end();
         ++iter) {
        if (iter->first == "vhost") {
            ParseVHost(iter->second, init_file, vhost_name, addr, vhost_gw);
        } else if (iter->first == "eth-port") {
            ParseEthPort(iter->second, init_file, eth_port);
        } else if (iter->first == "xmpp-server") {
            ParseXmppServer(iter->second, init_file, xs_addr_1, xs_addr_2);
        } else if (iter->first == "dns-server") {
            ParseDnsServer(iter->second, init_file, dns_addr_1, dns_addr_2);
        } else if (iter->first == "discovery-server") {
            ParseDiscoveryServer(iter->second, init_file, dss_addr, xs_instances);
        } else if (iter->first == "control") {
            ParseControl(iter->second, init_file, mgmt_ip);
        } else if (iter->first == "hypervisor") {
            ParseHypervisor(iter, init_file, mode_str, xen_ll_name,
                            xen_ll_addr_str);
        } else if (iter->first == "gateway") {
            continue;
        } else if (iter->first == "<xmlcomment>") {
            continue;
        } else {
            LOG(ERROR, "Error in config file <" << init_file 
                << ">. Unknown XML node <" << iter->first << ">. Ignoring");
        }
    }
    
    Agent::GetInstance()->SetMgmtIp(mgmt_ip);
    opt_str = agent.get_optional<string>("tunnel-type");
    if (opt_str) {
        tunnel_str = opt_str.get();
    }

    // Validate vhost_name
    if (vhost_name == "") {
        LOG(ERROR, "Error in config file <" << init_file 
            << ">. vhost interface name not specified or has invalid value");
        exit(EINVAL);
    }

    // IP Address stanza is optional
    if (addr != "") {
        vhost_addr =addr.substr(0, addr.find('/'));
        Ip4Address::from_string(vhost_addr, ec);
        if (ec.value() != 0) {
            LOG(ERROR, "Error in config file <" << init_file 
                << ">. Error parsing vhost interface IP address from <" 
                << addr << ">");
            exit(EINVAL);
        }

        ec = Ip4PrefixParse(addr, &vhost_prefix, &vhost_plen);
        if (ec != 0 || vhost_plen >= 32) {
            LOG(ERROR, "Error in config file <" << init_file 
                << ">. Error parsing vhost prefix length from <" 
                << addr << ">");
            exit(EINVAL);
        }
    }

    // Validate gateway
    if (vhost_gw == "") {
        LOG(ERROR, "Error in config file <" << init_file 
            << ">. vhost gateway not specified or has invalid value");
        //exit(EINVAL);
    } else {
        Ip4Address::from_string(vhost_gw, ec);
        if (ec.value() != 0) {
            LOG(ERROR, "Error in config file <" << init_file 
                << ">. Error parsing vhost gateway IP address from <" 
                << vhost_gw << ">");
            exit(EINVAL);
        }
    }

    // Validate ethernet port
    if (eth_port == "") {
        LOG(ERROR, "Error in config file <" << init_file 
            << ">. eth-port not specified or has invalid value");
        exit(EINVAL);
    }

    // Validate XMPP Server-1
    if (xs_addr_1 != "") {
        Ip4Address::from_string(xs_addr_1, ec);
        if (ec.value() != 0) {
            LOG(ERROR, "Error in config file <" << init_file 
                << ">. Error parsing XMPP address <" << xs_addr_1 << ">");
            exit(EINVAL);
        }
    }

    // Validate XMPP Server-2
    if (xs_addr_2 != "") {
        Ip4Address::from_string(xs_addr_2, ec);
        if (ec.value() != 0) {
            LOG(ERROR, "Error in config file <" << init_file 
                << ">. Error parsing XMPP address <" << xs_addr_2 << ">");
            exit(EINVAL);
        }
    }

    // Validate DNS Server-1
    if (dns_addr_1 != "") {
        Ip4Address::from_string(dns_addr_1, ec);
        if (ec.value() != 0) {
            LOG(ERROR, "Error in config file <" << init_file 
                << ">. Error parsing DNS address <" << dns_addr_1 << ">");
            exit(EINVAL);
        }
    }

    // Validate DNS Server-2
    if (dns_addr_2 != "") {
        Ip4Address::from_string(dns_addr_2, ec);
        if (ec.value() != 0) {
            LOG(ERROR, "Error in config file <" << init_file 
                << ">. Error parsing DNS address <" << dns_addr_2 << ">");
            exit(EINVAL);
        }
    }

    if ((tunnel_str != "MPLSoUDP") && (tunnel_str != "VXLAN"))
        tunnel_str = "MPLSoGRE";

    // Validate Discovery Server
    if (dss_addr != "") {
        Ip4Address::from_string(dss_addr, ec);
        if (ec.value() != 0) {
            LOG(ERROR, "Error in config file <" << init_file 
                << ">. Error parsing Discovery address <" << dss_addr << ">");
            exit(EINVAL);
        }
    }

    LOG(DEBUG, "Config file <" << init_file << "> read successfully.");
    singleton_ = new AgentConfig(vhost_name, vhost_addr, vhost_prefix, vhost_plen, 
                                 vhost_gw, eth_port, xs_addr_1, xs_addr_2,
                                 dns_addr_1, dns_addr_2, tunnel_str,
                                 dss_addr, xs_instances, cmd_line);

    Agent::GetInstance()->SetVirtualHostInterfaceName(vhost_name);
    Agent::GetInstance()->SetIpFabricItfName(eth_port);

    // If "/proc/xen" exists it means we are running in Xen dom0
    struct stat fstat;
    if (stat("/proc/xen", &fstat) == 0) {
        mode = MODE_XEN;
        LOG(INFO, "Found file /proc/xen. Initializing mode to XEN");
    }
    singleton_->SetXenInfo("", Ip4Address::from_string("169.254.0.1"), 16);

    if (mode_str == "kvm") {
        mode = MODE_KVM;
    } else if (mode_str == "xen") {
        mode = MODE_XEN;
    } else if (mode_str != "") {
        LOG(ERROR, "Unknown hypervisor mode " << mode_str << 
            " in config file <" << init_file << ">");
        exit(EINVAL);
    }
    VGwConfig::Init(init_file);
    AgentConfig::GetInstance()->SetMode(mode);

    if (mode == MODE_KVM) {
        return;
    }

    Ip4Address xen_ll_addr = Ip4Address::from_string("0.0.0.0");
    int xen_ll_plen = -1;
    if (xen_ll_addr_str != "") {
        string str = xen_ll_addr_str.substr(0, xen_ll_addr_str.find('/'));
        xen_ll_addr = Ip4Address::from_string(str, ec);
        if (ec.value() != 0) {
            LOG(ERROR, "Error in config file <" << init_file 
                << ">. Error parsing xen-ll-ip-address from <" 
                << str << ">");
            exit(EINVAL);
        }

        Ip4Address xen_prefix;
        ec = Ip4PrefixParse(xen_ll_addr_str, &xen_prefix, &xen_ll_plen);
        if (ec != 0 || xen_ll_plen >= 32) {
            LOG(ERROR, "Error in config file <" << init_file 
                << ">. Error parsing xen-ll-ip-address prefix length from <" 
                << xen_ll_addr_str << ">");
            exit(EINVAL);
        }
    }
    singleton_->SetXenInfo(xen_ll_name, xen_ll_addr, xen_ll_plen);

    return;
}

void AgentConfig::OnItfCreate(DBEntryBase *entry, AgentConfig::Callback cb) {

    if (entry->IsDeleted())
        return;

    Interface *itf = static_cast<Interface *>(entry);
    Interface::Type type = itf->GetType();
    if (type != Interface::ETH)
        return;

    CreateVhostBcastRoute(Agent::GetInstance()->GetDefaultVrf());

    //Create Receive and resolve route
    if (GetVHostAddr() != "") {
        Inet4UnicastAgentRouteTable *rt_table;

        rt_table = static_cast<Inet4UnicastAgentRouteTable *>
            (Agent::GetInstance()->GetVrfTable()->GetRouteTable(
                                   Agent::GetInstance()->GetDefaultVrf(),
                                   AgentRouteTableAPIS::INET4_UNICAST));
        boost::system::error_code ec;
        Ip4Address ip =Ip4Address::from_string(GetVHostAddr(), ec);
        assert(ec.value() == 0);
        Agent::GetInstance()->SetRouterId(ip);
        rt_table->AddVHostRecvRoute(
            Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetVirtualHostInterfaceName(),
            ip, false);
        rt_table->AddVHostSubnetRecvRoute(
            Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetVirtualHostInterfaceName(),
            ip, GetVHostPlen(), false);
        rt_table->AddResolveRoute(Agent::GetInstance()->GetDefaultVrf(), GetVHostPrefix(), 
                                  GetVHostPlen());
        Agent::GetInstance()->SetPrefixLen(GetVHostPlen());
    }

    boost::system::error_code ec;
    Ip4Address gw_ip;
    if (GetVHostGateway() != "") {
        gw_ip = Ip4Address::from_string(GetVHostGateway(), ec);
        assert(ec.value() == 0);
    } else {
        gw_ip = Ip4Address::from_string("0.0.0.0", ec);
        assert(ec.value() == 0);
    }

    Ip4Address default_dest_ip = Ip4Address::from_string("0.0.0.0", ec);
    Agent::GetInstance()->SetGatewayId(gw_ip);
    Inet4UnicastAgentRouteTable::AddGatewayRoute(Agent::GetInstance()->GetLocalPeer(),
                                     Agent::GetInstance()->GetDefaultVrf(),
                                     default_dest_ip, 0, gw_ip);

    if (cb)
        cb();
    trigger_ = SafeDBUnregister(Agent::GetInstance()->GetInterfaceTable(), lid_);
}

SandeshTraceBufferPtr CfgTraceBuf(SandeshTraceBufferCreate("Config", 100));

void AgentConfig::InitXenLinkLocalIntf() {
    if (!singleton_->isXenMode() || singleton_->xen_ll_.name_ == "")
        return;
    if(!interface_exist(singleton_->xen_ll_.name_)) {
	LOG(INFO, "Interface " << singleton_->xen_ll_.name_ << " not found");
	return;
    }
    VirtualHostInterface::CreateReq(singleton_->xen_ll_.name_, 
                                    Agent::GetInstance()->GetLinkLocalVrfName(), 
                                    VirtualHostInterface::LINK_LOCAL);

    Inet4UnicastAgentRouteTable *rt_table;
    rt_table = static_cast<Inet4UnicastAgentRouteTable *>
        (Agent::GetInstance()->GetVrfTable()->GetRouteTable(
         Agent::GetInstance()->GetLinkLocalVrfName(), AgentRouteTableAPIS::INET4_UNICAST));
    rt_table->AddVHostRecvRoute(rt_table->GetVrfName(),
                                singleton_->xen_ll_.name_, 
                                singleton_->xen_ll_.addr_, false);

    rt_table->AddVHostSubnetRecvRoute(rt_table->GetVrfName(),
                                      singleton_->xen_ll_.name_,
                                      singleton_->xen_ll_.addr_,
                                      singleton_->xen_ll_.plen_, false);

    rt_table->AddResolveRoute(rt_table->GetVrfName(),
                              singleton_->xen_ll_.prefix_,
                              singleton_->xen_ll_.plen_);
}

void AgentConfig::Init(DB *db, const char *init_file, Callback cb) {
    int count = 0;
    int dns_count = 0;

    if (singleton_->GetXmppServer_1() != "") {
        Agent::GetInstance()->SetAgentMcastLabelRange(count);
        Agent::GetInstance()->SetXmppServer(singleton_->GetXmppServer_1(), count++);
    }

    if (singleton_->GetXmppServer_2() != "") {
        Agent::GetInstance()->SetAgentMcastLabelRange(count);
        Agent::GetInstance()->SetXmppServer(singleton_->GetXmppServer_2(), count++);
    }

    if (singleton_->GetDnsServer_1() != "") {
        Agent::GetInstance()->SetDnsXmppServer(singleton_->GetDnsServer_1(), dns_count++);
    }

    if (singleton_->GetDnsServer_2() != "") {
        Agent::GetInstance()->SetDnsXmppServer(singleton_->GetDnsServer_2(), dns_count++);
    }

    if (singleton_->GetDiscoveryServer() != "") {
        Agent::GetInstance()->SetDiscoveryServer(singleton_->GetDiscoveryServer());
        Agent::GetInstance()->SetDiscoveryXmppServerInstances(singleton_->GetDiscoveryXmppServerInstances());
    }

    singleton_->lid_ = Agent::GetInstance()->GetInterfaceTable()->Register
        (boost::bind(&AgentConfig::OnItfCreate, singleton_, _2, cb));

    VirtualHostInterface::CreateReq(singleton_->GetVHostName(), 
                                    Agent::GetInstance()->GetDefaultVrf(), 
                                    VirtualHostInterface::HOST);
    InitXenLinkLocalIntf();
    EthInterface::CreateReq(singleton_->GetEthPort(), Agent::GetInstance()->GetDefaultVrf());
    if (singleton_->tunnel_type_ == "MPLSoUDP")
        TunnelType::SetDefaultType(TunnelType::MPLS_UDP);
    else if (singleton_->tunnel_type_ == "VXLAN")
        TunnelType::SetDefaultType(TunnelType::VXLAN);
    else
        TunnelType::SetDefaultType(TunnelType::MPLS_GRE);

    DiscoveryAgentClient::Init();
    VGwTable::Init();
}


static void DeleteRoutes() {
   boost::system::error_code ec;
   Ip4Address bcast_ip = Ip4Address::from_string("255.255.255.255", ec);
   Ip4Address ip = Ip4Address::from_string("0.0.0.0", ec);
   Inet4UnicastAgentRouteTable::DeleteReq(Agent::GetInstance()->GetLocalPeer(), 
                                          Agent::GetInstance()->GetDefaultVrf(),
                                          bcast_ip, 32);
   Inet4MulticastAgentRouteTable::DeleteMulticastRoute(Agent::GetInstance()->GetDefaultVrf(), 
                                                       ip, bcast_ip);
   Inet4UnicastAgentRouteTable::DeleteReq(Agent::GetInstance()->GetLocalPeer(), 
                                          Agent::GetInstance()->GetDefaultVrf(),
                                          ip, 0);

   Inet4UnicastAgentRouteTable::DeleteReq(Agent::GetInstance()->GetLocalPeer(), 
                                          Agent::GetInstance()->GetDefaultVrf(),
                                          Agent::GetInstance()->GetRouterId(), 32);
   Inet4UnicastAgentRouteTable::DeleteReq(Agent::GetInstance()->GetLocalPeer(), 
                                          Agent::GetInstance()->GetDefaultVrf(),
                                          Agent::GetInstance()->GetGatewayId(), 32);
   Inet4UnicastAgentRouteTable::DeleteReq(Agent::GetInstance()->GetLocalPeer(), 
                                          Agent::GetInstance()->GetDefaultVrf(),
                                          Agent::GetInstance()->GetRouterId(), 
                                          Agent::GetInstance()->GetPrefixLen());
   Inet4UnicastAgentRouteTable::DeleteReq(Agent::GetInstance()->GetLocalPeer(), 
            Agent::GetInstance()->GetDefaultVrf(),
            Ip4Address(Agent::GetInstance()->GetRouterId().to_ulong() | 
            ~(0xFFFFFFFF << (32 - Agent::GetInstance()->GetPrefixLen()))), 32);
}


static void DeleteNextHop() {
    NextHopKey *key = new DiscardNHKey();
    NextHopTable::Delete(key);

    key = new ResolveNHKey();
    NextHopTable::Delete(key);

    key = new ReceiveNHKey(new VirtualHostInterfaceKey(nil_uuid(),
                           Agent::GetInstance()->GetVirtualHostInterfaceName()), false);
    NextHopTable::Delete(key);

    key = new ReceiveNHKey(new VirtualHostInterfaceKey(nil_uuid(),
                           Agent::GetInstance()->GetVirtualHostInterfaceName()), true);
    NextHopTable::Delete(key);

    key = new InterfaceNHKey(new HostInterfaceKey(nil_uuid(),
                             Agent::GetInstance()->GetHostInterfaceName()), false,
                             InterfaceNHFlags::INET4);
    NextHopTable::Delete(key);
}

static void DeleteVrf() {
    Agent::GetInstance()->GetVrfTable()->DeleteVrf(Agent::GetInstance()->GetDefaultVrf());
}

static void DeleteInterface() {
    HostInterface::DeleteReq(Agent::GetInstance()->GetHostInterfaceName());
    VirtualHostInterface::DeleteReq(Agent::GetInstance()->GetVirtualHostInterfaceName());
    EthInterface::DeleteReq(Agent::GetInstance()->GetIpFabricItfName());
}

void AgentConfig::DeleteStaticEntries() {
    DeleteRoutes();
    DeleteNextHop();
    DeleteVrf();
    DeleteInterface();
}

void AgentConfig::Shutdown() {

    DiscoveryAgentClient::Shutdown();
    assert(AgentConfig::singleton_);
    delete AgentConfig::singleton_;
    AgentConfig::singleton_ = NULL;
}

void CfgModule::CreateDBTables(DB *db) {
    CfgIntTable      *table;

    DB::RegisterFactory("db.cfg_int.0", &CfgIntTable::CreateTable);
    table = static_cast<CfgIntTable *>(db->CreateTable("db.cfg_int.0"));
    assert(table);
    Agent::GetInstance()->SetIntfCfgTable(table);

    cfg_parser_ = new IFMapAgentParser(db);
    cfg_graph_ = new DBGraph;
    vnc_cfg_Agent_ModuleInit(db, cfg_graph_);
    vnc_cfg_Agent_ParserInit(db, cfg_parser_);
    bgp_schema_Agent_ModuleInit(db, cfg_graph_);
    bgp_schema_Agent_ParserInit(db, cfg_parser_);
    IFMapAgentLinkTable_Init(db, cfg_graph_);
    cfg_listener_ = new CfgListener;
    Agent::GetInstance()->SetCfgListener(cfg_listener_);
    Agent::GetInstance()->SetIfMapAgentParser(cfg_parser_);
    IFMapAgentStaleCleaner *cl = new IFMapAgentStaleCleaner(db, cfg_graph_, 
                                        *(Agent::GetInstance()->GetEventManager()->io_service()));
    Agent::GetInstance()->SetAgentStaleCleaner(cl);
    IFMapAgentSandeshInit(db);
}

void CfgModule::RegisterDBClients(DB *db) {
    cfg_listener_->Register("virtual-network", Agent::GetInstance()->GetVnTable(),
                            VirtualNetwork::ID_PERMS);
    cfg_listener_->Register("security-group", Agent::GetInstance()->GetSgTable(),
                            SecurityGroup::ID_PERMS);
    cfg_listener_->Register("virtual-machine", Agent::GetInstance()->GetVmTable(),
                            VirtualMachine::ID_PERMS);
    cfg_listener_->Register("virtual-machine-interface",
                            Agent::GetInstance()->GetInterfaceTable(),
                            VirtualMachineInterface::ID_PERMS);
    cfg_listener_->Register("access-control-list", Agent::GetInstance()->GetAclTable(),
                            AccessControlList::ID_PERMS);
    cfg_listener_->Register("routing-instance", Agent::GetInstance()->GetVrfTable(), -1);
    cfg_listener_->Register("virtual-network-network-ipam", 
                            boost::bind(&VnTable::IpamVnSync, _1), -1);
    cfg_listener_->Register("network-ipam", boost::bind(&DomainConfig::IpamSync,
                            Agent::GetInstance()->GetDomainConfigTable(), _1), -1);
    cfg_listener_->Register("virtual-DNS", boost::bind(&DomainConfig::VDnsSync, 
                            Agent::GetInstance()->GetDomainConfigTable(), _1), -1);
    cfg_listener_->Register
        ("virtual-machine-interface-routing-instance", 
         boost::bind(&InterfaceTable::VmInterfaceVrfSync, _1), -1);

    cfg_listener_->Register
        ("instance-ip",
         boost::bind(&VmPortInterface::InstanceIpSync, _1), -1);

    cfg_listener_->Register
        ("floating-ip", 
         boost::bind(&VmPortInterface::FloatingIpVnSync, _1), -1);

    cfg_listener_->Register
        ("floating-ip-pool", 
         boost::bind(&VmPortInterface::FloatingIpPoolSync, _1), -1);

    cfg_listener_->Register
        ("global-vrouter-config",
         boost::bind(&TunnelType::EncapPrioritySync, _1), -1);

    AgentConfig::GetInstance()->
        SetVmInterfaceTable(static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(Agent::GetInstance()->GetDB(),
        "virtual-machine-interface")));
    assert(AgentConfig::GetInstance()->GetVmInterfaceTable());

    AgentConfig::GetInstance()->SetAclTable(static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(Agent::GetInstance()->GetDB(), 
                               "access-control-list")));
    assert(AgentConfig::GetInstance()->GetAclTable());

    AgentConfig::GetInstance()->SetVmTable(static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(Agent::GetInstance()->GetDB(), 
                               "virtual-machine")));
    assert(AgentConfig::GetInstance()->GetVmTable());

    AgentConfig::GetInstance()->SetVnTable(static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(Agent::GetInstance()->GetDB(), 
                               "virtual-network")));
    assert(AgentConfig::GetInstance()->GetVnTable());

    AgentConfig::GetInstance()->SetSgTable(static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(Agent::GetInstance()->GetDB(), 
                               "security-group")));
    assert(AgentConfig::GetInstance()->GetSgTable());         

    AgentConfig::GetInstance()->SetVrfTable(static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(Agent::GetInstance()->GetDB(), 
                               "routing-instance")));
    assert(AgentConfig::GetInstance()->GetVrfTable());

    AgentConfig::GetInstance()->
        SetInstanceIpTable(static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(Agent::GetInstance()->GetDB(), 
                               "instance-ip")));
    assert(AgentConfig::GetInstance()->GetInstanceIpTable());

    AgentConfig::GetInstance()->
        SetFloatingIpTable(static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(Agent::GetInstance()->GetDB(), 
                               "floating-ip")));
    assert(AgentConfig::GetInstance()->GetFloatingIpTable());

    AgentConfig::GetInstance()->
        SetFloatingIpPoolTable(static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(Agent::GetInstance()->GetDB(), 
                               "floating-ip-pool")));
    assert(AgentConfig::GetInstance()->GetFloatingIpPoolTable());

    AgentConfig::GetInstance()->
        SetNetworkIpamTable(static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(Agent::GetInstance()->GetDB(), "network-ipam")));
    assert(AgentConfig::GetInstance()->GetNetworkIpamTable());

    AgentConfig::GetInstance()->
        SetVnNetworkIpamTable(static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(Agent::GetInstance()->GetDB(), 
                               "virtual-network-network-ipam")));
    assert(AgentConfig::GetInstance()->GetVnNetworkIpamTable());

    AgentConfig::GetInstance()->SetVmPortVrfTable(static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(Agent::GetInstance()->GetDB(), 
                               "virtual-machine-interface-routing-instance")));
    assert(AgentConfig::GetInstance()->GetVmPortVrfTable());

    InterfaceCfgClient::Init();
    CfgFilter::Init();
}

void CfgModule::Shutdown() {
    CfgFilter::Shutdown();
    InterfaceCfgClient::Shutdown();

    Agent::GetInstance()->GetDB()->RemoveTable(Agent::GetInstance()->GetIntfCfgTable());
    delete Agent::GetInstance()->GetIntfCfgTable();
    Agent::GetInstance()->SetIntfCfgTable(NULL);

    delete cfg_parser_;
    cfg_parser_ = NULL;
    Agent::GetInstance()->SetIfMapAgentParser(cfg_parser_);


    delete cfg_listener_;
    cfg_listener_ = NULL;

    Agent::GetInstance()->GetIfMapAgentStaleCleaner()->Clear();
    delete Agent::GetInstance()->GetIfMapAgentStaleCleaner();
    Agent::GetInstance()->SetAgentStaleCleaner(NULL);

    delete cfg_graph_;
    cfg_graph_ = NULL;
}
