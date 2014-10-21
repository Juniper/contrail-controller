/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/cpuinfo.h>
#include <db/db.h>
#include <cmn/agent_cmn.h>
#include <oper/interface_common.h>
#include <oper/interface.h>
#include <asm/types.h>

#include "vr_genetlink.h"
#include "nl_util.h"

#include <uve/stats_collector.h>
#include <uve/agent_uve.h>
#include <uve/stats_interval_types.h>
#include <init/agent_param.h>
#include <oper/mirror_table.h>
#include <uve/vrouter_stats_collector.h>

using process::ConnectionInfo;
using process::ProcessState;
using process::ConnectionStatus;
using process::ConnectionState;
using process::ConnectionStateManager;
using process::g_process_info_constants;

AgentUve *AgentUve::singleton_;

AgentUve::AgentUve(Agent *agent, uint64_t intvl) 
    : vn_uve_table_(new VnUveTable(agent)), 
      vm_uve_table_(new VmUveTable(agent)), 
      vrouter_uve_entry_(new VrouterUveEntry(agent)),
      agent_stats_collector_(new AgentStatsCollector(
                                 *(agent->event_manager()->io_service()),
                                 agent)),
      flow_stats_collector_(new FlowStatsCollector(
                                 *(agent->event_manager()->io_service()),
                                 agent->params()->flow_stats_interval(),
                                 agent->params()->flow_cache_timeout(),
                                 this)),
      agent_(agent), bandwidth_intvl_(intvl),
      vrouter_stats_collector_(new VrouterStatsCollector(
                                   *(agent->event_manager()->io_service()),
                                   this)) {
    singleton_ = this;
}

AgentUve::~AgentUve() {
}

void AgentUve::Shutdown() {
    agent_stats_collector_->Shutdown();
    vn_uve_table_.get()->Shutdown();
    vm_uve_table_.get()->Shutdown();
    vrouter_uve_entry_.get()->Shutdown();
    connection_state_manager_->Shutdown();
    flow_stats_collector_->Shutdown();
    vrouter_stats_collector_->Shutdown();
}

void AgentUve::Init() {
    std::string module_id(agent_->discovery_client_name());
    std::string instance_id(agent_->instance_id());
    EventManager *evm = agent_->event_manager();
    boost::asio::io_service &io = *evm->io_service();

    CpuLoadData::Init();
    agent_->set_connection_state(ConnectionState::GetInstance());
    connection_state_manager_ =
        ConnectionStateManager<NodeStatusUVE, NodeStatus>::
            GetInstance();
    connection_state_manager_->Init(io, agent_->host_name(),
            module_id, instance_id,
            boost::bind(&AgentUve::VrouterAgentProcessState,
                        this, _1, _2, _3));
}

uint8_t AgentUve::ExpectedConnections(uint8_t &num_control_nodes,
                                      uint8_t &num_dns_servers) {
    uint8_t count = 0;

    /* If Discovery server is configured, increment the count to
     * 3 for 3 possible discovery services */
    if (!agent_->discovery_server().empty()) {
        // Discovery:Collector
        // Discovery:dns-server
        // Discovery:xmpp-server
        count += 3;
    }
    for (int i = 0; i < MAX_XMPP_SERVERS; i++) {
        if (!agent_->controller_ifmap_xmpp_server(i).empty()) {
            num_control_nodes++;
            count++;
        }
        if (!agent_->dns_server(i).empty()) {
            num_dns_servers++;
            count++;
        }
    }
    //Increment 1 for collector service
    count++;
    return count;
}

void AgentUve::UpdateState(const ConnectionInfo &cinfo,
                            ProcessState::type &pstate,
                            std::string &message) {
    pstate = ProcessState::NON_FUNCTIONAL;
    message = cinfo.get_type() + " connection is down";
}

