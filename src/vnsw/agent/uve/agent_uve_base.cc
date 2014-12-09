/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/cpuinfo.h>
#include <db/db.h>
#include <cmn/agent_cmn.h>
#include <oper/interface_common.h>
#include <oper/interface.h>

#include "vr_genetlink.h"
#include "nl_util.h"

#include <uve/agent_uve_base.h>
#include <uve/vn_uve_table_base.h>
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

AgentUveBase *AgentUveBase::singleton_;

AgentUveBase::AgentUveBase(Agent *agent, uint64_t intvl)
    : vn_uve_table_(new VnUveTableBase(agent)),
      vm_uve_table_(new VmUveTableBase(agent)),
      vrouter_uve_entry_(new VrouterUveEntryBase(agent)),
      agent_(agent), bandwidth_intvl_(intvl),
      vrouter_stats_collector_(new VrouterStatsCollector(
                                   *(agent->event_manager()->io_service()),
                                   this)) {
    singleton_ = this;
}

AgentUveBase::~AgentUveBase() {
}

void AgentUveBase::Shutdown() {
    vn_uve_table_.get()->Shutdown();
    vm_uve_table_.get()->Shutdown();
    vrouter_uve_entry_.get()->Shutdown();
    connection_state_manager_->Shutdown();
}

void AgentUveBase::Init() {
    std::string module_id(agent_->discovery_client_name());
    std::string instance_id(agent_->instance_id());
    EventManager *evm = agent_->event_manager();
    boost::asio::io_service &io = *evm->io_service();

    CpuLoadData::Init();
    connection_state_manager_ =
        ConnectionStateManager<NodeStatusUVE, NodeStatus>::
            GetInstance();
    agent_->set_connection_state(ConnectionState::GetInstance());
    connection_state_manager_->Init(io, agent_->params()->host_name(),
            module_id, instance_id,
            boost::bind(&AgentUveBase::VrouterAgentProcessState,
                        this, _1, _2, _3));
}

uint8_t AgentUveBase::ExpectedConnections(uint8_t &num_control_nodes,
                                          uint8_t &num_dns_servers) {
    uint8_t count = 0;
    AgentParam *cfg = agent_->params();

    /* If Discovery server is configured, increment the count by 1 for each
     * possible discovery service if we subscribe to that service. We subscribe
     * to a discovery service only if the service IP is not configured
     * explicitly */
    if (!agent_->discovery_server().empty()) {
        // Discovery:Collector
        if (cfg->collector_server_list().size() == 0) {
            count++;
        }
        // Discovery:dns-server
        if (!cfg->dns_server_1().to_ulong() &&
            !cfg->dns_server_2().to_ulong()) {
            count++;
        }
        // Discovery:xmpp-server
        if (!cfg->xmpp_server_1().to_ulong() &&
            !cfg->xmpp_server_2().to_ulong() ) {
            count++;
        }
    }
    for (int i = 0; i < MAX_XMPP_SERVERS; i++) {
        if (!agent_->controller_ifmap_xmpp_server(i).empty()) {
            num_control_nodes++;
            count++;
        }
        if (agent_->services() && !agent_->dns_server(i).empty()) {
            num_dns_servers++;
            count++;
        }
    }
    //Increment 1 for collector service
    if (!agent_->discovery_server().empty() ||
        cfg->collector_server_list().size() != 0) {
        count++;
    }
    return count;
}

void AgentUveBase::UpdateMessage(const ConnectionInfo &cinfo,
                             std::string &message) {
    if (message.empty()) {
        message = cinfo.get_type();
    } else {
        message += ", " + cinfo.get_type();
    }
    const std::string &name(cinfo.get_name());
    if (!name.empty()) {
        message += ":" + name;
    }
}

void AgentUveBase::VrouterAgentProcessState
    (const std::vector<ConnectionInfo> &cinfos,
     ProcessState::type &pstate, std::string &message) {
    size_t num_conns(cinfos.size());
    uint8_t num_control_nodes = 0, num_dns_servers = 0;
    uint8_t down_control_nodes = 0;
    uint8_t expected_conns = ExpectedConnections(num_control_nodes,
                                                 num_dns_servers);
    std::string cup(g_process_info_constants.ConnectionStatusNames.
        find(ConnectionStatus::UP)->second);
    bool is_cup = true;
    // Iterate to determine process connectivity status
    for (std::vector<ConnectionInfo>::const_iterator it = cinfos.begin();
         it != cinfos.end(); it++) {
        const ConnectionInfo &cinfo(*it);
        const std::string &conn_status(cinfo.get_status());
        if (conn_status != cup) {
            if (cinfo.get_name().compare(0, 13,
                agent_->xmpp_control_node_prefix()) == 0) {
                down_control_nodes++;
                is_cup = false;
                UpdateMessage(cinfo, message);
                continue;
            } else if (cinfo.get_name().compare(0, 11,
                agent_->xmpp_dns_server_prefix()) == 0) {
                is_cup = false;
                UpdateMessage(cinfo, message);
                continue;
            } else {
                is_cup = false;
                UpdateMessage(cinfo, message);
                continue;
            }
        }
    }
    if ((num_control_nodes == 0) || (num_control_nodes == down_control_nodes)) {
        pstate = ProcessState::NON_FUNCTIONAL;
    } else {
        pstate = ProcessState::FUNCTIONAL;
    }
    if (num_conns != expected_conns) {
        message = "Number of connections:" + integerToString(num_conns) +
            ", Expected: " + integerToString(expected_conns);
        return;
    }
    if (!is_cup) {
        message += " connection down";
    }
    return;
}

void AgentUveBase::RegisterDBClients() {
    vn_uve_table_.get()->RegisterDBClients();
    vm_uve_table_.get()->RegisterDBClients();
    vrouter_uve_entry_.get()->RegisterDBClients();
}

