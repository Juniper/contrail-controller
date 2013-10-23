/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <asm/types.h>

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

#include <pkt/pkt_flow.h>
#include <oper/agent_types.h>

FlowTableKSyncObject *FlowTableKSyncObject::singleton_;

FlowTableKSyncObject::FlowTableKSyncObject() : 
    KSyncObject(), audit_flow_idx_(0),
    audit_timer_(TimerManager::CreateTimer
                 (*(Agent::GetInstance()->GetEventManager())->io_service(),
                  "Flow Audit Timer",
                  TaskScheduler::GetInstance()->GetTaskId
                  ("Agent::StatsCollector"))) { 
}

FlowTableKSyncObject::FlowTableKSyncObject(int max_index) :
    KSyncObject(max_index), audit_flow_idx_(0),
    audit_timer_(TimerManager::CreateTimer
                 (*(Agent::GetInstance()->GetEventManager())->io_service(),
                  "Flow Audit Timer",
                  TaskScheduler::GetInstance()->GetTaskId
                  ("Agent::StatsCollector"))) {
};
FlowTableKSyncObject::~FlowTableKSyncObject() {
    TimerManager::DeleteTimer(audit_timer_);
};

KSyncEntry *FlowTableKSyncObject::Alloc(const KSyncEntry *key, uint32_t index) {
    const FlowTableKSyncEntry *entry  =
        static_cast<const FlowTableKSyncEntry *>(key);
    FlowTableKSyncEntry *ksync = new FlowTableKSyncEntry(entry->GetFe(),
                                                         entry->GetHashId());
    return static_cast<KSyncEntry *>(ksync);
}

FlowTableKSyncEntry *FlowTableKSyncObject::Find(FlowEntry *key) {
    FlowTableKSyncEntry entry(key, key->flow_handle);
    KSyncObject *obj = 
        static_cast<KSyncObject *>(FlowTableKSyncObject::GetKSyncObject());
    return static_cast<FlowTableKSyncEntry *>(obj->Find(&entry));
}

