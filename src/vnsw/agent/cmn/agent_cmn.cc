/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <string>
#include <vector>
#include <base/logging.h>
#include <base/lifetime.h>
#include <io/event_manager.h>
#include <cmn/agent_cmn.h>
#include "cfg/init_config.h"
#include <oper/interface.h>
#include <oper/nexthop.h>
#include <oper/mirror_table.h>
#include <pkt/flowtable.h>
#include <pkt/pkt_types.h>
#include "uve/flow_stats.h"
#include <base/misc_utils.h>
#include <cmn/buildinfo.h>

std::string Agent::null_str_ = "";
std::string Agent::collector_ = "";
int Agent::collector_port_;
std::string Agent::fabric_vn_name_ = "default-domain:default-project:ip-fabric";
std::string Agent::fabric_vrf_name_ =
    "default-domain:default-project:ip-fabric:__default__";
std::string Agent::link_local_vn_name_ = 
    "default-domain:default-project:__link_local__";
std::string Agent::link_local_vrf_name_ = 
    "default-domain:default-project:__link_local__:__link_local__";

EventManager *Agent::event_mgr_;
AgentXmppChannel *Agent::agent_xmpp_channel_[MAX_XMPP_SERVERS];
AgentIfMapXmppChannel *Agent::ifmap_channel_[MAX_XMPP_SERVERS];
XmppClient *Agent::xmpp_client_[MAX_XMPP_SERVERS];
XmppInit *Agent::xmpp_init_[MAX_XMPP_SERVERS];
AgentDnsXmppChannel *Agent::dns_xmpp_channel_[MAX_XMPP_SERVERS];
XmppClient *Agent::dns_xmpp_client_[MAX_XMPP_SERVERS];
XmppInit *Agent::dns_xmpp_init_[MAX_XMPP_SERVERS];
DiscoveryServiceClient *Agent::ds_client_;
AgentXmppChannel *Agent::cn_mcast_builder_;
bool Agent::router_id_configured_;

// DB handles
DB *Agent::db_;
InterfaceTable *Agent::intf_table_;
NextHopTable *Agent::nh_table_;
VrfTable *Agent::vrf_table_;
VmTable *Agent::vm_table_;
VnTable *Agent::vn_table_;
SgTable *Agent::sg_table_;
AddrTable *Agent::addr_table_;
MplsTable *Agent::mpls_table_;
AclTable *Agent::acl_table_;
MirrorTable *Agent::mirror_table_;
Inet4UcRouteTable *Agent::uc_rt_table_;
Inet4McRouteTable *Agent::mc_rt_table_;
VrfAssignTable *Agent::vrf_assign_table_;
AgentStats *AgentStats::singleton_;

// Config DB Table handles
CfgIntTable *Agent::intf_cfg_table_;
CfgListener *Agent::cfg_listener_;

// Mirror Cfg table handles
MirrorCfgTable *Agent::mirror_cfg_table_;
IntfMirrorCfgTable *Agent::intf_mirror_cfg_table_;

Ip4Address Agent::router_id_;
uint32_t Agent::prefix_len_;
Ip4Address Agent::gateway_id_;
std::string Agent::xs_cfg_addr_;
int8_t Agent::xs_idx_;
std::string Agent::xs_addr_[MAX_XMPP_SERVERS];
uint32_t Agent::xs_port_[MAX_XMPP_SERVERS];
uint64_t Agent::xs_stime_[MAX_XMPP_SERVERS];
int8_t Agent::xs_dns_idx_ = -1;
std::string Agent::xs_dns_addr_[MAX_XMPP_SERVERS];
uint32_t Agent::xs_dns_port_[MAX_XMPP_SERVERS];
std::string Agent::dss_addr_;
uint32_t Agent::dss_port_;
int Agent::dss_xs_instances_;
std::string Agent::label_range_[MAX_XMPP_SERVERS];
std::string Agent::ip_fabric_intf_name_;
Peer *Agent::local_peer_;
Peer *Agent::local_vm_peer_;
Peer *Agent::mdata_vm_peer_;
IFMapAgentParser *Agent::ifmap_parser_;
IFMapAgentStaleCleaner *Agent::agent_stale_cleaner_;
std::string Agent::virtual_host_intf_name_;
std::string Agent::host_name_;
std::string Agent::prog_name_;
LifetimeManager* Agent::lifetime_manager_;
uint16_t Agent::mirror_src_udp_port_;
bool Agent::test_mode_ = false;
int Agent::sandesh_port_;

AgentInit *AgentInit::instance_;

const string &Agent::GetHostInterfaceName() {
    // There is single host interface.  Its addressed by type and not name
    return Agent::null_str_;
};

