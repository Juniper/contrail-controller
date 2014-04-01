/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/socket.h>
#if defined(__linux__)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <asm/types.h>
#elif defined(__FreeBSD__)
#include "vr_os.h"
#include <fcntl.h>
#include <sys/mman.h>
#endif

#include <boost/asio.hpp>

#include <cmn/agent_cmn.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_sock.h>
#include <ksync/agent_ksync_types.h>
#include <ksync/interface_ksync.h>
#include <ksync/nexthop_ksync.h>
#include <ksync/mirror_ksync.h>
#include <ksync/flowtable_ksync.h>
#include <filter/traffic_action.h>
#include <vr_types.h>
#include <nl_util.h>
#include <vr_flow.h>
#include <vr_genetlink.h>
#include <ksync/ksync_sock_user.h>
#include <ksync/ksync_init.h>

#include <pkt/flow_proto.h>
#include <oper/agent_types.h>
#include <uve/stats_collector.h>

FlowTableKSyncEntry::FlowTableKSyncEntry(FlowTableKSyncObject *obj, 
                                         FlowEntryPtr fe, uint32_t hash_id)
    : flow_entry_(fe), hash_id_(hash_id), 
    old_reverse_flow_id_(FlowEntry::kInvalidFlowHandle), old_action_(0), 
    old_component_nh_idx_(0xFFFF), old_first_mirror_index_(0xFFFF), 
    old_second_mirror_index_(0xFFFF), trap_flow_(false), nh_(NULL), 
    ksync_obj_(obj) {
}

FlowTableKSyncEntry::~FlowTableKSyncEntry() {
}

KSyncObject *FlowTableKSyncEntry::GetObject() {
    return ksync_obj_;
}

void FlowTableKSyncEntry::SetPcapData(FlowEntryPtr fe, 
                                      std::vector<int8_t> &data) {
    data.clear();
    uint32_t addr = ksync_obj_->ksync()->agent()->GetRouterId().to_ulong();
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
    FlowEntry *rev_flow = flow_entry_->reverse_flow_entry();

    //If action is NAT and reverse flow entry is not valid
    //then we should wait for the reverse flow to be programmed
    if ((flow_entry_->is_flags_set(FlowEntry::NatFlow) ||
         flow_entry_->is_flags_set(FlowEntry::EcmpFlow)) &&
        rev_flow && rev_flow->flow_handle() == FlowEntry::kInvalidFlowHandle) {
        return 0;
    }

    req.set_fr_op(flow_op::FLOW_SET);
    req.set_fr_rid(0);
    req.set_fr_index(flow_entry_->flow_handle());
    const FlowKey *fe_key = &flow_entry_->key();
    req.set_fr_flow_sip(htonl(fe_key->src.ipv4));
    req.set_fr_flow_dip(htonl(fe_key->dst.ipv4));
    req.set_fr_flow_proto(fe_key->protocol);
    req.set_fr_flow_sport(htons(fe_key->src_port));
    req.set_fr_flow_dport(htons(fe_key->dst_port));
    req.set_fr_flow_vrf(fe_key->vrf);
    uint16_t flags = 0;

    if (op == sandesh_op::DELETE) {
        if (flow_entry_->flow_handle() == FlowEntry::kInvalidFlowHandle) {
            return 0;
        }
        req.set_fr_flags(0);
    } else {
        flags = VR_FLOW_FLAG_ACTIVE;
        uint32_t fe_action = flow_entry_->match_p().action_info.action;
        if ((fe_action) & (1 << TrafficAction::PASS)) {
            action = VR_FLOW_ACTION_FORWARD;
        } 
        
        if ((fe_action) & (1 << TrafficAction::DROP)) {
            action = VR_FLOW_ACTION_DROP;
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
                                     GetRouterId().to_ulong()));
            req.set_fr_mir_sport(htons(ksync_obj_->ksync()->agent()->
                                                            GetMirrorPort()));
            std::vector<int8_t> pcap_data;
            SetPcapData(flow_entry_, pcap_data);
            req.set_fr_pcap_meta_data(pcap_data);
        }

        req.set_fr_ftable_size(0);
        req.set_fr_ecmp_nh_index(flow_entry_->data().component_nh_idx);

        if (flow_entry_->is_flags_set(FlowEntry::EcmpFlow)) {
            flags |= VR_RFLOW_VALID; 
            FlowEntry *rev_flow = flow_entry_->reverse_flow_entry();
            req.set_fr_rindex(rev_flow->flow_handle());
        }
 
        if (action == VR_FLOW_ACTION_NAT) {
            flags |= VR_RFLOW_VALID; 
            FlowEntry *nat_flow = flow_entry_->reverse_flow_entry();
            const FlowKey *nat_key = &nat_flow->key();

            if (flow_entry_->key().src.ipv4 != nat_key->dst.ipv4) {
                flags |= VR_FLOW_FLAG_SNAT;
            }
            if (flow_entry_->key().dst.ipv4 != nat_key->src.ipv4) {
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

            flags |= VR_FLOW_FLAG_VRFT;
            req.set_fr_flow_dvrf(flow_entry_->data().dest_vrf);
            req.set_fr_rindex(nat_flow->flow_handle());
        }

        if (flow_entry_->is_flags_set(FlowEntry::Trap)) {
            flags |= VR_FLOW_FLAG_TRAP_ECMP;
            action = VR_FLOW_ACTION_HOLD;
        }

        if (nh_) {
            req.set_fr_src_nh_index(nh_->GetIndex());
        } else {
            //Set to discard
            req.set_fr_src_nh_index(0);
        }

        req.set_fr_flags(flags);
        req.set_fr_action(action);
    }

    encode_len = req.WriteBinary((uint8_t *)buf, buf_len, &error);
    return encode_len;
}