void FlowTableKSyncObject::UpdateFlowStats(FlowEntry *fe,
                                           bool ignore_active_status) {
    const vr_flow_entry *k_flow = GetKernelFlowEntry
        (fe->flow_handle, ignore_active_status);
    if (k_flow) {
        fe->data.bytes =  k_flow->fe_stats.flow_bytes;
        fe->data.packets =  k_flow->fe_stats.flow_packets;
    }

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

KSyncObject *FlowTableKSyncEntry::GetObject() {
    return FlowTableKSyncObject::GetKSyncObject();
}

void FlowTableKSyncEntry::SetPcapData(FlowEntryPtr fe, 
                                      std::vector<int8_t> &data) {
    data.clear();
    uint32_t addr = Agent::GetInstance()->GetRouterId().to_ulong();
    data.push_back(FlowEntry::PCAP_CAPTURE_HOST);
    data.push_back(0x4);
    data.push_back(((addr >> 24) & 0xFF));
    data.push_back(((addr >> 16) & 0xFF));
    data.push_back(((addr >> 8) & 0xFF));
    data.push_back(((addr) & 0xFF));

    data.push_back(FlowEntry::PCAP_FLAGS);
    data.push_back(0x4);
    uint32_t action;
    action = fe->data.match_p.action_info.action;
    if (fe->data.ingress) {
        // Set 31st bit for ingress
        action |= 0x40000000;
    }
    data.push_back((action >> 24) & 0xFF);
    data.push_back((action >> 16) & 0xFF);
    data.push_back((action >> 8) & 0xFF);
    data.push_back((action) & 0xFF);
                                                                                                                                                                
    data.push_back(FlowEntry::PCAP_SOURCE_VN);
    data.push_back(fe->data.source_vn.size());
    data.insert(data.end(), fe->data.source_vn.begin(), fe->data.source_vn.end());
    data.push_back(FlowEntry::PCAP_DEST_VN);
    data.push_back(fe->data.dest_vn.size());
    data.insert(data.end(), fe->data.dest_vn.begin(), fe->data.dest_vn.end());
    data.push_back(FlowEntry::PCAP_TLV_END);
    data.push_back(0x0);
}

int FlowTableKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_flow_req req;
    int encode_len;
    int error;
    uint16_t action = 0;
    std::vector<int8_t> pcap_data;


    if (fe_->flow_handle == FlowEntry::kInvalidFlowHandle) {
        return 0;
    }

    req.set_fr_op(flow_op::FLOW_SET);
    req.set_fr_rid(0);
    req.set_fr_index(fe_->flow_handle);
    FlowKey *fe_key = &fe_->key;
    req.set_fr_flow_sip(htonl(fe_key->src.ipv4));
    req.set_fr_flow_dip(htonl(fe_key->dst.ipv4));
    req.set_fr_flow_proto(fe_key->protocol);
    req.set_fr_flow_sport(htons(fe_key->src_port));
    req.set_fr_flow_dport(htons(fe_key->dst_port));
    req.set_fr_flow_vrf(fe_key->vrf);
    uint16_t flags = 0;

    if (op == sandesh_op::DELETE) {
        req.set_fr_flags(0);
    } else {
        flags = VR_FLOW_FLAG_ACTIVE;
        uint32_t fe_action = fe_->data.match_p.action_info.action;
        if ((fe_action) & (1 << TrafficAction::PASS)) {
            action = VR_FLOW_ACTION_FORWARD;
        } 
        
        if ((fe_action) & (1 << TrafficAction::DROP)) {
            action = VR_FLOW_ACTION_DROP;
        }

        if (action == VR_FLOW_ACTION_FORWARD && fe_->nat) {
            action = VR_FLOW_ACTION_NAT;
        }

        if (action == VR_FLOW_ACTION_NAT && 
            fe_->data.reverse_flow.get() == NULL) {
            action = VR_FLOW_ACTION_DROP;
        }
        
        if ((fe_action) & (1 << TrafficAction::MIRROR)) {
            flags |= VR_FLOW_FLAG_MIRROR;
            req.set_fr_mir_id(-1);
            req.set_fr_sec_mir_id(-1);
            if (fe_->data.match_p.action_info.mirror_l.size() > FlowEntry::kMaxMirrorsPerFlow) {
                FLOW_TRACE(Err, GetHashId(),
                           "Don't support more than two mirrors/analyzers per flow:" +
                           integerToString(fe_->data.match_p.action_info.mirror_l.size()));
            }
            // Lookup for fist and second mirror entries
            std::vector<MirrorActionSpec>::iterator it;
            it = fe_->data.match_p.action_info.mirror_l.begin();
            uint16_t idx_1 = MirrorKSyncObject::GetIdx((*it).analyzer_name);
            req.set_fr_mir_id(idx_1);
            FLOW_TRACE(ModuleInfo, "Mirror index first: " + integerToString(idx_1));
            ++it;
            if (it != fe_->data.match_p.action_info.mirror_l.end()) {
                uint16_t idx_2 = MirrorKSyncObject::GetIdx((*it).analyzer_name);
                if (idx_1 != idx_2) {
                    req.set_fr_sec_mir_id(idx_2);
                    FLOW_TRACE(ModuleInfo, "Mirror index second: " + integerToString(idx_2));
                } else {
                    FLOW_TRACE(Err, GetHashId(), 
                               "Both Mirror indexes are same, hence didn't set the second mirror dest.");
                }
            }
            req.set_fr_mir_vrf(fe_->data.mirror_vrf); 
            req.set_fr_mir_sip(htonl(Agent::GetInstance()->GetRouterId().to_ulong()));
            req.set_fr_mir_sport(htons(Agent::GetInstance()->GetMirrorPort()));
            SetPcapData(fe_, pcap_data);
            req.set_fr_pcap_meta_data(pcap_data);
        }

        req.set_fr_ftable_size(0);

        if (fe_->data.ecmp == true && action != VR_FLOW_ACTION_NAT) {
            FlowEntry *nat_flow = fe_->data.reverse_flow.get();
            if (nat_flow) {
                flags |= VR_RFLOW_VALID;
                FlowKey *nat_key = &nat_flow->key;
                req.set_fr_rindex(nat_flow->flow_handle);
                req.set_fr_rflow_sip(htonl(nat_key->src.ipv4));
                req.set_fr_rflow_dip(htonl(nat_key->dst.ipv4));
                req.set_fr_rflow_proto(nat_key->protocol);
                req.set_fr_rflow_sport(htons(nat_key->src_port));
                req.set_fr_rflow_dport(htons(nat_key->dst_port));
                req.set_fr_rflow_vrf(nat_key->vrf);
                req.set_fr_rflow_dvrf(nat_flow->data.dest_vrf);
                req.set_fr_flow_dvrf(fe_->data.dest_vrf);
                req.set_fr_rflow_mir_vrf(nat_flow->data.mirror_vrf);
                SetPcapData(nat_flow, pcap_data);
                req.set_fr_rflow_pcap_meta_data(pcap_data);
                if (nat_flow->data.trap) {
                    req.set_fr_rflow_action(VR_FLOW_ACTION_TRAP);
                    flags |= VR_FLOW_FLAG_TRAP_ECMP;
                } else {
                    req.set_fr_rflow_action(action);
                }
            }
        }

        if (fe_->data.ecmp == true) {
            FlowEntry *nat_flow = fe_->data.reverse_flow.get();
            req.set_fr_ecmp_nh_index(fe_->data.component_nh_idx);
            if (nat_flow) {
                req.set_fr_rflow_ecmp_nh_index(nat_flow->data.component_nh_idx);
            }
        } else {
            req.set_fr_ecmp_nh_index(0xFFFF);
            req.set_fr_rflow_ecmp_nh_index(0xFFFF);
        }

        if (action == VR_FLOW_ACTION_NAT) {
            flags |= VR_RFLOW_VALID; 
            assert(fe_->data.reverse_flow.get() != NULL);
            FlowEntry *nat_flow = fe_->data.reverse_flow.get();
            FlowKey *nat_key = &nat_flow->key;
            FlowData *nat_data = &nat_flow->data;

            req.set_fr_rindex(nat_flow->flow_handle);
            req.set_fr_rflow_sip(htonl(nat_key->src.ipv4));
            req.set_fr_rflow_dip(htonl(nat_key->dst.ipv4));
            req.set_fr_rflow_proto(nat_key->protocol);
            req.set_fr_rflow_sport(htons(nat_key->src_port));
            req.set_fr_rflow_dport(htons(nat_key->dst_port));

            if (fe_->key.src.ipv4 != nat_key->dst.ipv4) {
                flags |= VR_FLOW_FLAG_SNAT;
            }
            if (fe_->key.dst.ipv4 != nat_key->src.ipv4) {
                flags |= VR_FLOW_FLAG_DNAT;
            }

            if (fe_->key.protocol == IPPROTO_TCP || 
                fe_->key.protocol == IPPROTO_UDP) {
                if (fe_->key.src_port != nat_key->dst_port) {
                    flags |= VR_FLOW_FLAG_SPAT;
                }
                if (fe_->key.dst_port != nat_key->src_port) {
                    flags |= VR_FLOW_FLAG_DPAT;
                }
            }

            flags |= VR_FLOW_FLAG_VRFT;
            req.set_fr_rflow_vrf(nat_key->vrf);
            req.set_fr_rflow_dvrf(nat_data->dest_vrf);

            req.set_fr_flow_dvrf(fe_->data.dest_vrf);
            req.set_fr_rflow_mir_vrf(nat_flow->data.mirror_vrf);
            SetPcapData(nat_flow, pcap_data);
            req.set_fr_rflow_pcap_meta_data(pcap_data);
            if (nat_data->trap) {
                req.set_fr_rflow_action(VR_FLOW_ACTION_TRAP);
                flags |= VR_FLOW_FLAG_TRAP_ECMP;
            } else {
                req.set_fr_rflow_action(action);
            }
        } else {
            if (fe_->data.ecmp != true) {
                req.set_fr_rindex(FlowEntry::kInvalidFlowHandle);
            }
        }

        if (fe_->data.trap) {
            req.set_fr_flags(flags | VR_FLOW_FLAG_TRAP_ECMP);
            req.set_fr_action(VR_FLOW_ACTION_TRAP);
        } else {
            req.set_fr_flags(flags);
            req.set_fr_action(action);
        }
    }

    FillFlowInfo(op, action, flags);

    encode_len = req.WriteBinary((uint8_t *)buf, buf_len, &error);
    return encode_len;
}