const string &Agent::GetVirtualHostInterfaceName() {
    return virtual_host_intf_name_;
};

void Agent::SetVirtualHostInterfaceName(const string &name) {
    virtual_host_intf_name_ = name;
};

const string &Agent::GetHostName() {
    return host_name_;
};

const string Agent::GetBuildInfo() {
    return MiscUtils::GetBuildInfo(MiscUtils::Agent, BuildInfo);
};

void Agent::SetHostName(const string &name) {
    host_name_ = name;
};

bool Agent::isXenMode() {
    AgentConfig *config = AgentConfig::GetInstance();
    return config->isXenMode();
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
        "Agent::KSync"
    };
    SetTaskPolicyOne("db::DBTable", db_exclude_list, 
                     sizeof(db_exclude_list) / sizeof(char *));

    const char *flow_exclude_list[] = {
        "Agent::StatsCollector",
        "io::ReaderTask"
    };
    SetTaskPolicyOne("Agent::FlowHandler", flow_exclude_list, 
                     sizeof(flow_exclude_list) / sizeof(char *));

    const char *sandesh_exclude_list[] = {
        "db::DBTable",
        "Agent::FlowHandler",
        "Agent::Services",
        "Agent::StatsCollector",
        "io::ReaderTask",
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
        "db::DBTable"
    };
    SetTaskPolicyOne("Agent::KSync", ksync_exclude_list, 
                     sizeof(ksync_exclude_list) / sizeof(char *));
}

void Agent::CreateLifetimeManager() {
    lifetime_manager_ = new LifetimeManager(
            TaskScheduler::GetInstance()->GetTaskId("db::DBTable"));
}

void Agent::ShutdownLifetimeManager() {
    delete lifetime_manager_;
    lifetime_manager_ = NULL;
}

void AgentStatsReq::HandleRequest() const {
    IpcStatsResp *ipc = new IpcStatsResp();
    ipc->set_ipc_in_msgs(AgentStats::GetIpcInMsgs());
    ipc->set_ipc_out_msgs(AgentStats::GetIpcOutMsgs());
    ipc->set_context(context());
    ipc->set_more(true);
    ipc->Response();

    PktTrapStatsResp *pkt = new PktTrapStatsResp();
    pkt->set_exceptions(AgentStats::GetPktExceptions());
    pkt->set_invalid_agent_hdr(AgentStats::GetPktInvalidAgentHdr());
    pkt->set_invalid_interface(AgentStats::GetPktInvalidInterface());
    pkt->set_no_handler(AgentStats::GetPktNoHandler());
    pkt->set_pkt_dropped(AgentStats::GetPktDropped());
    pkt->set_context(context());
    pkt->set_more(true);
    pkt->Response();

    FlowStatsResp *flow = new FlowStatsResp();
    flow->set_flow_active(AgentStats::GetFlowActive());
    flow->set_flow_created(AgentStats::GetFlowCreated());
    flow->set_flow_aged(AgentStats::GetFlowAged());
    flow->set_context(context());
    flow->set_more(true);
    flow->Response();

    XmppStatsResp *xmpp_resp = new XmppStatsResp();
    vector<XmppStatsInfo> list;
    for (int count = 0; count < MAX_XMPP_SERVERS; count++) {
        XmppStatsInfo peer;
        if (!Agent::GetXmppServer(count).empty()) {
            peer.set_ip(Agent::GetXmppServer(count));
            AgentXmppChannel *ch = Agent::GetAgentXmppChannel(count);
            if (ch == NULL) {
                continue;
            }
            XmppChannel *xc = ch->GetXmppChannel();
            if (xc == NULL) {
                continue;
            }
            peer.set_reconnect(AgentStats::GetXmppReconnect(count));
            peer.set_in_msgs(AgentStats::GetXmppInMsgs(count));
            peer.set_out_msgs(AgentStats::GetXmppOutMsgs(count));
            list.push_back(peer);
        }
    }
    xmpp_resp->set_xmpp_list(list);
    xmpp_resp->set_context(context());
    xmpp_resp->set_more(true);
    xmpp_resp->Response();

    SandeshStatsResp *sandesh = new SandeshStatsResp();
    sandesh->set_sandesh_in_msgs(AgentStats::GetSandeshInMsgs());
    sandesh->set_sandesh_out_msgs(AgentStats::GetSandeshOutMsgs());
    sandesh->set_sandesh_http_sessions(AgentStats::GetSandeshHttpSessions());
    sandesh->set_sandesh_reconnects(AgentStats::GetSandeshReconnects());
    sandesh->set_context(context());
    sandesh->set_more(false);
    sandesh->Response();
}