bool FlowTableKSyncEntry::Sync() {
    bool changed = false;
    
    if (hash_id_ != flow_entry_->flow_handle()) {
        hash_id_ = flow_entry_->flow_handle();
        changed = true;
    }

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

    if (flow_entry_->data().nh_state_.get() && 
        flow_entry_->data().nh_state_->nh()) {
        NHKSyncObject *nh_object = ksync_obj_->ksync()->nh_ksync_obj();
        DBTableBase *table = nh_object->GetDBTable();
        NHKSyncEntry *nh;
        nh = static_cast<NHKSyncEntry *>(flow_entry_->data().nh_state_->nh()->
                GetState(table, nh_object->GetListenerId(table)));
        if (nh == NULL) {
            NHKSyncEntry tmp_nh(nh_object, flow_entry_->data().nh_state_->nh());
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
    if (flow_entry_->data().nh_state_.get() && 
        flow_entry_->data().nh_state_->nh()) {
        NHKSyncObject *nh_object = ksync_obj_->ksync()->nh_ksync_obj();
        DBTableBase *table = nh_object->GetDBTable();
        NHKSyncEntry *nh;
        nh = static_cast<NHKSyncEntry *>(flow_entry_->data().nh_state_->nh()->
                GetState(table, nh_object->GetListenerId(table)));
        if (nh == NULL) {
            NHKSyncEntry tmp_nh(nh_object, flow_entry_->data().nh_state_->nh());
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
    str << flow_entry_;
    return str.str();
}

bool FlowTableKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const FlowTableKSyncEntry &entry = static_cast
        <const FlowTableKSyncEntry &>(rhs);
    return flow_entry_ < entry.flow_entry_;
}

FlowTableKSyncObject::FlowTableKSyncObject(KSync *ksync) : 
    KSyncObject(), ksync_(ksync), audit_flow_idx_(0),
    audit_timer_(TimerManager::CreateTimer
                 (*(ksync_->agent()->GetEventManager())->io_service(),
                  "Flow Audit Timer",
                  TaskScheduler::GetInstance()->GetTaskId
                  ("Agent::StatsCollector"),
                  StatsCollector::FlowStatsCollector)) { 
}

FlowTableKSyncObject::FlowTableKSyncObject(KSync *ksync, int max_index) :
    KSyncObject(max_index), ksync_(ksync), audit_flow_idx_(0),
    audit_timer_(TimerManager::CreateTimer
                 (*(ksync_->agent()->GetEventManager())->io_service(),
                  "Flow Audit Timer",
                  TaskScheduler::GetInstance()->GetTaskId
                  ("Agent::StatsCollector"),
                  StatsCollector::FlowStatsCollector)) {
}

FlowTableKSyncObject::~FlowTableKSyncObject() {
    TimerManager::DeleteTimer(audit_timer_);
}

KSyncEntry *FlowTableKSyncObject::Alloc(const KSyncEntry *key, uint32_t index) {
    const FlowTableKSyncEntry *entry  =
        static_cast<const FlowTableKSyncEntry *>(key);
    FlowTableKSyncEntry *ksync = new FlowTableKSyncEntry(this, 
                                                         entry->flow_entry(),
                                                         entry->hash_id());
    return static_cast<KSyncEntry *>(ksync);
}

FlowTableKSyncEntry *FlowTableKSyncObject::Find(FlowEntry *key) {
    FlowTableKSyncEntry entry(this, key, key->flow_handle());
    KSyncObject *obj = static_cast<KSyncObject *>(this);
    return static_cast<FlowTableKSyncEntry *>(obj->Find(&entry));
}

const vr_flow_entry *FlowTableKSyncObject::GetKernelFlowEntry
    (uint32_t idx, bool ignore_active_status) { 
    if (idx == FlowEntry::kInvalidFlowHandle) {
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

bool FlowTableKSyncObject::GetFlowKey(uint32_t index, FlowKey &key) {
    const vr_flow_entry *kflow = GetKernelFlowEntry(index, false);
    if (!kflow) {
        return false;
    }
    key.vrf = kflow->fe_key.key_vrf_id;
    key.src.ipv4 = ntohl(kflow->fe_key.key_src_ip);
    key.dst.ipv4 = ntohl(kflow->fe_key.key_dest_ip);
    key.src_port = ntohs(kflow->fe_key.key_src_port);
    key.dst_port = ntohs(kflow->fe_key.key_dst_port);
    key.protocol = kflow->fe_key.key_proto;
    return true;
}

void vr_flow_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->FlowMsgHandler(this);
}

void FlowTableKSyncObject::MapFlowMemTest() {
    flow_table_ = KSyncSockTypeMap::FlowMmapAlloc(kTestFlowTableSize);
    memset(flow_table_, 0, kTestFlowTableSize);
    flow_table_entries_count_ = kTestFlowTableSize / sizeof(vr_flow_entry);
    audit_yield_ = flow_table_entries_count_;
    audit_timeout_ = 0; // timout immediately.
}

void FlowTableKSyncObject::UnmapFlowMemTest() {
    KSyncSockTypeMap::FlowMmapFree();
}

bool FlowTableKSyncObject::AuditProcess() {
    uint32_t flow_idx;
    const vr_flow_entry *vflow_entry;
    audit_timestamp_ += AuditYieldTimer;
    while (!audit_flow_list_.empty()) {
        std::pair<uint32_t, uint64_t> list_entry = audit_flow_list_.front();
        if ((audit_timestamp_ - list_entry.second) < audit_timeout_) {
            /* Wait for audit_timeout_ to create short flow for the entry */
            break;
        }
        flow_idx = list_entry.first;
        audit_flow_list_.pop_front();

        vflow_entry = GetKernelFlowEntry(flow_idx, false);
        if (vflow_entry && vflow_entry->fe_action == VR_FLOW_ACTION_HOLD) {
            FlowKey key(vflow_entry->fe_key.key_vrf_id, 
                        ntohl(vflow_entry->fe_key.key_src_ip), 
                        ntohl(vflow_entry->fe_key.key_dest_ip),
                        vflow_entry->fe_key.key_proto,
                        ntohs(vflow_entry->fe_key.key_src_port),
                        ntohs(vflow_entry->fe_key.key_dst_port));
            FlowEntry *flow_p = ksync_->agent()->pkt()->flow_table()->
                                Find(key);
            if (flow_p == NULL) {
                /* Create Short flow only for non-existing flows. */
                FlowEntryPtr flow(ksync_->agent()->pkt()->flow_table()->
                                  Allocate(key));
                flow->InitAuditFlow(flow_idx);
                AGENT_ERROR(FlowLog, flow_idx, "FlowAudit : Converting HOLD "
                            "entry to short flow");
                ksync_->agent()->pkt()->flow_table()->Add(flow.get(), NULL);
            }

        }
    }

    int count = 0;
    assert(audit_yield_);
    while (count < audit_yield_) {
        vflow_entry = GetKernelFlowEntry(audit_flow_idx_, false);
        if (vflow_entry && vflow_entry->fe_action == VR_FLOW_ACTION_HOLD) {
            audit_flow_list_.push_back(std::make_pair(audit_flow_idx_,
                                                      audit_timestamp_));
        }

        count++;
        audit_flow_idx_++;
        if (audit_flow_idx_ == flow_table_entries_count_) {
            audit_flow_idx_ = 0;
        }
    }
    return true;
}

// Steps to map flow table entry
// - Query the Flow table parameters from kernel
// - Create device /dev/flow with major-num and minor-num 
// - Map device memory
void FlowTableKSyncObject::MapFlowMem() {
    struct nl_client *cl;
    vr_flow_req req;
    int attr_len;
    int encode_len, error, ret;

    assert((cl = nl_register_client()) != NULL);
    assert(nl_socket(cl, NETLINK_GENERIC) > 0);
    assert(vrouter_get_family_id(cl) > 0);

    assert(nl_build_nlh(cl, cl->cl_genl_family_id, NLM_F_REQUEST) == 0);
    assert(nl_build_genlh(cl, SANDESH_REQUEST, 0) == 0);

    attr_len = nl_get_attr_hdr_size();

    req.set_fr_op(flow_op::FLOW_TABLE_GET);
    req.set_fr_rid(0);
    req.set_fr_index(0);
    req.set_fr_action(0);
    req.set_fr_flags(0);
    req.set_fr_ftable_size(0);
    encode_len = req.WriteBinary(nl_get_buf_ptr(cl) + attr_len,
                                 nl_get_buf_len(cl), &error);
    nl_build_attr(cl, encode_len, NL_ATTR_VR_MESSAGE_PROTOCOL);
    nl_update_nlh(cl);

    if ((ret = nl_sendmsg(cl)) < 0) {
        LOG(DEBUG, "Error requesting Flow Table message. Error : " << ret);
        assert(0);
    }

    while ((ret = nl_recvmsg(cl)) > 0) {
        KSyncSock *sock = KSyncSock::Get(0);
        sock->Decoder(cl->cl_buf, KSyncSock::GetAgentSandeshContext());
    }
    nl_free_client(cl);

    // Remove the existing /dev/flow file first. We will add it again below
    if (unlink("/dev/flow") != 0) {
        if (errno != ENOENT) {
            LOG(DEBUG, "Error deleting </dev/flow>. Error <" << errno 
                << "> : " << strerror(errno));
            assert(0);
        }
    }

    assert(flow_table_size_ != 0);
    assert(major_devid_);
    if (mknod("/dev/flow", (S_IFCHR | O_RDWR), makedev(major_devid_, 0)) < 0) {
        if (errno != EEXIST) {
            LOG(DEBUG, "Error creating device </dev/flow>. Error <" << errno
            << "> : " << strerror(errno));
            assert(0);
        }
    }

    int fd;
    if ((fd = open("/dev/flow", O_RDONLY | O_SYNC)) < 0) {
        LOG(DEBUG, "Error opening device </dev/flow>. Error <" << errno 
            << "> : " << strerror(errno));
        assert(0);
    }

    flow_table_ = (vr_flow_entry *)mmap(NULL, flow_table_size_,
                                        PROT_READ, MAP_SHARED, fd, 0);
    if (flow_table_ == MAP_FAILED) {
        LOG(DEBUG, "Error mapping flow table memory. Error <" << errno
            << "> : " << strerror(errno));
        assert(0);
    }

    flow_table_entries_count_ = flow_table_size_ / sizeof(vr_flow_entry);
    audit_yield_ = AuditYield;
    audit_timeout_ = AuditTimeout;
    audit_timer_->Start(AuditTimeout,
                        boost::bind(&FlowTableKSyncObject::AuditProcess, 
                                    this));
    return;
}
