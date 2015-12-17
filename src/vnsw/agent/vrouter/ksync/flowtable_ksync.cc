/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
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
#include <boost/asio/buffer.hpp>

#include <net/address_util.h>
#include <cmn/agent_cmn.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_types.h>
#include <vrouter/ksync/agent_ksync_types.h>
#include <vrouter/ksync/interface_ksync.h>
#include <vrouter/ksync/nexthop_ksync.h>
#include <vrouter/ksync/mirror_ksync.h>
#include <vrouter/ksync/flowtable_ksync.h>
#include <filter/traffic_action.h>
#include <vr_types.h>
#include <nl_util.h>
#include <vr_flow.h>
#include <vr_genetlink.h>
#include <ksync/ksync_sock_user.h>
#include "vnswif_listener.h"
#include <vrouter/ksync/ksync_init.h>

#include <pkt/flow_proto.h>
#include <oper/agent_types.h>
#include <services/services_init.h>
#include <services/icmp_error_proto.h>
#include <uve/stats_collector.h>

const uint32_t KSyncFlowEntryFreeList::kInitCount;
const uint32_t KSyncFlowEntryFreeList::kTestInitCount;
const uint32_t KSyncFlowEntryFreeList::kGrowSize;
const uint32_t KSyncFlowEntryFreeList::kMinThreshold;

using namespace boost::asio::ip;

static uint16_t GetDropReason(uint16_t dr) {
    switch (dr) {
    case FlowEntry::SHORT_UNAVIALABLE_INTERFACE:
        return VR_FLOW_DR_UNAVIALABLE_INTF;
    case FlowEntry::SHORT_IPV4_FWD_DIS:
        return VR_FLOW_DR_IPv4_FWD_DIS;
    case FlowEntry::SHORT_UNAVIALABLE_VRF:
        return VR_FLOW_DR_UNAVAILABLE_VRF;
    case FlowEntry::SHORT_NO_SRC_ROUTE:
        return VR_FLOW_DR_NO_SRC_ROUTE;
    case FlowEntry::SHORT_NO_DST_ROUTE:
        return VR_FLOW_DR_NO_DST_ROUTE;
    case FlowEntry::SHORT_AUDIT_ENTRY:
        return VR_FLOW_DR_AUDIT_ENTRY;
    case FlowEntry::SHORT_VRF_CHANGE:
        return VR_FLOW_DR_VRF_CHANGE;
    case FlowEntry::SHORT_NO_REVERSE_FLOW:
        return VR_FLOW_DR_NO_REVERSE_FLOW;
    case FlowEntry::SHORT_REVERSE_FLOW_CHANGE:
        return VR_FLOW_DR_REVERSE_FLOW_CHANGE;
    case FlowEntry::SHORT_NAT_CHANGE:
        return VR_FLOW_DR_NAT_CHANGE;
    case FlowEntry::SHORT_FLOW_LIMIT:
        return VR_FLOW_DR_FLOW_LIMIT;
    case FlowEntry::SHORT_LINKLOCAL_SRC_NAT:
        return VR_FLOW_DR_LINKLOCAL_SRC_NAT;
    case FlowEntry::DROP_POLICY:
        return VR_FLOW_DR_POLICY;
    case FlowEntry::DROP_OUT_POLICY:
        return VR_FLOW_DR_OUT_POLICY;
    case FlowEntry::DROP_SG:
        return VR_FLOW_DR_SG;
    case FlowEntry::DROP_OUT_SG:
        return VR_FLOW_DR_OUT_SG;
    case FlowEntry::DROP_REVERSE_SG:
        return VR_FLOW_DR_REVERSE_SG;
    case FlowEntry::DROP_REVERSE_OUT_SG:
        return VR_FLOW_DR_REVERSE_OUT_SG;
    default:
        break;
    }
    return VR_FLOW_DR_UNKNOWN;
}

FlowTableKSyncEntry::FlowTableKSyncEntry(FlowTableKSyncObject *obj) {
    Reset();
    ksync_obj_ = obj;
}

FlowTableKSyncEntry::FlowTableKSyncEntry(FlowTableKSyncObject *obj,
                                         FlowEntry *flow, uint32_t hash_id) {
    Reset();
    Reset(flow, hash_id);
    ksync_obj_ = obj;
}

FlowTableKSyncEntry::~FlowTableKSyncEntry() {
}

