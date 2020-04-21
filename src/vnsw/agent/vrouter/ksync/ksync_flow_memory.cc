/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/socket.h>
#if defined(__linux__)
#include <linux/netlink.h>
#elif defined(__FreeBSD__)
#include "vr_os.h"
#endif
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <asm/types.h>
#include <boost/asio.hpp>

#include <base/timer.h>
#include <base/task_trigger.h>
#include <base/address_util.h>
#include <cmn/agent_cmn.h>
#include <services/services_init.h>
#include <uve/stats_collector.h>
#include <services/icmp_error_proto.h>
#include <pkt/flow_proto.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>
#include <ksync/ksync_sock_user.h>
#include <vrouter/flow_stats/flow_stats_collector.h>

#include <vr_types.h>
#include <nl_util.h>
#include <vr_flow.h>
#include <vr_genetlink.h>

#include "ksync_init.h"
#include "ksync_flow_memory.h"
#include "sandesh_ksync.h"

using namespace boost::asio::ip;
static const int kTestFlowTableSize = 131072 * sizeof(vr_flow_entry);

KSyncFlowMemory::KSyncFlowMemory(KSync *ksync, uint32_t minor_id) :
    KSyncMemory(ksync, minor_id) {
    table_path_ = FLOW_TABLE_DEV;
    hold_flow_counter_ = 0;
}

void KSyncFlowMemory::Init() {
    IcmpErrorProto *proto = ksync_->agent()->services()->icmp_error_proto();
    proto->Register(boost::bind(&KSyncFlowMemory::GetFlowKey, this, _1, _2, _3));

    KSyncMemory::Init();
}

int KSyncFlowMemory::EncodeReq(struct nl_client *cl, uint32_t attr_len) {
    int encode_len, error;

    vr_flow_table_data info;
    info.set_ftable_op(flow_op::FLOW_TABLE_GET);
    info.set_ftable_size(0);
    info.set_ftable_dev(0);
    info.set_ftable_file_path("");
    info.set_ftable_processed(0);
    info.set_ftable_hold_oflows(0);
    info.set_ftable_added(0);
    info.set_ftable_cpus(0);
    info.set_ftable_created(0);
    info.set_ftable_oflow_entries(0);
    encode_len = info.WriteBinary(nl_get_buf_ptr(cl) + attr_len,
                                  nl_get_buf_len(cl), &error);
    return encode_len;
}

int KSyncFlowMemory::get_entry_size() {
    return sizeof(vr_flow_entry);
}

void KSyncFlowMemory::SetTableSize() {
    ksync_->agent()->set_flow_table_size(table_entries_count_);
    flow_table_ = static_cast<vr_flow_entry *>(table_);
}

void KSyncFlowMemory::CreateProtoAuditEntry(uint32_t idx, uint8_t gen_id) {
    const vr_flow_entry *ventry = GetKernelFlowEntry(idx, false);
    // Audit and remove  entry if its still in HOLD state
    if (ventry && ventry->fe_gen_id == gen_id &&
        ventry->fe_action == VR_FLOW_ACTION_HOLD) {
        IpAddress sip, dip;
        VrFlowToIp(ventry, &sip, &dip);
        FlowKey key(ventry->fe_key.flow_nh_id, sip, dip,
                ventry->fe_key.flow_proto,
                ntohs(ventry->fe_key.flow_sport),
                ntohs(ventry->fe_key.flow_dport));

        FlowProto *proto = ksync_->agent()->pkt()->get_flow_proto();
        proto->CreateAuditEntry(key, idx, gen_id);
    }
}

void KSyncFlowMemory::DecrementHoldFlowCounter() {
    hold_flow_counter_--;
    return;
}

void KSyncFlowMemory::IncrementHoldFlowCounter() {
    hold_flow_counter_++;
    return;
}

void KSyncFlowMemory::UpdateAgentHoldFlowCounter() {
    ksync_->agent()->stats()->update_hold_flow_count(hold_flow_counter_);
    hold_flow_counter_ = 0;
    return;
}

bool KSyncFlowMemory::IsInactiveEntry(uint32_t audit_idx, uint8_t &gen_id) {
    const vr_flow_entry *vflow_entry =
        GetKernelFlowEntry(audit_idx, false);
    if (vflow_entry && vflow_entry->fe_action == VR_FLOW_ACTION_HOLD) {
        gen_id = vflow_entry->fe_gen_id;
        return true;
    }
    return false;
}

void KSyncFlowMemory::VrFlowToIp(const vr_flow_entry *kflow, IpAddress *sip,
                                 IpAddress *dip) {
    if (kflow->fe_key.flow_family == AF_INET) {
        *sip = Ip4Address(ntohl(kflow->fe_key.key_u.ip4_key.ip4_sip));
        *dip = Ip4Address(ntohl(kflow->fe_key.key_u.ip4_key.ip4_dip));
    } else {
        const unsigned char *k_sip = kflow->fe_key.key_u.ip6_key.ip6_sip;
        const unsigned char *k_dip = kflow->fe_key.key_u.ip6_key.ip6_dip;
        Ip6Address::bytes_type sbytes;
        Ip6Address::bytes_type dbytes;
        for (int i = 0; i < 16; i++) {
            sbytes[i] = k_sip[i];
            dbytes[i] = k_dip[i];
        }
        *sip = Ip6Address(sbytes);
        *dip = Ip6Address(dbytes);
    }
}

