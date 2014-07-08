/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <db/db.h>
#include <cmn/agent_cmn.h>

#include <oper/interface_common.h>
#include <oper/interface.h>
#include <oper/mirror_table.h>

#include "vr_genetlink.h"
#include "vr_interface.h"
#include "vr_types.h"
#include "nl_util.h"

#include <uve/stats_collector.h>
#include <uve/agent_stats_collector.h>
#include <uve/vn_uve_table.h>
#include <uve/vm_uve_table.h>
#include <cmn/agent_param.h>
#include <uve/interface_stats_io_context.h>
#include <uve/vrf_stats_io_context.h>
#include <uve/drop_stats_io_context.h>
#include <uve/agent_uve.h>

AgentStatsCollector::AgentStatsCollector
    (boost::asio::io_service &io, Agent* agent)
    : StatsCollector(TaskScheduler::GetInstance()->
                     GetTaskId("Agent::StatsCollector"), 
                     StatsCollector::AgentStatsCollector, 
                     io, agent->params()->agent_stats_interval(), 
                     "Agent Stats collector"), 
    vrf_listener_id_(DBTableBase::kInvalidId), 
    intf_listener_id_(DBTableBase::kInvalidId), agent_(agent) {
    AddNamelessVrfStatsEntry();
    intf_stats_sandesh_ctx_.reset(new AgentStatsSandeshContext(this));
    vrf_stats_sandesh_ctx_.reset( new AgentStatsSandeshContext(this));
    drop_stats_sandesh_ctx_.reset(new AgentStatsSandeshContext(this));
}

AgentStatsCollector::~AgentStatsCollector() {
}

void AgentStatsCollector::SendInterfaceBulkGet() {
    vr_interface_req encoder;

    encoder.set_h_op(sandesh_op::DUMP);
    encoder.set_vifr_context(0);
    encoder.set_vifr_marker(intf_stats_sandesh_ctx_.get()->marker_id());
    SendRequest(encoder, InterfaceStatsType);
}

void AgentStatsCollector::SendVrfStatsBulkGet() {
    vr_vrf_stats_req encoder;

    encoder.set_h_op(sandesh_op::DUMP);
    encoder.set_vsr_rid(0);
    encoder.set_vsr_family(AF_INET);
    encoder.set_vsr_type(RT_UCAST);
    encoder.set_vsr_marker(vrf_stats_sandesh_ctx_.get()->marker_id());
    SendRequest(encoder, VrfStatsType);
}

void AgentStatsCollector::SendDropStatsBulkGet() {
    vr_drop_stats_req encoder;

    encoder.set_h_op(sandesh_op::GET);
    encoder.set_vds_rid(0);
    SendRequest(encoder, DropStatsType);
}

bool AgentStatsCollector::SendRequest(Sandesh &encoder, StatsType type) {
    int encode_len;
    int error;
    uint8_t *buf = (uint8_t *)malloc(KSYNC_DEFAULT_MSG_SIZE);

    encode_len = encoder.WriteBinary(buf, KSYNC_DEFAULT_MSG_SIZE, &error);
    SendAsync((char*)buf, encode_len, type);

    return true;
}

IoContext *AgentStatsCollector::AllocateIoContext(char* buf, uint32_t buf_len,
                                                  StatsType type, uint32_t seq) {
    switch (type) {
        case InterfaceStatsType:
            return (new InterfaceStatsIoContext(buf_len, buf, seq, 
                                         intf_stats_sandesh_ctx_.get(), 
                                         IoContext::UVE_Q_ID));
            break;
       case VrfStatsType:
            return (new VrfStatsIoContext(buf_len, buf, seq, 
                                        vrf_stats_sandesh_ctx_.get(), 
                                        IoContext::UVE_Q_ID));
            break;
       case DropStatsType:
            return (new DropStatsIoContext(buf_len, buf, seq, 
                                         drop_stats_sandesh_ctx_.get(), 
                                         IoContext::UVE_Q_ID));
            break;
       default:
            return NULL;
    }
}

