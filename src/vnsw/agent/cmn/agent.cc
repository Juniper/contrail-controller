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

#include <vnc_cfg_types.h>
#include <cmn/agent_cmn.h>
#include <cmn/agent_stats.h>

#include <init/agent_param.h>
#include <init/agent_init.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_mirror.h>
#include <cfg/discovery_agent.h>

#include <oper/operdb_init.h>
#include <oper/interface_common.h>
#include <oper/multicast.h>
#include <oper/nexthop.h>
#include <oper/mirror_table.h>

#include <services/services_init.h>
#include <pkt/pkt_init.h>
#include <pkt/flow_table.h>
#include <pkt/pkt_types.h>
#include <pkt/proto.h>
#include <pkt/proto_handler.h>
#include <uve/flow_stats_collector.h>
#include <uve/agent_uve.h>
#include <vgw/cfg_vgw.h>
#include <vgw/vgw.h>
#include <cmn/agent_factory.h>
#include <controller/controller_init.h>

#include <diag/diag.h>
#include <ksync/ksync_init.h>

const std::string Agent::null_str_ = "";
const std::string Agent::fabric_vn_name_ = 
    "default-domain:default-project:ip-fabric";
std::string Agent::fabric_vrf_name_ =
    "default-domain:default-project:ip-fabric:__default__";
const std::string Agent::link_local_vn_name_ = 
    "default-domain:default-project:__link_local__";
const std::string Agent::link_local_vrf_name_ = 
    "default-domain:default-project:__link_local__:__link_local__";
const uint8_t Agent::vrrp_mac_[] = {0x00, 0x00, 0x5E, 0x00, 0x01, 0x00};
const std::string Agent::bcast_mac_ = "FF:FF:FF:FF:FF:FF";
const std::string Agent::config_file_ = "/etc/contrail/contrail-vrouter-agent.conf";
const std::string Agent::log_file_ = "/var/log/contrail/vrouter.log";

Agent *Agent::singleton_;

const string &Agent::GetHostInterfaceName() {
    // There is single host interface.  Its addressed by type and not name
    return Agent::null_str_;
};

const string &Agent::vhost_interface_name() const {
    return vhost_interface_name_;
};

const string &Agent::GetHostName() {
    return host_name_;
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
void Agent::CopyConfig(AgentParam *params, AgentInit *init) {
    params_ = params;
    init_ = init;

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
        xs_dns_addr_[dns_count++] = params_->dns_server_1().to_string();
    }

    if (params_->dns_server_2().to_ulong()) {
        xs_dns_addr_[dns_count++] = params_->dns_server_2().to_string();
    }

    if (params_->discovery_server().to_ulong()) {
        dss_addr_ = params_->discovery_server().to_string();
        dss_xs_instances_ = params_->xmpp_instance_count();
    }

    vhost_interface_name_ = params_->vhost_name();
    ip_fabric_intf_name_ = params_->eth_port();
    host_name_ = params_->host_name();
    prog_name_ = params_->program_name();
    sandesh_port_ = params_->http_server_port();
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
    debug_ = params_->debug();
    test_mode_ = params_->test_mode();
}

DiscoveryAgentClient *Agent::discovery_client() const {
    return cfg_.get()->discovery_client();
}

CfgListener *Agent::cfg_listener() const { 
    return cfg_.get()->cfg_listener();
}

void Agent::set_cn_mcast_builder(AgentXmppChannel *peer) {
    cn_mcast_builder_ =  peer;
}

void Agent::InitCollector() {
    // If discovery server is not specified, init connection to collector
    // based on configuration
    if (dss_addr_.empty() == false) {
        return;
    }

    Module::type module = Module::VROUTER_AGENT;
    NodeType::type node_type =
        g_vns_constants.Module2NodeType.find(module)->second;
    Sandesh::InitGenerator(g_vns_constants.ModuleNames.find(module)->second,
                           params_->host_name(),
                           g_vns_constants.NodeTypeNames.find(node_type)->second,
                           g_vns_constants.INSTANCE_ID_DEFAULT,
                           GetEventManager(),
                           params_->http_server_port());

    if (params_->collector_port() != 0 && 
        params_->collector().to_ulong() != 0) {
        Sandesh::ConnectToCollector(params_->collector().to_string(),
                                    params_->collector_port());
    }
}

void Agent::CreateDBTables() {
    if (cfg_.get()) {
        cfg_.get()->CreateDBTables(db_);
    }

    if (oper_db_.get()) {
        oper_db_.get()->CreateDBTables(db_);
    }
}

void Agent::CreateDBClients() {
    if (cfg_.get()) {
        cfg_.get()->RegisterDBClients(db_);
    }

    if (oper_db_.get()) {
        oper_db_.get()->CreateDBClients();
    }
}