void KSyncFlowMemory::KFlow2FlowKey(const vr_flow_entry *kflow,
                                    FlowKey *key) const {
    key->nh = kflow->fe_key.flow4_nh_id;
    Address::Family family = (kflow->fe_key.flow_family == AF_INET)?
                              Address::INET : Address::INET6;
    VrFlowToIp(kflow, &key->src_addr, &key->dst_addr);
    key->src_port = ntohs(kflow->fe_key.flow4_sport);
    key->dst_port = ntohs(kflow->fe_key.flow4_dport);
    key->protocol = kflow->fe_key.flow4_proto;
    key->family = family;
}

const vr_flow_entry *KSyncFlowMemory::GetValidKFlowEntry(const FlowKey &key,
                                                         uint32_t idx,
                                                         uint8_t gen_id) const {
    const vr_flow_entry *kflow = GetKernelFlowEntry(idx, false);
    if (!kflow) {
        return NULL;
    }
    if (key.protocol == IPPROTO_TCP) {
        FlowKey rhs;
        KFlow2FlowKey(kflow, &rhs);
        if (!key.IsEqual(rhs)) {
            return NULL;
        }
        if (kflow->fe_gen_id != gen_id) {
            return NULL;
        }
    }
    return kflow;
}

const vr_flow_entry *KSyncFlowMemory::GetKernelFlowEntry
    (uint32_t idx, bool ignore_active_status) const {
    if (idx == FlowEntry::kInvalidFlowHandle) {
        return NULL;
    }

    if (idx >= table_entries_count_) {
        /* if index is outside the range of flow table entries return NULL */
        return NULL;
    }

    if (ignore_active_status) {
        return &flow_table_[idx];
    }

    if (flow_table_[idx].fe_flags & VR_FLOW_FLAG_ACTIVE) {
        return &flow_table_[idx];
    }
    return NULL;
}

bool KSyncFlowMemory::GetFlowKey(uint32_t index, FlowKey *key, bool *is_nat_flow) {
    const vr_flow_entry *kflow = GetKernelFlowEntry(index, false);
    if (!kflow) {
        return false;
    }
    key->nh = kflow->fe_key.flow4_nh_id;
    Address::Family family = (kflow->fe_key.flow_family == AF_INET)?
                              Address::INET : Address::INET6;
    VrFlowToIp(kflow, &key->src_addr, &key->dst_addr);
    key->src_port = ntohs(kflow->fe_key.flow4_sport);
    key->dst_port = ntohs(kflow->fe_key.flow4_dport);
    key->protocol = kflow->fe_key.flow4_proto;
    key->family = family;
    if (kflow->fe_action == VR_FLOW_ACTION_NAT) {
        *is_nat_flow = true;
    } else {
        *is_nat_flow = false;
    }
    return true;
}

bool KSyncFlowMemory::IsEvictionMarked(const vr_flow_entry *entry,
                                       uint16_t flags) const {
    if (!entry) {
        return false;
    }
    if (flags & VR_FLOW_FLAG_EVICTED) {
        return true;
    }
    return false;
}

const vr_flow_entry *KSyncFlowMemory::GetKFlowStats(const FlowKey &key,
                                                    uint32_t idx,
                                                    uint8_t gen_id,
                                                    vr_flow_stats *stat) const {
    const vr_flow_entry *kflow = GetValidKFlowEntry(key, idx, gen_id);
    if (!kflow) {
        return NULL;
    }
    *stat = kflow->fe_stats;
    kflow = GetValidKFlowEntry(key, idx, gen_id);
    return kflow;
}

void KSyncFlowMemory::ReadFlowInfo(const vr_flow_entry *kflow,
                                   vr_flow_stats *stat, KFlowData *info) const {
    *stat = kflow->fe_stats;
    info->underlay_src_port = kflow->fe_udp_src_port;
    info->tcp_flags = kflow->fe_tcp_flags;
    info->flags = kflow->fe_flags;
}

const vr_flow_entry *KSyncFlowMemory::GetKFlowStatsAndInfo(const FlowKey &key,
                                                           uint32_t idx,
                                                           uint8_t gen_id,
                                                           vr_flow_stats *stats,
                                                           KFlowData *info)
    const {
    const vr_flow_entry *kflow = GetKernelFlowEntry(idx, false);
    if (!kflow) {
        return NULL;
    }
    if (key.protocol == IPPROTO_TCP) {
        FlowKey rhs;
        KFlow2FlowKey(kflow, &rhs);
        if (!key.IsEqual(rhs)) {
            return NULL;
        }

        ReadFlowInfo(kflow, stats, info);

        if (kflow->fe_gen_id != gen_id) {
            return NULL;
        }
    } else {
        ReadFlowInfo(kflow, stats, info);
    }
    return kflow;
}

void KSyncFlowMemory::InitTest() {
    table_ = KSyncSockTypeMap::FlowMmapAlloc(kTestFlowTableSize);
    memset(table_, 0, kTestFlowTableSize);
    table_entries_count_ = kTestFlowTableSize / get_entry_size();
    audit_yield_ = table_entries_count_;
    audit_timeout_ = 100 * 1000; // timout immediately.
    SetTableSize();
}

void KSyncFlowMemory::Shutdown() {
    KSyncSockTypeMap::FlowMmapFree();
}

void vr_flow_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->FlowMsgHandler(this);
}

void vr_flow_response::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->FlowResponseHandler(this);
}

void vr_flow_table_data::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->FlowTableInfoHandler(this);
}