std::string FlowTableKSyncEntry::GetActionString(uint16_t action, 
                                                 uint16_t flags) {
    ostringstream action_str;
    if (action == VR_FLOW_ACTION_DROP) {
        action_str << "Drop: ";
    } 

    if (action & VR_FLOW_ACTION_FORWARD) {
        action_str << "Pass: ";
    } 

    if (flags & VR_FLOW_FLAG_SNAT) {
        action_str << "Source NAT ";
    }

    if (flags & VR_FLOW_FLAG_DNAT) {
        action_str << "Destination NAT ";
    }

    if (flags & VR_FLOW_FLAG_SPAT) {
        action_str << "Source Port NAT ";
    }

    if (flags & VR_FLOW_FLAG_DPAT) {
        action_str << "Destination Port NAT ";
    }

    if (flags & VR_FLOW_FLAG_VRFT) {
        action_str << "VRF translate ";
    }

    return action_str.str();
}

void FlowTableKSyncEntry::FillFlowInfo(sandesh_op::type op, 
                                       uint16_t action, uint16_t flag) {
    FlowInfo info;
    info.set_flow_index(fe_->flow_handle);
    info.set_action(GetActionString(action, flag));

    if (op == sandesh_op::ADD) {
        FLOW_TRACE(Trace, "KSync add", info);
    } else {
        FLOW_TRACE(Trace, "KSync delete", info);
    }
}