void AgentStatsCollector::SendAsync(char* buf, uint32_t buf_len, 
                                    StatsType type) {
    KSyncSock   *sock = KSyncSock::Get(0);
    uint32_t seq = sock->AllocSeqNo(true);

    IoContext *ioc = AllocateIoContext(buf, buf_len, type, seq);
    if (ioc) {
        sock->GenericSend(ioc); 
    }
}

void vr_interface_req::Process(SandeshContext *context) {
     AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
     ioc->IfMsgHandler(this);
}

void AgentStatsCollector::AddInterfaceStatsEntry(const Interface *intf) {
    InterfaceStatsTree::iterator it;
    it = if_stats_tree_.find(intf);
    if (it == if_stats_tree_.end()) {
        AgentStatsCollector::InterfaceStats stats;
        stats.name = intf->name();
        if_stats_tree_.insert(InterfaceStatsPair(intf, stats));
    }
}

void AgentStatsCollector::DelInterfaceStatsEntry(const Interface *intf) {
    InterfaceStatsTree::iterator it;
    it = if_stats_tree_.find(intf);
    if (it != if_stats_tree_.end()) {
        if_stats_tree_.erase(it);
    }
}

void AgentStatsCollector::AddNamelessVrfStatsEntry() {
    AgentStatsCollector::VrfStats stats;
    stats.name = GetNamelessVrf();
    vrf_stats_tree_.insert(VrfStatsPair(GetNamelessVrfId(), stats));
}

void AgentStatsCollector::AddUpdateVrfStatsEntry(const VrfEntry *vrf) {
    VrfIdToVrfStatsTree::iterator it;
    it = vrf_stats_tree_.find(vrf->vrf_id());
    if (it == vrf_stats_tree_.end()) {
        AgentStatsCollector::VrfStats stats;
        stats.name = vrf->GetName();
        vrf_stats_tree_.insert(VrfStatsPair(vrf->vrf_id(), stats));
    } else {
        /* Vrf could be deleted in agent oper DB but not in Kernel. To handle 
         * this case we maintain vrfstats object in AgentStatsCollector even
         * when vrf is absent in agent oper DB.  Since vrf could get deleted and
         * re-added we need to update the name in vrfstats object.
         */
        AgentStatsCollector::VrfStats *stats = &it->second;
        stats->name = vrf->GetName();
    }
}

void AgentStatsCollector::DelVrfStatsEntry(const VrfEntry *vrf) {
    VrfIdToVrfStatsTree::iterator it;
    it = vrf_stats_tree_.find(vrf->vrf_id());
    if (it != vrf_stats_tree_.end()) {
        AgentStatsCollector::VrfStats *stats = &it->second;
        stats->prev_discards = stats->k_discards;
        stats->prev_resolves = stats->k_resolves;
        stats->prev_receives = stats->k_receives;
        stats->prev_udp_mpls_tunnels = stats->k_udp_mpls_tunnels;
        stats->prev_udp_tunnels = stats->k_udp_tunnels;
        stats->prev_gre_mpls_tunnels = stats->k_gre_mpls_tunnels;
        stats->prev_fabric_composites = stats->k_fabric_composites;
        stats->prev_l2_mcast_composites = stats->k_l2_mcast_composites;
        stats->prev_l3_mcast_composites = stats->k_l3_mcast_composites;
        stats->prev_multi_proto_composites = stats->k_multi_proto_composites;
        stats->prev_ecmp_composites = stats->k_ecmp_composites;
        stats->prev_l2_encaps = stats->k_l2_encaps;
        stats->prev_encaps = stats->k_encaps;
    }
}

bool AgentStatsCollector::Run() {
    SendInterfaceBulkGet();
    SendVrfStatsBulkGet();
    SendDropStatsBulkGet();
    return true;
}

void AgentStatsCollector::SendStats() {
    agent_->uve()->vn_uve_table()->SendVnStats(false);
    agent_->uve()->vm_uve_table()->SendVmStats();
}

