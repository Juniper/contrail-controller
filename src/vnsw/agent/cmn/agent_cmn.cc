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

const std::string Agent::null_str_ = "";
const std::string Agent::fabric_vn_name_ = "default-domain:default-project:ip-fabric";
const std::string Agent::fabric_vrf_name_ =
    "default-domain:default-project:ip-fabric:__default__";
const std::string Agent::link_local_vn_name_ = 
    "default-domain:default-project:__link_local__";
const std::string Agent::link_local_vrf_name_ = 
    "default-domain:default-project:__link_local__:__link_local__";

AgentStats *AgentStats::singleton_;
Agent *Agent::singleton_;
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

bool Agent::GetBuildInfo(std::string &build_info_str) {
    return MiscUtils::GetBuildInfo(MiscUtils::Agent, BuildInfo, build_info_str);
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
    ipc->set_ipc_in_msgs(AgentStats::GetInstance()->GetIpcInMsgs());
    ipc->set_ipc_out_msgs(AgentStats::GetInstance()->GetIpcOutMsgs());
    ipc->set_context(context());
    ipc->set_more(true);
    ipc->Response();

    PktTrapStatsResp *pkt = new PktTrapStatsResp();
    pkt->set_exceptions(AgentStats::GetInstance()->GetPktExceptions());
    pkt->set_invalid_agent_hdr(AgentStats::GetInstance()->GetPktInvalidAgentHdr());
    pkt->set_invalid_interface(AgentStats::GetInstance()->GetPktInvalidInterface());
    pkt->set_no_handler(AgentStats::GetInstance()->GetPktNoHandler());
    pkt->set_pkt_dropped(AgentStats::GetInstance()->GetPktDropped());
    pkt->set_context(context());
    pkt->set_more(true);
    pkt->Response();

    FlowStatsResp *flow = new FlowStatsResp();
    flow->set_flow_active(AgentStats::GetInstance()->GetFlowActive());
    flow->set_flow_created(AgentStats::GetInstance()->GetFlowCreated());
    flow->set_flow_aged(AgentStats::GetInstance()->GetFlowAged());
    flow->set_context(context());
    flow->set_more(true);
    flow->Response();

    XmppStatsResp *xmpp_resp = new XmppStatsResp();
    vector<XmppStatsInfo> list;
    for (int count = 0; count < MAX_XMPP_SERVERS; count++) {
        XmppStatsInfo peer;
        if (!Agent::GetInstance()->GetXmppServer(count).empty()) {
            peer.set_ip(Agent::GetInstance()->GetXmppServer(count));
            AgentXmppChannel *ch = Agent::GetInstance()->GetAgentXmppChannel(count);
            if (ch == NULL) {
                continue;
            }
            XmppChannel *xc = ch->GetXmppChannel();
            if (xc == NULL) {
                continue;
            }
            peer.set_reconnect(AgentStats::GetInstance()->GetXmppReconnect(count));
            peer.set_in_msgs(AgentStats::GetInstance()->GetXmppInMsgs(count));
            peer.set_out_msgs(AgentStats::GetInstance()->GetXmppOutMsgs(count));
            list.push_back(peer);
        }
    }
    xmpp_resp->set_xmpp_list(list);
    xmpp_resp->set_context(context());
    xmpp_resp->set_more(true);
    xmpp_resp->Response();

    SandeshStatsResp *sandesh = new SandeshStatsResp();
    sandesh->set_sandesh_in_msgs(AgentStats::GetInstance()->GetSandeshInMsgs());
    sandesh->set_sandesh_out_msgs(AgentStats::GetInstance()->GetSandeshOutMsgs());
    sandesh->set_sandesh_http_sessions(AgentStats::GetInstance()->GetSandeshHttpSessions());
    sandesh->set_sandesh_reconnects(AgentStats::GetInstance()->GetSandeshReconnects());
    sandesh->set_context(context());
    sandesh->set_more(false);
    sandesh->Response();
}
