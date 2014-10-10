/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <string>
#include <vector>
#include <base/logging.h>
#include <base/lifetime.h>
#include <base/misc_utils.h>
#include <io/event_manager.h>
#include <ifmap/ifmap_link.h>

#include <cmn/agent_cmn.h>
#include <vnc_cfg_types.h>
#include <agent_types.h>

#include <init/agent_param.h>
#include <cmn/agent_signal.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_mirror.h>
#include <cfg/discovery_agent.h>
#include <cmn/agent.h>

#include <oper/operdb_init.h>
#include <oper/interface_common.h>
#include <oper/multicast.h>
#include <oper/nexthop.h>
#include <oper/mirror_table.h>
#include <oper/peer.h>

#include <filter/acl.h>

#include <cmn/agent_factory.h>

const std::string Agent::null_string_ = "";
const std::string Agent::fabric_vn_name_ =
    "default-domain:default-project:ip-fabric";
std::string Agent::fabric_vrf_name_ =
    "default-domain:default-project:ip-fabric:__default__";
const std::string Agent::link_local_vn_name_ =
    "default-domain:default-project:__link_local__";
const std::string Agent::link_local_vrf_name_ =
    "default-domain:default-project:__link_local__:__link_local__";
const MacAddress Agent::vrrp_mac_(0x00, 0x00, 0x5E, 0x00, 0x01, 0x00);
const std::string Agent::bcast_mac_ = "FF:FF:FF:FF:FF:FF";
const std::string Agent::config_file_ = "/etc/contrail/contrail-vrouter-agent.conf";
const std::string Agent::log_file_ = "/var/log/contrail/vrouter.log";
const std::string Agent::xmpp_dns_server_connection_name_prefix_ = "dns-server:";
const std::string Agent::xmpp_control_node_connection_name_prefix_ = "control-node:";

Agent *Agent::singleton_;

const string &Agent::GetHostInterfaceName() const {
    // There is single host interface.  Its addressed by type and not name
    return Agent::null_string_;
};

std::string Agent::GetUuidStr(boost::uuids::uuid uuid_val) const {
    std::ostringstream str;
    str << uuid_val;
    return str.str();
}

const string &Agent::vhost_interface_name() const {
    return vhost_interface_name_;
};

bool Agent::isXenMode() {
    return params_->isXenMode();
}

static void SetTaskPolicyOne(const char *task, const char *exclude_list[],
                             int count) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    TaskPolicy policy;
    for (int i = 0; i < count; ++i) {
        int task_id = scheduler->GetTaskId(exclude_list[i]);
        policy.push_back(TaskExclusion(task_id));
    }
    scheduler->SetPolicy(scheduler->GetTaskId(task), policy);
}