void FlowTableKSyncEntry::Reset() {
    KSyncEntry::Reset();
    flow_entry_ = NULL;
    hash_id_ = FlowEntry::kInvalidFlowHandle;
    old_reverse_flow_id_ = FlowEntry::kInvalidFlowHandle;
    old_action_ = 0;
    old_component_nh_idx_ = 0xFFFF;
    old_first_mirror_index_ = 0xFFFF;
    old_second_mirror_index_ = 0xFFFF;
    trap_flow_ = false;
    old_drop_reason_ = 0;
    ecmp_ = false;
    nh_ = NULL;
}

void FlowTableKSyncEntry::Reset(FlowEntry *flow, uint32_t hash_id) {
    flow_entry_ = flow;
    hash_id_ = hash_id;
}

KSyncObject *FlowTableKSyncEntry::GetObject() {
    return ksync_obj_;
}

void FlowTableKSyncEntry::SetPcapData(FlowEntryPtr fe, 
                                      std::vector<int8_t> &data) {
    data.clear();
    uint32_t addr = ksync_obj_->ksync()->agent()->router_id().to_ulong();
    data.push_back(FlowEntry::PCAP_CAPTURE_HOST);
    data.push_back(0x4);
    data.push_back(((addr >> 24) & 0xFF));
    data.push_back(((addr >> 16) & 0xFF));
    data.push_back(((addr >> 8) & 0xFF));
    data.push_back(((addr) & 0xFF));

    data.push_back(FlowEntry::PCAP_FLAGS);
    data.push_back(0x4);
    uint32_t action;
    action = fe->match_p().action_info.action;
    if (fe->is_flags_set(FlowEntry::IngressDir)) {
        // Set 31st bit for ingress
        action |= 0x40000000;
    }
    data.push_back((action >> 24) & 0xFF);
    data.push_back((action >> 16) & 0xFF);
    data.push_back((action >> 8) & 0xFF);
    data.push_back((action) & 0xFF);
    
    data.push_back(FlowEntry::PCAP_SOURCE_VN);
    data.push_back(fe->data().source_vn.size());
    data.insert(data.end(), fe->data().source_vn.begin(), 
                fe->data().source_vn.end());
    data.push_back(FlowEntry::PCAP_DEST_VN);
    data.push_back(fe->data().dest_vn.size());
    data.insert(data.end(), fe->data().dest_vn.begin(), fe->data().dest_vn.end());
    data.push_back(FlowEntry::PCAP_TLV_END);
    data.push_back(0x0);
}

int FlowTableKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_flow_req &req = ksync_obj_->flow_req();
    int encode_len;
    int error;
    uint16_t action = 0;
    uint16_t drop_reason = VR_FLOW_DR_UNKNOWN;

    if (flow_entry_->ksync_index_entry()->IsEvicted() == true) {
        return 0;
    }

    req.set_fr_op(flow_op::FLOW_SET);
    req.set_fr_rid(0);
    req.set_fr_index(hash_id_);
    const FlowKey *fe_key = &flow_entry_->key();
    req.set_fr_flow_ip(IpToVector(fe_key->src_addr, fe_key->dst_addr,
                                  flow_entry_->key().family));
    req.set_fr_flow_proto(fe_key->protocol);
    req.set_fr_flow_sport(htons(fe_key->src_port));
    req.set_fr_flow_dport(htons(fe_key->dst_port));
    req.set_fr_flow_nh_id(fe_key->nh);
    if (flow_entry_->key().family == Address::INET)
        req.set_fr_family(AF_INET);
    else
        req.set_fr_family(AF_INET6);
    req.set_fr_flow_vrf(flow_entry_->data().vrf);
    uint16_t flags = 0;

    if (op == sandesh_op::DELETE) {
        if (hash_id_ == FlowEntry::kInvalidFlowHandle) {
            return 0;
        }
        req.set_fr_flags(0);
    } else {
        FlowEntry *rev_flow = flow_entry_->reverse_flow_entry();
        if (rev_flow &&
            rev_flow->flow_handle() == FlowEntry::kInvalidFlowHandle) {
            return 0;
        }

        flags = VR_FLOW_FLAG_ACTIVE;
        uint32_t fe_action = flow_entry_->match_p().action_info.action;
        if ((fe_action) & (1 << TrafficAction::PASS)) {
            action = VR_FLOW_ACTION_FORWARD;
        } 
        
        if ((fe_action) & (1 << TrafficAction::DENY)) {
            action = VR_FLOW_ACTION_DROP;
            drop_reason = GetDropReason(flow_entry_->data().drop_reason);
        }

        if (action == VR_FLOW_ACTION_FORWARD &&
            flow_entry_->is_flags_set(FlowEntry::NatFlow)) {
            action = VR_FLOW_ACTION_NAT;
        }

        if (action == VR_FLOW_ACTION_NAT && 
            flow_entry_->reverse_flow_entry() == NULL) {
            action = VR_FLOW_ACTION_DROP;
        }
        
        if ((fe_action) & (1 << TrafficAction::MIRROR)) {
            flags |= VR_FLOW_FLAG_MIRROR;
            req.set_fr_mir_id(-1);
            req.set_fr_sec_mir_id(-1);
            if (flow_entry_->match_p().action_info.mirror_l.size() > 
                FlowEntry::kMaxMirrorsPerFlow) {
                FLOW_TRACE(Err, hash_id_,
                           "Don't support more than two mirrors/analyzers per "
                           "flow:" + integerToString
                           (flow_entry_->
                            data().match_p.action_info.mirror_l.size()));
            }
            // Lookup for fist and second mirror entries
            std::vector<MirrorActionSpec>::const_iterator it;
            it = flow_entry_->match_p().action_info.mirror_l.begin();
            MirrorKSyncObject* obj = ksync_obj_->ksync()->agent()->ksync()->
                                                          mirror_ksync_obj();
            uint16_t idx_1 = obj->GetIdx((*it).analyzer_name);
            req.set_fr_mir_id(idx_1);
            FLOW_TRACE(ModuleInfo, "Mirror index first: " + 
                       integerToString(idx_1));
            ++it;
            if (it != flow_entry_->match_p().action_info.mirror_l.end()) {
                uint16_t idx_2 = obj->GetIdx((*it).analyzer_name);
                if (idx_1 != idx_2) {
                    req.set_fr_sec_mir_id(idx_2);
                    FLOW_TRACE(ModuleInfo, "Mirror index second: " + 
                               integerToString(idx_2));
                } else {
                    FLOW_TRACE(Err, hash_id_, 
                               "Both Mirror indexes are same, hence didn't set "
                               "the second mirror dest.");
                }
            }
            req.set_fr_mir_vrf(flow_entry_->data().mirror_vrf); 
            req.set_fr_mir_sip(htonl(ksync_obj_->ksync()->agent()->
                                     router_id().to_ulong()));
            req.set_fr_mir_sport(htons(ksync_obj_->ksync()->agent()->
                                                            mirror_port()));
            std::vector<int8_t> pcap_data;
            SetPcapData(flow_entry_, pcap_data);
            req.set_fr_pcap_meta_data(pcap_data);
        }

        req.set_fr_ftable_size(0);
        req.set_fr_ecmp_nh_index(flow_entry_->data().component_nh_idx);

        if (action == VR_FLOW_ACTION_NAT) {
            FlowEntry *nat_flow = flow_entry_->reverse_flow_entry();
            const FlowKey *nat_key = &nat_flow->key();

            if (flow_entry_->key().src_addr != nat_key->dst_addr) {
                flags |= VR_FLOW_FLAG_SNAT;
            }
            if (flow_entry_->key().dst_addr != nat_key->src_addr) {
                flags |= VR_FLOW_FLAG_DNAT;
            }

            if (flow_entry_->key().protocol == IPPROTO_TCP || 
                flow_entry_->key().protocol == IPPROTO_UDP) {
                if (flow_entry_->key().src_port != nat_key->dst_port) {
                    flags |= VR_FLOW_FLAG_SPAT;
                }
                if (flow_entry_->key().dst_port != nat_key->src_port) {
                    flags |= VR_FLOW_FLAG_DPAT;
                }
            }
            if (nat_flow->is_flags_set(FlowEntry::LinkLocalBindLocalSrcPort)) {
                flags |= VR_FLOW_FLAG_LINK_LOCAL;
            }

            flags |= VR_FLOW_FLAG_VRFT;
            req.set_fr_flow_dvrf(flow_entry_->data().dest_vrf);
        }

        if (fe_action & (1 << TrafficAction::VRF_TRANSLATE)) {
            flags |= VR_FLOW_FLAG_VRFT;
            req.set_fr_flow_dvrf(flow_entry_->acl_assigned_vrf_index());
        }

        if (flow_entry_->is_flags_set(FlowEntry::Trap)) {
            flags |= VR_FLOW_FLAG_TRAP_ECMP;
            action = VR_FLOW_ACTION_HOLD;
        }

        if (enable_rpf_) {
            if (nh_) {
                const NHKSyncEntry *ksync_nh =
                    static_cast<const NHKSyncEntry *>(nh_.get());
                req.set_fr_src_nh_index(ksync_nh->nh_id());
            } else {
                req.set_fr_src_nh_index(NextHopTable::kRpfDiscardIndex);
            }
        } else {
            //Set to discard, vrouter ignores RPF check if
            //nexthop is set to discard
            req.set_fr_src_nh_index(0);
        }

        if (rev_flow) {
            flags |= VR_RFLOW_VALID;
            req.set_fr_rindex(rev_flow->flow_handle());
        }

        req.set_fr_flags(flags);
        req.set_fr_action(action);
        req.set_fr_drop_reason(drop_reason);
    }

    encode_len = req.WriteBinary((uint8_t *)buf, buf_len, &error);
    return encode_len;
}

