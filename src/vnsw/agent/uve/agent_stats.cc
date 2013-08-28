/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/if_tun.h>

#include <db/db.h>
#include <cmn/agent_cmn.h>

#include <oper/interface.h>
#include <oper/mirror_table.h>

#include "vr_genetlink.h"
#include "vr_interface.h"
#include "vr_types.h"
#include "nl_util.h"

#include <uve/stats_collector.h>
#include <uve/agent_stats.h>
#include <uve/uve_init.h>
#include <uve/uve_client.h>

void AgentStatsCollector::SendIntfBulkGet() {
    vr_interface_req encoder;
    AgentStatsSandeshContext *ctx = AgentUve::GetIntfStatsSandeshContext();

    encoder.set_h_op(sandesh_op::DUMP);
    encoder.set_vifr_context(0);
    encoder.set_vifr_marker(ctx->GetMarker());
    SendRequest(encoder, IntfStatsType);
}

void AgentStatsCollector::SendVrfStatsBulkGet() {
    vr_vrf_stats_req encoder;
    AgentStatsSandeshContext *ctx = AgentUve::GetVrfStatsSandeshContext();

    encoder.set_h_op(sandesh_op::DUMP);
    encoder.set_vsr_rid(0);
    encoder.set_vsr_family(AF_INET);
    encoder.set_vsr_type(RT_UCAST);
    encoder.set_vsr_marker(ctx->GetMarker());
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
    int ret;
    uint8_t *buf;
    uint32_t buf_len;
    struct nl_client cl;

    nl_init_generic_client_req(&cl, KSyncSock::GetNetlinkFamilyId());

    if ((ret = nl_build_header(&cl, &buf, &buf_len)) < 0) {
        LOG(DEBUG, "Error creating if message. Error : " << ret);
        return false;
    }
    encode_len = encoder.WriteBinary(buf, buf_len, &error);
    nl_update_header(&cl, encode_len);

    SendAsync(cl.cl_buf, cl.cl_msg_len, type);
    return true;
}

void AgentStatsCollector::SendAsync(char* buf, uint32_t buf_len, StatsType type) {
    KSyncSock   *sock = KSyncSock::Get(0);
    uint32_t seq = sock->AllocSeqNo();

    IoContext *ioc = NULL;
    switch (type) {
        case IntfStatsType:
            ioc = new IntfStatsIoContext(buf_len, buf, seq,
                                         AgentUve::GetIntfStatsSandeshContext(), 
                                         IoContext::UVE_Q_ID);
            break;
       case VrfStatsType:
            ioc = new VrfStatsIoContext(buf_len, buf, seq,
                                         AgentUve::GetVrfStatsSandeshContext(), 
                                         IoContext::UVE_Q_ID);
            break;
       case DropStatsType:
            ioc = new DropStatsIoContext(buf_len, buf, seq,
                                         AgentUve::GetDropStatsSandeshContext(), 
                                         IoContext::UVE_Q_ID);
            break;
       default:
            break;
    }
    if (ioc) {
        sock->GenericSend(buf_len, buf, ioc); 
    }
}

void vr_interface_req::Process(SandeshContext *context) {
     AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
     ioc->IfMsgHandler(this);
}

void AgentStatsCollector::AddIfStatsEntry(const Interface *intf) {
    IntfToIfStatsTree::iterator it;
    it = if_stats_tree_.find(intf);
    if (it == if_stats_tree_.end()) {
        AgentStatsCollector::IfStats stats;
        stats.name = intf->GetName();
        if_stats_tree_.insert(IfStatsPair(intf, stats));
    }
}