void Agent::InitPeers() {
    // Create peer entries
    local_peer_.reset(new Peer(Peer::LOCAL_PEER, LOCAL_PEER_NAME));
    local_vm_peer_.reset(new Peer(Peer::LOCAL_VM_PEER, LOCAL_VM_PEER_NAME));
    linklocal_peer_.reset(new Peer(Peer::LINKLOCAL_PEER, LINKLOCAL_PEER_NAME));
    ecmp_peer_.reset(new Peer(Peer::ECMP_PEER, ECMP_PEER_NAME));
    vgw_peer_.reset(new Peer(Peer::VGW_PEER, VGW_PEER_NAME));
}

void Agent::InitModules() {
    if (cfg_.get()) {
        cfg_.get()->Init();
    }

    if (oper_db_.get()) {
        oper_db_.get()->Init();
    }
}

Agent::Agent() :
    params_(NULL), init_(NULL), event_mgr_(NULL), agent_xmpp_channel_(),
    ifmap_channel_(), xmpp_client_(), xmpp_init_(), dns_xmpp_channel_(),
    dns_xmpp_client_(), dns_xmpp_init_(), agent_stale_cleaner_(NULL),
    cn_mcast_builder_(NULL), ds_client_(NULL), host_name_(""),
    prog_name_(""), sandesh_port_(0), db_(NULL), intf_table_(NULL),
    nh_table_(NULL), uc_rt_table_(NULL), mc_rt_table_(NULL), vrf_table_(NULL),
    vm_table_(NULL), vn_table_(NULL), sg_table_(NULL), addr_table_(NULL),
    mpls_table_(NULL), acl_table_(NULL), mirror_table_(NULL),
    vrf_assign_table_(NULL), mirror_cfg_table_(NULL),
    intf_mirror_cfg_table_(NULL), intf_cfg_table_(NULL), 
    domain_config_table_(NULL), router_id_(0), prefix_len_(0), 
    gateway_id_(0), xs_cfg_addr_(""), xs_idx_(0), xs_addr_(), xs_port_(),
    xs_stime_(), xs_dns_idx_(0), xs_dns_addr_(), xs_dns_port_(),
    dss_addr_(""), dss_port_(0), dss_xs_instances_(0), label_range_(),
    ip_fabric_intf_name_(""), vhost_interface_name_(""),
    pkt_interface_name_("pkt0"), cfg_listener_(NULL), arp_proto_(NULL),
    dhcp_proto_(NULL), dns_proto_(NULL), icmp_proto_(NULL), flow_proto_(NULL),
    local_peer_(NULL), local_vm_peer_(NULL), linklocal_peer_(NULL),
    vgw_peer_(NULL), ifmap_parser_(NULL), router_id_configured_(false),
    mirror_src_udp_port_(0), lifetime_manager_(NULL), 
    ksync_sync_mode_(true), mgmt_ip_(""),
    vxlan_network_identifier_mode_(AUTOMATIC), headless_agent_mode_(false), 
    debug_(false), test_mode_(false) {

    assert(singleton_ == NULL);
    singleton_ = this;
    db_ = new DB();
    assert(db_);

    event_mgr_ = new EventManager();
    assert(event_mgr_);

    SetAgentTaskPolicy();
    CreateLifetimeManager();
}

Agent::~Agent() {
    delete event_mgr_;
    event_mgr_ = NULL;

    ShutdownLifetimeManager();

    delete db_;
    db_ = NULL;
}

AgentConfig *Agent::cfg() const {
    return cfg_.get();
}

void Agent::set_cfg(AgentConfig *cfg) {
    cfg_.reset(cfg);
}

DiagTable *Agent::diag_table() const {
    return diag_table_.get();
}

void Agent::set_diag_table(DiagTable *table) {
    diag_table_.reset(table);
}

AgentStats *Agent::stats() const {
    return stats_.get();
}

void Agent::set_stats(AgentStats *stats) {
    stats_.reset(stats);
}

KSync *Agent::ksync() const {
    return ksync_.get();
}

void Agent::set_ksync(KSync *ksync) {
    return ksync_.reset(ksync);
}

AgentUve *Agent::uve() const {
    return uve_.get();
}

void Agent::set_uve(AgentUve *uve) {
    uve_.reset(uve);
}

PktModule *Agent::pkt() const {
    return pkt_.get();
}

void Agent::set_pkt(PktModule *pkt) {
    pkt_.reset(pkt);
}

ServicesModule *Agent::services() const {
    return services_.get();
}

void Agent::set_services(ServicesModule *services) {
    services_.reset(services);
}

VNController *Agent::controller() const {
    return controller_.get();
}

void Agent::set_controller(VNController *val) {
    controller_.reset(val);
}

VirtualGateway *Agent::vgw() const {
    return vgw_.get();
}

void Agent::set_vgw(VirtualGateway *vgw) {
    vgw_.reset(vgw);
}

OperDB *Agent::oper_db() const {
    return oper_db_.get();
}

void Agent::set_oper_db(OperDB *oper_db) {
    oper_db_.reset(oper_db);
}