bool FlowTableKSyncEntry::Sync() {
    bool changed = false;
    
    FlowEntry *rev_flow = flow_entry_->reverse_flow_entry();   
    if (rev_flow) {
        if (old_reverse_flow_id_ != rev_flow->flow_handle()) {
            old_reverse_flow_id_ = rev_flow->flow_handle();
            changed = true;
        }
    }

    if (flow_entry_->match_p().action_info.action != old_action_) {
        old_action_ = flow_entry_->match_p().action_info.action;
        changed = true;
    }

    if (flow_entry_->data().drop_reason != old_drop_reason_) {
        old_drop_reason_ = flow_entry_->data().drop_reason;
        changed = true;
    }
    if (flow_entry_->data().component_nh_idx != old_component_nh_idx_) {
        old_component_nh_idx_ = flow_entry_->data().component_nh_idx;
        changed = true;
    }

    MirrorKSyncObject* obj = ksync_obj_->ksync()->mirror_ksync_obj();
    // Lookup for fist and second mirror entries
    std::vector<MirrorActionSpec>::const_iterator it;
    it = flow_entry_->match_p().action_info.mirror_l.begin();
    if (it != flow_entry_->match_p().action_info.mirror_l.end()) { 
        uint16_t idx = obj->GetIdx((*it).analyzer_name);
        if (old_first_mirror_index_ != idx) {
            old_first_mirror_index_ = idx;
            changed = true;
        }
        ++it;
        if (it != flow_entry_->match_p().action_info.mirror_l.end()) {
            idx = obj->GetIdx((*it).analyzer_name);
            if (old_second_mirror_index_ != idx) {
                old_second_mirror_index_ = idx;
                changed = true;
            }
        }
    }

    //Trap reverse flow
    if (trap_flow_ != flow_entry_->is_flags_set(FlowEntry::Trap)) {
        trap_flow_ = flow_entry_->is_flags_set(FlowEntry::Trap);
        changed = true;
    }

    if (ecmp_ != flow_entry_->is_flags_set(FlowEntry::EcmpFlow)) {
        ecmp_ = flow_entry_->is_flags_set(FlowEntry::EcmpFlow);
        changed = true;
    }

    if (enable_rpf_ != flow_entry_->data().enable_rpf) {
        enable_rpf_ = flow_entry_->data().enable_rpf;
        changed = true;
    }

    if (flow_entry_->data().nh.get()) {
        NHKSyncObject *nh_object = ksync_obj_->ksync()->nh_ksync_obj();
        DBTableBase *table = nh_object->GetDBTable();
        NHKSyncEntry *nh;
        nh = static_cast<NHKSyncEntry *>(flow_entry_->data().nh.get()->
                GetState(table, nh_object->GetListenerId(table)));
        if (nh == NULL) {
            NHKSyncEntry tmp_nh(nh_object, flow_entry_->data().nh.get());
            nh = static_cast<NHKSyncEntry *>(nh_object->GetReference(&tmp_nh));
        }
        if (nh_ != nh) {
            nh_ = nh;
            changed = true;
        }
    }

    return changed;
}