void AgentStatsCollector::DelIfStatsEntry(const Interface *intf) {
    IntfToIfStatsTree::iterator it;
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
    it = vrf_stats_tree_.find(vrf->GetVrfId());
    if (it == vrf_stats_tree_.end()) {
        AgentStatsCollector::VrfStats stats;
        stats.name = vrf->GetName();
        vrf_stats_tree_.insert(VrfStatsPair(vrf->GetVrfId(), stats));
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
    it = vrf_stats_tree_.find(vrf->GetVrfId());
    if (it != vrf_stats_tree_.end()) {
        AgentStatsCollector::VrfStats *stats = &it->second;
        stats->prev_discards = stats->k_discards;
        stats->prev_resolves = stats->k_resolves;
        stats->prev_receives = stats->k_receives;
        stats->prev_tunnels = stats->k_tunnels;
        stats->prev_composites = stats->k_composites;
        stats->prev_encaps = stats->k_encaps;
    }
}

bool AgentStatsCollector::Run() {
    SendIntfBulkGet();
    SendVrfStatsBulkGet();
    SendDropStatsBulkGet();
    return true;
}

void AgentStatsCollector::SendStats() {
    GetUveClient()->SendVnStats();
    GetUveClient()->SendVmStats();
}

AgentStatsCollector::IfStats *AgentStatsCollector::GetIfStats(const Interface *intf) {
    IntfToIfStatsTree::iterator it;

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

void IntfStatsIoContext::Handler() {
    AgentStatsCollector *collector = AgentUve::GetStatsCollector();
    collector->run_counter_++;
    AgentUve::GetStatsCollector()->SendStats();
    /* Reset the marker for query during next timer interval, if there is
     * no additional records for the current query */
    AgentStatsSandeshContext *ctx = AgentUve::GetIntfStatsSandeshContext();
    if (!ctx->MoreData()) {
        ctx->SetMarker(-1);
    }
}

void IntfStatsIoContext::ErrorHandler(int err) {
    LOG(ERROR, "Error reading Interface Stats. Error <" << err << ": "
        << strerror(err) << ": Sequence No : " << GetSeqno());
}

void VrfStatsIoContext::Handler() {
    GetUveClient()->SendVnStats();
    /* Reset the marker for query during next timer interval, if there is
     * no additional records for the current query */
    AgentStatsSandeshContext *ctx = AgentUve::GetVrfStatsSandeshContext();
    if (!ctx->MoreData()) {
        ctx->SetMarker(-1);
    }
}

void VrfStatsIoContext::ErrorHandler(int err) {
    LOG(ERROR, "Error reading Vrf Stats. Error <" << err << ": "
        << strerror(err) << ": Sequence No : " << GetSeqno());
}

void DropStatsIoContext::Handler() {
}

void DropStatsIoContext::ErrorHandler(int err) {
    LOG(ERROR, "Error reading Drop Stats. Error <" << err << ": "
        << strerror(err) << ": Sequence No : " << GetSeqno());
}

int AgentStatsSandeshContext::VrResponseMsgHandler(vr_response *r) {
    int code = r->get_resp_code();
   
    SetResponseCode(code);
    if (code > 0) {
       /* Positive value indicates the number of records returned in the 
        * response from Kernel. Kernel response includes vr_response along
        * with actual response.
        */
        return 0;
    }
    
    if (code < 0) {
        LOG(ERROR, "Error: " << strerror(-code));
        return -code;
    }

    return 0;
}

void AgentStatsSandeshContext::IfMsgHandler(vr_interface_req *req) {
    AgentStatsCollector *collector = AgentUve::GetStatsCollector();
    SetMarker(req->get_vifr_idx());
    const Interface *intf = InterfaceTable::FindInterface(req->get_vifr_idx());
    if (intf == NULL) {
        LOG(DEBUG, "Invalid interface index <" << req->get_vifr_idx() << ">");
        return;
     }
 
    AgentStatsCollector::IfStats *stats = collector->GetIfStats(intf);
    if (!stats) {
        LOG(DEBUG, "Interface not present in stats tree <" << intf->GetName() << ">");
        return;
    }
    if (intf->GetType() == Interface::VMPORT) {
        AgentStats::IncrInPkts(req->get_vifr_ipackets() - stats->in_pkts);
        AgentStats::IncrInBytes(req->get_vifr_ibytes() - stats->in_bytes);
        AgentStats::IncrOutPkts(req->get_vifr_opackets() - stats->out_pkts);
        AgentStats::IncrOutBytes(req->get_vifr_obytes() - stats->out_bytes);
    }

    stats->in_pkts = req->get_vifr_ipackets();
    stats->in_bytes = req->get_vifr_ibytes();
    stats->out_pkts = req->get_vifr_opackets();
    stats->out_bytes = req->get_vifr_obytes();
    stats->speed = req->get_vifr_speed();
    stats->duplexity = req->get_vifr_duplex();
}

void AgentStatsSandeshContext::VrfStatsMsgHandler(vr_vrf_stats_req *req) {
    AgentStatsCollector *collector = AgentUve::GetStatsCollector();
    SetMarker(req->get_vsr_vrf());
    bool vrf_present = true;
    const VrfEntry *vrf = Agent::GetVrfTable()->FindVrfFromId(req->get_vsr_vrf());
    if (vrf == NULL) {
        if (req->get_vsr_vrf() != AgentStatsCollector::GetNamelessVrfId()) {
            vrf_present = false;
        }
        return;
     }
 
    AgentStatsCollector::VrfStats *stats = collector->GetVrfStats(req->get_vsr_vrf());
    if (!stats) {
        LOG(DEBUG, "Vrf not present in stats tree <" << vrf->GetName() << ">");
        return;
    }
    if (!vrf_present) {
        stats->prev_discards = req->get_vsr_discards();
        stats->prev_resolves = req->get_vsr_resolves();
        stats->prev_receives = req->get_vsr_receives();
        stats->prev_tunnels = req->get_vsr_tunnels();
        stats->prev_composites = req->get_vsr_composites();
        stats->prev_encaps = req->get_vsr_encaps();
    } else {
        stats->discards = req->get_vsr_discards() - stats->prev_discards;
        stats->resolves = req->get_vsr_resolves() - stats->prev_resolves;
        stats->receives = req->get_vsr_receives() - stats->prev_receives;
        stats->tunnels = req->get_vsr_tunnels() - stats->prev_tunnels;
        stats->composites = req->get_vsr_composites() - stats->prev_composites;
        stats->encaps = req->get_vsr_encaps() - stats->prev_encaps;
        
        /* Update the last read values from Kernel in the following fields.
         * This will be used to update prev_* fields on receiving vrf delete
         * notification
         */
        if (req->get_vsr_vrf() != AgentStatsCollector::GetNamelessVrfId()) {
            stats->k_discards = req->get_vsr_discards();
            stats->k_resolves = req->get_vsr_resolves();
            stats->k_receives = req->get_vsr_receives();
            stats->k_tunnels = req->get_vsr_tunnels();
            stats->k_composites = req->get_vsr_composites();
            stats->k_encaps = req->get_vsr_encaps();
        }

    }
}

void AgentStatsSandeshContext::DropStatsMsgHandler(vr_drop_stats_req *req) {
    AgentStatsCollector *collector = AgentUve::GetStatsCollector();
    collector->SetDropStats(*req);
}