bool FlowTableKSyncEntry::Sync() {
    bool changed = false;
    
    if (hash_id_ != fe_->flow_handle) {
        hash_id_ = fe_->flow_handle;
        changed = true;
    }

    FlowEntry *rev_flow = fe_->data.reverse_flow.get();   
    if (rev_flow) {
        if (old_reverse_flow_id_ != rev_flow->flow_handle) {
            old_reverse_flow_id_ = rev_flow->flow_handle;
            changed = true;
        }
    }

    if (fe_->data.match_p.action_info.action != old_action_) {
        old_action_ = fe_->data.match_p.action_info.action;
        changed = true;
    }

    if (fe_->data.component_nh_idx != old_component_nh_idx_) {
        old_component_nh_idx_ = fe_->data.component_nh_idx;
        changed = true;
    }

    // Lookup for fist and second mirror entries
    std::vector<MirrorActionSpec>::iterator it;
    it = fe_->data.match_p.action_info.mirror_l.begin();
    if (it != fe_->data.match_p.action_info.mirror_l.end()) { 
        uint16_t idx = MirrorKSyncObject::GetIdx((*it).analyzer_name);
        if (old_first_mirror_index_ != idx) {
            old_first_mirror_index_ = idx;
            changed = true;
        }
        ++it;
        if (it != fe_->data.match_p.action_info.mirror_l.end()) {
            idx = MirrorKSyncObject::GetIdx((*it).analyzer_name);
            if (old_second_mirror_index_ != idx) {
                old_second_mirror_index_ = idx;
                changed = true;
            }
        }
    }

    //Trap reverse flow
    if (trap_flow_ != fe_->data.trap) {
        trap_flow_ = fe_->data.trap;
        changed = true;
    }
    return changed;
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

void FlowTableKSyncEntry::Response() {
}

void vr_flow_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->FlowMsgHandler(this);
}