void Agent::SetAgentTaskPolicy() {
    /*
     * TODO(roque): this method should not be called by the agent constructor.
     */
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    const char *db_exclude_list[] = {
        "Agent::FlowHandler",
        "Agent::Services",
        "Agent::StatsCollector",
        "sandesh::RecvQueue",
        "io::ReaderTask",
        "Agent::Uve",
        "Agent::KSync",
        "Agent::PktFlowResponder"
    };
    SetTaskPolicyOne("db::DBTable", db_exclude_list, 
                     sizeof(db_exclude_list) / sizeof(char *));

    const char *flow_exclude_list[] = {
        "Agent::StatsCollector",
        "io::ReaderTask",
        "Agent::PktFlowResponder"
    };
    SetTaskPolicyOne("Agent::FlowHandler", flow_exclude_list, 
                     sizeof(flow_exclude_list) / sizeof(char *));

    const char *sandesh_exclude_list[] = {
        "db::DBTable",
        "Agent::FlowHandler",
        "Agent::Services",
        "Agent::StatsCollector",
        "io::ReaderTask",
        "Agent::PktFlowResponder"
    };
    SetTaskPolicyOne("sandesh::RecvQueue", sandesh_exclude_list, 
                     sizeof(sandesh_exclude_list) / sizeof(char *));

    const char *xmpp_config_exclude_list[] = {
        "Agent::FlowHandler",
        "Agent::Services",
        "Agent::StatsCollector",
        "sandesh::RecvQueue",
        "io::ReaderTask",
        "xmpp::StateMachine",
        "db::DBTable"
    };
    SetTaskPolicyOne("bgp::Config", xmpp_config_exclude_list, 
                     sizeof(xmpp_config_exclude_list) / sizeof(char *));

    const char *xmpp_state_machine_exclude_list[] = {
        "io::ReaderTask",
        "db::DBTable"
    };
    SetTaskPolicyOne("xmpp::StateMachine", xmpp_state_machine_exclude_list, 
                     sizeof(xmpp_state_machine_exclude_list) / sizeof(char *));

    const char *ksync_exclude_list[] = {
        "Agent::FlowHandler",
        "Agent::StatsCollector",
        "db::DBTable",
        "Agent::PktFlowResponder"
    };
    SetTaskPolicyOne("Agent::KSync", ksync_exclude_list, 
                     sizeof(ksync_exclude_list) / sizeof(char *));

    const char *stats_collector_exclude_list[] = {
        "Agent::PktFlowResponder"
    };
    SetTaskPolicyOne("Agent::StatsCollector", stats_collector_exclude_list,
                     sizeof(stats_collector_exclude_list) / sizeof(char *));

    const char *metadata_exclude_list[] = {
        "http::RequestHandlerTask"
    };
    SetTaskPolicyOne("http client", metadata_exclude_list,
                     sizeof(metadata_exclude_list) / sizeof(char *));
}

void Agent::CreateLifetimeManager() {
    lifetime_manager_ = new LifetimeManager(
            TaskScheduler::GetInstance()->GetTaskId("db::DBTable"));
}

void Agent::ShutdownLifetimeManager() {
    delete lifetime_manager_;
    lifetime_manager_ = NULL;
}

// Get configuration from AgentParam into Agent
void Agent::CopyConfig(AgentParam *params) {
    params_ = params;

    int count = 0;
    int dns_count = 0;

    if (params_->xmpp_server_1().to_ulong()) {
        SetAgentMcastLabelRange(count);
        xs_addr_[count++] = params_->xmpp_server_1().to_string();
    }

    if (params_->xmpp_server_2().to_ulong()) {
        SetAgentMcastLabelRange(count);
        xs_addr_[count++] = params_->xmpp_server_2().to_string();
    }

    if (params_->dns_server_1().to_ulong()) {
        dns_port_[dns_count] = params_->dns_port_1();
        dns_addr_[dns_count++] = params_->dns_server_1().to_string();
    }

    if (params_->dns_server_2().to_ulong()) {
        dns_port_[dns_count] = params_->dns_port_2();
        dns_addr_[dns_count++] = params_->dns_server_2().to_string();
    }

    if (params_->discovery_server().to_ulong()) {
        dss_addr_ = params_->discovery_server().to_string();
        dss_xs_instances_ = params_->xmpp_instance_count();
    }

    vhost_interface_name_ = params_->vhost_name();
    ip_fabric_intf_name_ = params_->eth_port();
    host_name_ = params_->host_name();
    prog_name_ = params_->program_name();
    introspect_port_ = params_->http_server_port();
    prefix_len_ = params_->vhost_plen();
    gateway_id_ = params_->vhost_gw();
    router_id_ = params_->vhost_addr();
    if (router_id_.to_ulong()) {
        router_id_configured_ = false;
    }

    if (params_->tunnel_type() == "MPLSoUDP")
        TunnelType::SetDefaultType(TunnelType::MPLS_UDP);
    else if (params_->tunnel_type() == "VXLAN")
        TunnelType::SetDefaultType(TunnelType::VXLAN);
    else
        TunnelType::SetDefaultType(TunnelType::MPLS_GRE);

    headless_agent_mode_ = params_->headless_mode();
    simulate_evpn_tor_ = params->simulate_evpn_tor();
    debug_ = params_->debug();
    test_mode_ = params_->test_mode();
}

