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
#include <oper/global_vrouter.h>
#include <uve/vrouter_stats_collector.h>
#include <cmn/agent_stats.h>

using process::ConnectionInfo;
using process::ConnectionType;
using process::ProcessState;
using process::ConnectionStatus;
using process::ConnectionState;
using process::ConnectionStateManager;
using process::g_process_info_constants;

AgentUveBase *AgentUveBase::singleton_;

AgentUveBase::AgentUveBase(Agent *agent, uint64_t intvl,
                           uint32_t default_intvl, uint32_t incremental_intvl)
    : vn_uve_table_(NULL), vm_uve_table_(NULL), vrouter_uve_entry_(NULL),
      prouter_uve_table_(new ProuterUveTable(agent, default_intvl)),
      interface_uve_table_(NULL),
      default_interval_(default_intvl),
      incremental_interval_(incremental_intvl),
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
    prouter_uve_table_.get()->Shutdown();
    interface_uve_table_.get()->Shutdown();
    connection_state_manager_->Shutdown();
    vrouter_stats_collector_->Shutdown();
}

void AgentUveBase::Init() {
    std::string module_id(agent_->module_name());
    std::string instance_id(agent_->instance_id());
    EventManager *evm = agent_->event_manager();
    boost::asio::io_service &io = *evm->io_service();

    CpuLoadData::Init();
    connection_state_manager_ =
        ConnectionStateManager::
            GetInstance();
    agent_->set_connection_state(ConnectionState::GetInstance());
    connection_state_manager_->Init(io, agent_->agent_name(),
            module_id, instance_id,
            boost::bind(&AgentUveBase::VrouterAgentProcessState,
                        this, _1, _2, _3), "ObjectVRouter");
}

uint8_t AgentUveBase::ExpectedConnections(uint8_t &num_control_nodes,
                                          uint8_t &num_dns_servers) {
    uint8_t count = 0;
    AgentParam *cfg = agent_->params();

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
    if (cfg->collector_server_list().size() != 0) {
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

bool AgentUveBase::HasSelfConfiguration() const {
    if (!agent_ || !agent_->oper_db() || !agent_->oper_db()->global_vrouter()) {
        return false;
    }
    return agent_->oper_db()->global_vrouter()->configured();
}

void AgentUveBase::VrouterAgentProcessState
    (const std::vector<ConnectionInfo> &cinfos,
     ProcessState::type &pstate, std::string &message) {
    size_t num_conns = 0;
    uint8_t num_control_nodes = 0, num_dns_servers = 0;
    uint8_t down_control_nodes = 0;
    uint8_t expected_conns = ExpectedConnections(num_control_nodes,
                                                 num_dns_servers);
    std::string cup(g_process_info_constants.ConnectionStatusNames.
        find(ConnectionStatus::UP)->second);
    bool is_cup = true;
    bool is_tor_connected = false;
    string tor_type(g_process_info_constants.ConnectionTypeNames.
            find(ConnectionType::TOR)->second);
    // Iterate to determine process connectivity status
    for (std::vector<ConnectionInfo>::const_iterator it = cinfos.begin();
         it != cinfos.end(); it++) {
        const ConnectionInfo &cinfo(*it);
        const std::string &conn_status(cinfo.get_status());
        if (cinfo.get_type() == tor_type) {
            is_tor_connected = true;
            continue;
        }
        /* Don't consider ConnectionType::TOR type for counting connections.
         * contrail-tor-agent is not supposed to report as Non-Functional when
         * it is in backup mode, but contrail-tor-agent does not have a way to
         * figure out that it is in backup mode. Hence for contrail-tor-agent
         * (both active and backup modes) we don't consider connection to TOR
         * for reporting Node Status */
        num_conns++;
        if (conn_status != cup) {
            if (cinfo.get_name().compare(0, 13,
                agent_->xmpp_control_node_prefix()) == 0) {
                down_control_nodes++;
            }
            is_cup = false;
            UpdateMessage(cinfo, message);
        }
    }
    if ((num_control_nodes == 0) || (num_control_nodes == down_control_nodes)) {
        pstate = ProcessState::NON_FUNCTIONAL;
        if ((num_control_nodes == 0) && message.empty()) {
            message = "No control-nodes configured";
        }
    } else if (!is_tor_connected && agent_->tor_agent_enabled()) {
        // waiting for first TOR config to arrive
        pstate = ProcessState::NON_FUNCTIONAL;
        message += " No ToR Config";
    } else {
        pstate = ProcessState::FUNCTIONAL;
    }
    if (!is_cup) {
        message += " connection down";
    }
    if (!HasSelfConfiguration()) {
        // waiting for Global vrouter config
        pstate = ProcessState::NON_FUNCTIONAL;
        if (message.empty()) {
            message = "No Configuration for self";
        } else {
            message += ", No Configuration for self";
        }
    }
    for (int i = 0; i < MAX_XMPP_SERVERS; i++) {
        if (!agent_->controller_ifmap_xmpp_server(i).empty()) {
            if (agent_->stats()->xmpp_reconnects(i) >= 1) {
                break;
            }
        }
    }

    if (num_conns != expected_conns) {
        message += " Number of connections:" + integerToString(num_conns) +
            ", Expected: " + integerToString(expected_conns);
        return;
    }
    return;
}

void AgentUveBase::RegisterDBClients() {
    vn_uve_table_.get()->RegisterDBClients();
    vm_uve_table_.get()->RegisterDBClients();
    vrouter_uve_entry_.get()->RegisterDBClients();
    prouter_uve_table_.get()->RegisterDBClients();
    interface_uve_table_.get()->RegisterDBClients();
}