void FlowTableKSyncObject::MapFlowMemTest() {
    flow_info_.set_fr_op(flow_op::FLOW_TABLE_GET);
    flow_info_.set_fr_rid(0);
    flow_info_.set_fr_index(0);
    flow_info_.set_fr_action(0);
    flow_info_.set_fr_flags(0);
    flow_info_.set_fr_ftable_size(kTestFlowTableSize);

    flow_table_ = KSyncSockTypeMap::FlowMmapAlloc(kTestFlowTableSize);
    memset(flow_table_, 0, kTestFlowTableSize);
    flow_table_entries_ = flow_info_.get_fr_ftable_size() / sizeof(vr_flow_entry);
    audit_yeild_ = flow_table_entries_;
}

void FlowTableKSyncObject::UnmapFlowMemTest() {
    KSyncSockTypeMap::FlowMmapFree();
}

bool FlowTableKSyncObject::AuditProcess(FlowTableKSyncObject *obj) {
    uint32_t flow_idx;
    const vr_flow_entry *vflow_entry;
    while (!obj->audit_flow_list_.empty()) {
        flow_idx = obj->audit_flow_list_.front();
        obj->audit_flow_list_.pop_front();

        vflow_entry = obj->GetKernelFlowEntry(flow_idx, false);
        if (vflow_entry && vflow_entry->fe_action == VR_FLOW_ACTION_HOLD) {
            FlowKey key(vflow_entry->fe_key.key_vrf_id, 
                        ntohl(vflow_entry->fe_key.key_src_ip), 
                        ntohl(vflow_entry->fe_key.key_dest_ip),
                        vflow_entry->fe_key.key_proto,
                        ntohs(vflow_entry->fe_key.key_src_port),
                        ntohs(vflow_entry->fe_key.key_dst_port));
            FlowEntryPtr flow(FlowTable::GetFlowTableObject()->Allocate(key));
            flow->flow_handle = flow_idx;
            flow->short_flow = true;
            flow->data.source_vn = *FlowHandler::UnknownVn();
            flow->data.dest_vn = *FlowHandler::UnknownVn();
            SecurityGroupList empty_sg_id_l;
            flow->data.source_sg_id_l = empty_sg_id_l;
            flow->data.dest_sg_id_l = empty_sg_id_l;
            AGENT_ERROR(FlowLog, flow_idx, "FlowAudit : Converting HOLD entry "
                            " to short flow");
            FlowTable::GetFlowTableObject()->Add(flow.get(), NULL);

        }
    }

    int count = 0;
    assert(obj->audit_yeild_);
    while (count < obj->audit_yeild_) {
        vflow_entry = obj->GetKernelFlowEntry(obj->audit_flow_idx_, false);
        if (vflow_entry && vflow_entry->fe_action == VR_FLOW_ACTION_HOLD) {
            obj->audit_flow_list_.push_back(obj->audit_flow_idx_);
        }

        count++;
        obj->audit_flow_idx_++;
        if (obj->audit_flow_idx_ == obj->flow_table_entries_) {
            obj->audit_flow_idx_ = 0;
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

    assert(flow_info_.get_fr_ftable_size() != 0);
    assert(flow_info_.get_fr_ftable_dev());
    int major_devid = flow_info_.get_fr_ftable_dev();
    if (mknod("/dev/flow", (S_IFCHR | O_RDWR), makedev(major_devid, 0)) < 0) {
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

    flow_table_ = (vr_flow_entry *)mmap(NULL, flow_info_.get_fr_ftable_size(),
                                        PROT_READ, MAP_SHARED, fd, 0);
    if (flow_table_ == MAP_FAILED) {
        LOG(DEBUG, "Error mapping flow table memory. Error <" << errno
            << "> : " << strerror(errno));
        assert(0);
    }

    flow_table_entries_ = flow_info_.get_fr_ftable_size() / sizeof(vr_flow_entry);
    audit_yeild_ = AuditYeild;
    singleton_->audit_timer_->Start(AuditTimeout,
                                    boost::bind(&FlowTableKSyncObject::AuditProcess, singleton_));
    return;
}