KSyncEntry* FlowTableKSyncEntry::UnresolvedReference() {
    //Pick NH from flow entry
    //We should ideally pick it up from ksync entry once
    //Sync() api gets called before event notify, similar to
    //netlink DB entry
    if (flow_entry_->data().nh.get()) {
        NHKSyncObject *nh_object = ksync_obj_->ksync()->nh_ksync_obj();
        DBTableBase *table = nh_object->GetDBTable();
        NHKSyncEntry *nh;
        nh = static_cast<NHKSyncEntry *>(flow_entry_->data().nh.get()->
                GetState(table, nh_object->GetListenerId(table)));
        if (nh == NULL) {
            NHKSyncEntry tmp_nh(nh_object, flow_entry_->data().nh.get());
            nh = static_cast<NHKSyncEntry *>(nh_object->GetReference(&tmp_nh));
        }
        if (nh && !nh->IsResolved()) {
            return nh;
        }
    }
    if (flow_entry_->match_p().action_info.mirror_l.size()) {
        MirrorKSyncObject *mirror_object =
            ksync_obj_->ksync()->mirror_ksync_obj();
        std::vector<MirrorActionSpec>::const_iterator it;
        it = flow_entry_->match_p().action_info.mirror_l.begin();
        std::string analyzer1 = (*it).analyzer_name;
        MirrorKSyncEntry mksync1(mirror_object, analyzer1);
        MirrorKSyncEntry *mirror1 =
        static_cast<MirrorKSyncEntry *>(mirror_object->GetReference(&mksync1));
        if (mirror1 && !mirror1->IsResolved()) {
            return mirror1;
        }
        ++it;
        if (it != flow_entry_->match_p().action_info.mirror_l.end()) {
            std::string analyzer2 = (*it).analyzer_name;
            if (analyzer1 != analyzer2) {
                MirrorKSyncEntry mksync2(mirror_object, analyzer2);
                MirrorKSyncEntry *mirror2 = static_cast<MirrorKSyncEntry *>
                    (mirror_object->GetReference(&mksync2));
                if (mirror2 && !mirror2->IsResolved()) {
                    return mirror2;
                }
            }
        }
    }
    return NULL;
}

int FlowTableKSyncEntry::AddMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int FlowTableKSyncEntry::ChangeMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int FlowTableKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::DELETE, buf, buf_len);
}

std::string FlowTableKSyncEntry::ToString() const {
    std::ostringstream str;
    const FlowKey *fe_key = &flow_entry_->key();
    str << "Flow : " << hash_id_
        << " with Source IP: " << fe_key->src_addr.to_string()
        << " Source port: " << fe_key->src_port
        << " Destination IP: " << fe_key->dst_addr.to_string()
        << " Destination port: " << fe_key->dst_port
        << " Protocol "<< (uint16_t)fe_key->protocol;
    return str.str();
}

bool FlowTableKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const FlowTableKSyncEntry &entry = static_cast
        <const FlowTableKSyncEntry &>(rhs);
    /*
     * Ksync Flow Table should have the same key as vrouter flow table,
     * so that all the flow entries present in vrouter can be represented
     * in Ksync. This will also ensure that the index change for a flow
     * entry will be sync'ed appropriately in vrouter.
     */
    if (hash_id_ != entry.hash_id_) {
        return hash_id_ < entry.hash_id_;
    }
    return flow_entry_ < entry.flow_entry_;
}

void FlowTableKSyncEntry::ErrorHandler(int err, uint32_t seq_no) const {
    if (err == ENOSPC || err == EBADF) {
        KSYNC_ERROR(VRouterError, "VRouter operation failed. Error <", err,
                    ":", strerror(err), ">. Object <", ToString(),
                    ">. Operation <", OperationString(), ">. Message number :",
                    seq_no);
        return;
    }
    KSyncEntry::ErrorHandler(err, seq_no);
}

FlowTableKSyncObject::FlowTableKSyncObject(KSync *ksync) : 
    KSyncObject("KSync FlowTable"), ksync_(ksync), free_list_(this) {
}

FlowTableKSyncObject::FlowTableKSyncObject(KSync *ksync, int max_index) :
    KSyncObject("KSync FlowTable", max_index), ksync_(ksync), free_list_(this) {
}