AgentStatsCollector::InterfaceStats *AgentStatsCollector::GetInterfaceStats
    (const Interface *intf) {
    InterfaceStatsTree::iterator it;

    it = if_stats_tree_.find(intf);
    if (it == if_stats_tree_.end()) {
        return NULL;
    }

    return &it->second;
}

AgentStatsCollector::VrfStats *AgentStatsCollector::GetVrfStats(int vrf_id) {
    VrfIdToVrfStatsTree::iterator it;
    it = vrf_stats_tree_.find(vrf_id);
    if (it == vrf_stats_tree_.end()) {
        return NULL;
    }

    return &it->second;
}

void AgentStatsCollector::InterfaceNotify(DBTablePartBase *part, 
                                          DBEntryBase *e) {
    const Interface *intf = static_cast<const Interface *>(e);
    bool set_state = false, reset_state = false;

    DBState *state = static_cast<DBState *>
                      (e->GetState(part->parent(), intf_listener_id_));
    switch(intf->type()) {
    case Interface::VM_INTERFACE:
        if (e->IsDeleted() || ((intf->ipv4_active() == false) &&
                                (intf->l2_active() == false))) {
            if (state) {
                reset_state = true;
            }
        } else {
            if (!state) {
                set_state = true;
            }
        }
        break;
    default:
        if (e->IsDeleted()) {
            if (state) {
                reset_state = true;
            }
        } else {
            if (!state) {
                set_state = true;
            }
        }
    }
    if (set_state) {
        state = new DBState();
        e->SetState(part->parent(), intf_listener_id_, state);
        AddInterfaceStatsEntry(intf);
    } else if (reset_state) {
        DelInterfaceStatsEntry(intf);
        delete state;
        e->ClearState(part->parent(), intf_listener_id_);
    }
    return;
}

void AgentStatsCollector::VrfNotify(DBTablePartBase *part, DBEntryBase *e) {
    const VrfEntry *vrf = static_cast<const VrfEntry *>(e);
    DBState *state = static_cast<DBState *>
                      (e->GetState(part->parent(), vrf_listener_id_));
    if (e->IsDeleted()) {
        if (state) {
            DelVrfStatsEntry(vrf);
            delete state;
            e->ClearState(part->parent(), vrf_listener_id_);
        }
    } else {
        if (!state) {
            state = new DBState();
            e->SetState(part->parent(), vrf_listener_id_, state);
        }
        AddUpdateVrfStatsEntry(vrf);
    }
}

void AgentStatsCollector::RegisterDBClients() {
    InterfaceTable *intf_table = agent_->interface_table();
    intf_listener_id_ = intf_table->Register
        (boost::bind(&AgentStatsCollector::InterfaceNotify, this, _1, _2));

    VrfTable *vrf_table = agent_->vrf_table();
    vrf_listener_id_ = vrf_table->Register
        (boost::bind(&AgentStatsCollector::VrfNotify, this, _1, _2));
}

void AgentStatsCollector::Shutdown(void) {
    agent_->vrf_table()->Unregister(vrf_listener_id_);
    agent_->interface_table()->Unregister(intf_listener_id_);
}

void AgentStatsCollector::InterfaceStats::UpdateStats
    (uint64_t in_b, uint64_t in_p, uint64_t out_b, uint64_t out_p) {
    in_bytes = in_b;
    in_pkts = in_p;
    out_bytes = out_b;
    out_pkts = out_p;
}

void AgentStatsCollector::InterfaceStats::UpdatePrevStats() {
    prev_in_bytes = in_bytes;
    prev_in_pkts = in_pkts;
    prev_out_bytes = out_bytes;
    prev_out_pkts = out_pkts;
}

void AgentStatsCollector::InterfaceStats::GetDiffStats
    (uint64_t *in_b, uint64_t *in_p, uint64_t *out_b, uint64_t *out_p) {
    *in_b = in_bytes - prev_in_bytes;
    *in_p = in_pkts - prev_in_pkts;
    *out_b = out_bytes - prev_out_bytes;
    *out_p = out_pkts - prev_out_pkts;
}