DiscoveryAgentClient *Agent::discovery_client() const {
    return cfg_->discovery_client();
}

CfgListener *Agent::cfg_listener() const { 
    return cfg_->cfg_listener();
}

void Agent::set_cn_mcast_builder(AgentXmppChannel *peer) {
    cn_mcast_builder_ =  peer;
}

void Agent::InitCollector() {
    /* If Sandesh initialization is not being done via discovery we need to
     * initialize here. We need to do sandesh initialization here for cases
     * (i) When both Discovery and Collectors are configured.
     * (ii) When both are not configured (to initilialize introspect)
     * (iii) When only collector is configured
     */
    if (!discovery_server().empty() &&
        params_->collector_server_list().size() == 0) {
        return;
    }

    /* If collector configuration is specified, use that for connection to
     * collector. If not we still need to invoke InitGenerator to initialize
     * introspect.
     */
    Module::type module = Module::VROUTER_AGENT;
    NodeType::type node_type =
        g_vns_constants.Module2NodeType.find(module)->second;
    if (params_->collector_server_list().size() != 0) {
        Sandesh::InitGenerator(discovery_client_name_,
                params_->host_name(),
                g_vns_constants.NodeTypeNames.find(node_type)->second,
                instance_id_,
                event_manager(),
                params_->http_server_port(), 0,
                params_->collector_server_list(),
                NULL);
    } else {
        Sandesh::InitGenerator(discovery_client_name_,
                params_->host_name(),
                g_vns_constants.NodeTypeNames.find(node_type)->second,
                instance_id_,
                event_manager(),
                params_->http_server_port(),
                NULL);
    }

}

