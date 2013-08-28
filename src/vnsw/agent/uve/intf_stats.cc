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
#include <uve/intf_stats.h>
#include <uve/uve_init.h>
#include <uve/uve_client.h>

bool IntfStatsCollector::SendIntfBulkGet() {
    vr_interface_req encoder;
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
    IntfStatsSandeshContext *ctx = AgentUve::GetIntfStatsSandeshContext();

    encoder.set_h_op(sandesh_op::DUMP);
    encoder.set_vifr_context(0);
    encoder.set_vifr_marker(ctx->GetMarker());
    encode_len = encoder.WriteBinary(buf, buf_len, &error);
    nl_update_header(&cl, encode_len);

    SendAsync(cl.cl_buf, cl.cl_msg_len);
    return true;
}

void IntfStatsCollector::SendAsync(char* buf, uint32_t buf_len) {
    KSyncSock   *sock = KSyncSock::Get(0);
    uint32_t seq = sock->AllocSeqNo();

    IntfStatsIoContext *ioc = new IntfStatsIoContext(buf_len, buf, seq,
                                       AgentUve::GetIntfStatsSandeshContext(), IoContext::UVE_Q_ID);
    sock->GenericSend(buf_len, buf, ioc); 
}

void vr_interface_req::Process(SandeshContext *context) {
     AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
     ioc->IfMsgHandler(this);
}

void IntfStatsCollector::AddStatsEntry(const Interface *intf) {
    UuidToStatsTree::iterator it;
    it = uuid_stats_tree_.find(intf);
    if (it == uuid_stats_tree_.end()) {
        IntfStatsCollector::Stats stats;
        stats.name = intf->GetName();
        uuid_stats_tree_.insert(UuidStatsPair(intf, stats));
    }
}

void IntfStatsCollector::DelStatsEntry(const Interface *intf) {
    UuidToStatsTree::iterator it;
    it = uuid_stats_tree_.find(intf);
    if (it != uuid_stats_tree_.end()) {
        uuid_stats_tree_.erase(it);
    }
}

bool IntfStatsCollector::Run() {
    SendIntfBulkGet();
    return true;
}

void IntfStatsCollector::SendStats() {
    GetUveClient()->SendVnStats();
    GetUveClient()->SendVmStats();
}

IntfStatsCollector::Stats *IntfStatsCollector::GetStats(const Interface *intf) {
    UuidToStatsTree::iterator it;

    it = uuid_stats_tree_.find(intf);
    if (it == uuid_stats_tree_.end()) {
        return NULL;
    }

    return &it->second;
}

void IntfStatsIoContext::Handler() {
    IntfStatsCollector *collector = AgentUve::GetStatsCollector();
    collector->run_counter_++;
    AgentUve::GetStatsCollector()->SendStats();
    /* Reset the marker for query during next timer interval, if there is
     * no additional records for the current query */
    IntfStatsSandeshContext *ctx = AgentUve::GetIntfStatsSandeshContext();
    if (!ctx->MoreData()) {
        ctx->SetMarker(-1);
    }
}

void IntfStatsIoContext::ErrorHandler(int err) {
    LOG(ERROR, "Error reading Interface Stats. Error <" << err << ": "
        << strerror(err) << ": Sequence No : " << GetSeqno());
}

int IntfStatsSandeshContext::VrResponseMsgHandler(vr_response *r) {
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

void IntfStatsSandeshContext::IfMsgHandler(vr_interface_req *req) {
    IntfStatsCollector *collector = AgentUve::GetStatsCollector();
    SetMarker(req->get_vifr_idx());
    const Interface *intf = InterfaceTable::FindInterface(req->get_vifr_idx());
    if (intf == NULL) {
        LOG(DEBUG, "Invalid interface index <" << req->get_vifr_idx() << ">");
        return;
     }
 
    IntfStatsCollector::Stats *stats = collector->GetStats(intf);
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