void AgentUve::VrouterAgentProcessState
    (const std::vector<ConnectionInfo> &cinfos,
     ProcessState::type &pstate, std::string &message) {
    size_t num_conns(cinfos.size());
    uint8_t num_control_nodes = 0, num_dns_servers = 0;
    uint8_t down_control_nodes = 0, down_dns_servers = 0;
    uint8_t expected_conns = ExpectedConnections(num_control_nodes,
                                                 num_dns_servers);
    if (num_conns != expected_conns) {
        pstate = ProcessState::NON_FUNCTIONAL;
        message = "Number of connections:" + integerToString(num_conns) +
            ", Expected: " + integerToString(expected_conns);
        return;
    }
    std::string cup(g_process_info_constants.ConnectionStatusNames.
        find(ConnectionStatus::UP)->second);
    // Iterate to determine process connectivity status
    for (std::vector<ConnectionInfo>::const_iterator it = cinfos.begin();
         it != cinfos.end(); it++) {
        const ConnectionInfo &cinfo(*it);
        const std::string &conn_status(cinfo.get_status());
        if (conn_status != cup) {
            if (cinfo.get_name().compare(0, 13,
                agent_->xmpp_control_node_prefix()) == 0) {
                down_control_nodes++;
                if (num_control_nodes == down_control_nodes) {
                    UpdateState(cinfo, pstate, message);
                    return;
                }
            } else if (cinfo.get_name().compare(0, 11,
                agent_->xmpp_dns_server_prefix()) == 0) {
                down_dns_servers++;
                if (num_dns_servers == down_dns_servers) {
                    UpdateState(cinfo, pstate, message);
                    return;
                }
            } else {
                UpdateState(cinfo, pstate, message);
                return;
            }
        }
    }
    pstate = ProcessState::FUNCTIONAL;
    return;
}

void AgentUve::RegisterDBClients() {
    agent_stats_collector_->RegisterDBClients();
    vn_uve_table_.get()->RegisterDBClients();
    vm_uve_table_.get()->RegisterDBClients();
    vrouter_uve_entry_.get()->RegisterDBClients();
}

void AgentUve::NewFlow(const FlowEntry *flow) {
    uint8_t proto = flow->key().protocol;
    uint16_t sport = flow->key().src_port;
    uint16_t dport = flow->key().dst_port;

    // Update vrouter port bitmap
    vrouter_uve_entry_.get()->UpdateBitmap(proto, sport, dport);

    // Update source-vn port bitmap
    vn_uve_table_.get()->UpdateBitmap(flow->data().source_vn, 
                                      proto, sport, dport);

    // Update dest-vn port bitmap
    vn_uve_table_.get()->UpdateBitmap(flow->data().dest_vn, 
                                      proto, sport, dport);

    const Interface *intf = flow->data().intf_entry.get();

    const VmInterface *port = dynamic_cast<const VmInterface *>(intf);
    if (port == NULL) {
        return;
    }
    const VmEntry *vm = port->vm();
    if (vm == NULL) {
        return;
    }

    // update vm and interface (all interfaces of vm) bitmap
    vm_uve_table_.get()->UpdateBitmap(vm, proto, sport, dport);
}

void AgentUve::DeleteFlow(const FlowEntry *flow) {
    /* We need not reset bitmaps on flow deletion. We will have to 
     * provide introspect to reset this */
}

void SetFlowStatsInterval_InSeconds::HandleRequest() const {
    SandeshResponse *resp;
    if (get_interval() > 0) {
        AgentUve::GetInstance()->flow_stats_collector()->
            set_expiry_time(get_interval() * 1000);
        resp = new StatsCfgResp();
    } else {
        resp = new StatsCfgErrResp();
    }

    resp->set_context(context());
    resp->Response();
    return;
}

void SetAgentStatsInterval_InSeconds::HandleRequest() const {
    SandeshResponse *resp;
    if (get_interval() > 0) {
        AgentUve::GetInstance()->agent_stats_collector()->
            set_expiry_time(get_interval() * 1000);
        resp = new StatsCfgResp();
    } else {
        resp = new StatsCfgErrResp();
    }

    resp->set_context(context());
    resp->Response();
    return;
}

void GetStatsInterval::HandleRequest() const {
    StatsIntervalResp_InSeconds *resp = new StatsIntervalResp_InSeconds();
    resp->set_agent_stats_interval((AgentUve::GetInstance()->
                                    agent_stats_collector()->
                                    expiry_time())/1000);
    resp->set_flow_stats_interval((AgentUve::GetInstance()->
                                   flow_stats_collector()->
                                   expiry_time())/1000);
    resp->set_context(context());
    resp->Response();
    return;
}