static bool interface_exist(string &name) {
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

void Agent::InitXenLinkLocalIntf() {
    if (!params_->isXenMode() || params_->xen_ll_name() == "")
        return;

    string dev_name = params_->xen_ll_name();
    if(!interface_exist(dev_name)) {
        LOG(INFO, "Interface " << dev_name << " not found");
        return;
    }
    params_->set_xen_ll_name(dev_name);

    InetInterface::Create(intf_table_, params_->xen_ll_name(),
                          InetInterface::LINK_LOCAL, link_local_vrf_name_,
                          params_->xen_ll_addr(), params_->xen_ll_plen(),
                          params_->xen_ll_gw(), NullString(), link_local_vrf_name_);
}

void Agent::InitPeers() {
    // Create peer entries
    local_peer_.reset(new Peer(Peer::LOCAL_PEER, LOCAL_PEER_NAME));
    local_vm_peer_.reset(new Peer(Peer::LOCAL_VM_PEER, LOCAL_VM_PEER_NAME));
    linklocal_peer_.reset(new Peer(Peer::LINKLOCAL_PEER, LINKLOCAL_PEER_NAME));
    ecmp_peer_.reset(new Peer(Peer::ECMP_PEER, ECMP_PEER_NAME));
    vgw_peer_.reset(new Peer(Peer::VGW_PEER, VGW_PEER_NAME));
    multicast_peer_.reset(new Peer(Peer::MULTICAST_PEER, MULTICAST_PEER_NAME));
    multicast_tree_builder_peer_.reset(
                                 new Peer(Peer::MULTICAST_FABRIC_TREE_BUILDER,
                                          MULTICAST_FABRIC_TREE_BUILDER_NAME));
}

Agent::Agent() :
    params_(NULL), event_mgr_(NULL), agent_xmpp_channel_(),
    ifmap_channel_(), xmpp_client_(), xmpp_init_(), dns_xmpp_channel_(),
    dns_xmpp_client_(), dns_xmpp_init_(), agent_stale_cleaner_(NULL),
    cn_mcast_builder_(NULL), ds_client_(NULL), host_name_(""),
    prog_name_(""), introspect_port_(0),
    instance_id_(g_vns_constants.INSTANCE_ID_DEFAULT), db_(NULL),
    task_scheduler_(NULL), agent_init_(NULL), intf_table_(NULL),
    nh_table_(NULL), uc_rt_table_(NULL), mc_rt_table_(NULL), vrf_table_(NULL),
    vm_table_(NULL), vn_table_(NULL), sg_table_(NULL), mpls_table_(NULL),
    acl_table_(NULL), mirror_table_(NULL), vrf_assign_table_(NULL),
    mirror_cfg_table_(NULL), intf_mirror_cfg_table_(NULL),
    intf_cfg_table_(NULL), router_id_(0), prefix_len_(0), 
    gateway_id_(0), xs_cfg_addr_(""), xs_idx_(0), xs_addr_(), xs_port_(),
    xs_stime_(), xs_dns_idx_(0), dns_addr_(), dns_port_(),
    dss_addr_(""), dss_port_(0), dss_xs_instances_(0), discovery_client_name_(),
    label_range_(), ip_fabric_intf_name_(""), vhost_interface_name_(""),
    pkt_interface_name_("pkt0"), cfg_listener_(NULL), arp_proto_(NULL),
    dhcp_proto_(NULL), dns_proto_(NULL), icmp_proto_(NULL),
    dhcpv6_proto_(NULL), icmpv6_proto_(NULL), flow_proto_(NULL),
    local_peer_(NULL), local_vm_peer_(NULL), linklocal_peer_(NULL),
    vgw_peer_(NULL), ifmap_parser_(NULL), router_id_configured_(false),
    mirror_src_udp_port_(0), lifetime_manager_(NULL), 
    ksync_sync_mode_(true), mgmt_ip_(""),
    vxlan_network_identifier_mode_(AUTOMATIC), headless_agent_mode_(false), 
    connection_state_(NULL), debug_(false), test_mode_(false),
    init_done_(false), simulate_evpn_tor_(false) {

    assert(singleton_ == NULL);
    singleton_ = this;
    db_ = new DB();
    assert(db_);

    event_mgr_ = new EventManager();
    assert(event_mgr_);

    SetAgentTaskPolicy();
    CreateLifetimeManager();

    discovery_client_name_ = g_vns_constants.ModuleNames.find(Module::VROUTER_AGENT)->second;

    agent_signal_.reset(
        AgentObjectFactory::Create<AgentSignal>(event_mgr_));
}

Agent::~Agent() {
    uve_ = NULL;

    agent_signal_->Terminate();
    agent_signal_.reset();

    delete event_mgr_;
    event_mgr_ = NULL;

    ShutdownLifetimeManager();

    delete db_;
    db_ = NULL;
    singleton_ = NULL;
}

AgentConfig *Agent::cfg() const {
    return cfg_;
}

void Agent::set_cfg(AgentConfig *cfg) {
    cfg_ = cfg;
}

DiagTable *Agent::diag_table() const {
    return diag_table_;
}

void Agent::set_diag_table(DiagTable *table) {
    diag_table_ = table;
}

AgentStats *Agent::stats() const {
    return stats_;
}

void Agent::set_stats(AgentStats *stats) {
    stats_ = stats;
}

KSync *Agent::ksync() const {
    return ksync_;
}

void Agent::set_ksync(KSync *ksync) {
    ksync_ = ksync;
}

AgentUve *Agent::uve() const {
    return uve_;
}

void Agent::set_uve(AgentUve *uve) {
    uve_ = uve;
}

PktModule *Agent::pkt() const {
    return pkt_;
}

void Agent::set_pkt(PktModule *pkt) {
    pkt_ = pkt;
}

ServicesModule *Agent::services() const {
    return services_;
}

void Agent::set_services(ServicesModule *services) {
    services_ = services;
}

VNController *Agent::controller() const {
    return controller_;
}

void Agent::set_controller(VNController *val) {
    controller_ = val;
}

VirtualGateway *Agent::vgw() const {
    return vgw_;
}

void Agent::set_vgw(VirtualGateway *vgw) {
    vgw_ = vgw;
}

OperDB *Agent::oper_db() const {
    return oper_db_;
}

void Agent::set_oper_db(OperDB *oper_db) {
    oper_db_ = oper_db;
}

DomainConfig *Agent::domain_config_table() const {
    return oper_db_->domain_config_table();
}