FlowTableKSyncObject::~FlowTableKSyncObject() {
}

void FlowTableKSyncObject::PreFree(KSyncEntry *entry) {
    FlowTableKSyncEntry *flow_ksync = static_cast<FlowTableKSyncEntry *>(entry);
    FlowEntry *flow = flow_ksync->flow_entry().get();
    ksync_->ksync_flow_index_manager()->ReleaseRequest(flow);
}

KSyncEntry *FlowTableKSyncObject::Alloc(const KSyncEntry *key, uint32_t index) {
    const FlowTableKSyncEntry *entry  =
        static_cast<const FlowTableKSyncEntry *>(key);
    return free_list_.Allocate(entry);
}

void FlowTableKSyncObject::Free(KSyncEntry *entry) {
    FlowTableKSyncEntry *ksync  = static_cast<FlowTableKSyncEntry *>(entry);
    free_list_.Free(ksync);
}

FlowTableKSyncEntry *FlowTableKSyncObject::Find(FlowEntry *key) {
    FlowTableKSyncEntry entry(this, key, key->flow_handle());
    KSyncObject *obj = static_cast<KSyncObject *>(this);
    return static_cast<FlowTableKSyncEntry *>(obj->Find(&entry));
}

void FlowTableKSyncObject::UpdateKey(KSyncEntry *entry, uint32_t flow_handle) {
    static_cast<FlowTableKSyncEntry *>(entry)->set_hash_id(flow_handle);
}

void FlowTableKSyncObject::UpdateFlowHandle(FlowTableKSyncEntry *entry,
                                            uint32_t flow_handle) {
    ChangeKey(entry, flow_handle);
}

void FlowTableKSyncObject::Init() {
}

/////////////////////////////////////////////////////////////////////////////
// KSyncFlowEntryFreeList implementation
/////////////////////////////////////////////////////////////////////////////
KSyncFlowEntryFreeList::KSyncFlowEntryFreeList(FlowTableKSyncObject *object) :
    object_(object), max_count_(0), grow_pending_(false), total_alloc_(0),
    total_free_(0), free_list_() {

    uint32_t count = kInitCount;
    if (object->ksync()->agent()->test_mode()) {
        count = kTestInitCount;
    }
    while (max_count_ < count) {
        free_list_.push_back(*new FlowTableKSyncEntry(object_));
        max_count_++;
    }
}

KSyncFlowEntryFreeList::~KSyncFlowEntryFreeList() {
    while (free_list_.empty() == false) {
        FreeList::iterator it = free_list_.begin();
        FlowTableKSyncEntry *flow = &(*it);
        free_list_.erase(it);
        delete flow;
    }
}

// Allocate a chunk of FlowEntries
void KSyncFlowEntryFreeList::Grow() {
    grow_pending_ = false;
    if (free_list_.size() >= kMinThreshold)
        return;

    for (uint32_t i = 0; i < kGrowSize; i++) {
        free_list_.push_front(*new FlowTableKSyncEntry(object_));
        max_count_++;
    }
}

FlowTableKSyncEntry *KSyncFlowEntryFreeList::Allocate(const KSyncEntry *key) {
    const FlowTableKSyncEntry *flow_key  =
        static_cast<const FlowTableKSyncEntry *>(key);
    FlowTableKSyncEntry *flow = NULL;
    if (free_list_.size() == 0) {
        flow = new FlowTableKSyncEntry(object_);
        max_count_++;
    } else {
        FreeList::iterator it = free_list_.begin();
        flow = &(*it);
        free_list_.erase(it);
    }

    if (grow_pending_ == false && free_list_.size() < kMinThreshold) {
        grow_pending_ = true;
        FlowProto *proto = object_->ksync()->agent()->pkt()->get_flow_proto();
        proto->GrowFreeListRequest(flow_key->flow_entry()->key());
    }

    // Do post allocation initialization
    flow->Reset(flow_key->flow_entry().get(), flow_key->hash_id());
    total_alloc_++;
    return flow;
}

void KSyncFlowEntryFreeList::Free(FlowTableKSyncEntry *flow) {
    total_free_++;
    flow->Reset();
    free_list_.push_back(*flow);
    // TODO : Free entry if beyound threshold
}

void FlowTableKSyncObject::GrowFreeList() {
    free_list_.Grow();
}
