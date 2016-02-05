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
#include <net/address_util.h>
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

#include <vr_types.h>
#include <nl_util.h>
#include <vr_flow.h>
#include <vr_genetlink.h>

#include "ksync_init.h"
#include "ksync_flow_memory.h"
using namespace boost::asio::ip;
static const int kTestFlowTableSize = 131072 * sizeof(vr_flow_entry);

KSyncFlowMemory::KSyncFlowMemory(KSync *ksync) :
    ksync_(ksync),
    flow_table_path_(),
    major_devid_(0),
    flow_table_size_(0),
    flow_table_entries_count_(0),
    audit_timer_(TimerManager::CreateTimer
                 (*(ksync->agent()->event_manager())->io_service(),
                  "Flow Audit Timer",
                  ksync->agent()->task_scheduler()->GetTaskId(kTaskFlowAudit),
                  0)),
    audit_timeout_(0),
    audit_yield_(0),
    audit_interval_(0),
    audit_flow_idx_(0),
    audit_flow_list_() {
}

KSyncFlowMemory::~KSyncFlowMemory() {
    TimerManager::DeleteTimer(audit_timer_);
}

void KSyncFlowMemory::Init() {
    IcmpErrorProto *proto = ksync_->agent()->services()->icmp_error_proto();
    proto->Register(boost::bind(&KSyncFlowMemory::GetFlowKey, this, _1, _2));

    audit_interval_ = kAuditYieldTimer;
    audit_timeout_ = kAuditTimeout;
    uint32_t flow_table_count = ksync_->agent()->flow_table_size();
    // Compute number of entries to visit per timer interval so that complete
    // table can be visited in kAuditSweepTime
    uint32_t timer_per_sec = 1000 / kAuditYieldTimer;
    uint32_t timer_per_sweep = kAuditSweepTime * timer_per_sec;
    audit_yield_ = flow_table_count / timer_per_sweep;
    if (audit_yield_ > kAuditYieldMax)
        audit_yield_ = kAuditYieldMax;

    audit_timer_->Start(audit_interval_,
                        boost::bind(&KSyncFlowMemory::AuditProcess, this));
}

// Steps to map flow table entry
// - Query the Flow table parameters from kernel
// - Create device /dev/flow with major-num and minor-num
// - Map device memory
void KSyncFlowMemory::InitFlowMem() {
    struct nl_client *cl;
    vr_flow_req req;
    int attr_len;
    int encode_len, error, ret;

    assert((cl = nl_register_client()) != NULL);
    assert(nl_socket(cl, AF_NETLINK, SOCK_DGRAM, NETLINK_GENERIC) > 0);
    assert(nl_connect(cl, 0, 0) == 0);
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
        KSyncSockNetlink::NetlinkDecoder(cl->cl_buf,
                                         KSyncSock::GetAgentSandeshContext());
    }
    nl_free_client(cl);

    // Remove the existing /dev/flow file first. We will add it again below
#if !defined(__FreeBSD__)
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
#endif

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
    ksync_->agent()->set_flow_table_size(flow_table_entries_count_);
    return;
}

void KSyncFlowMemory::InitTest() {
    flow_table_ = KSyncSockTypeMap::FlowMmapAlloc(kTestFlowTableSize);
    memset(flow_table_, 0, kTestFlowTableSize);
    flow_table_entries_count_ = kTestFlowTableSize / sizeof(vr_flow_entry);
    audit_yield_ = flow_table_entries_count_;
    audit_timeout_ = 10; // timout immediately.
    ksync_->agent()->set_flow_table_size(flow_table_entries_count_);
}

void KSyncFlowMemory::UnmapFlowMemTest() {
    KSyncSockTypeMap::FlowMmapFree();
}

void KSyncFlowMemory::Shutdown() {
    UnmapFlowMemTest();
}

void KSyncFlowMemory::KFlow2FlowKey(const vr_flow_entry *kflow,
                                    FlowKey *key) const {
    key->nh = kflow->fe_key.flow4_nh_id;
    Address::Family family = (kflow->fe_key.flow_family == AF_INET)?
                              Address::INET : Address::INET6;
    CharArrayToIp(kflow->fe_key.flow_ip, sizeof(kflow->fe_key.flow_ip),
                  family, &key->src_addr, &key->dst_addr);
    key->src_port = ntohs(kflow->fe_key.flow4_sport);
    key->dst_port = ntohs(kflow->fe_key.flow4_dport);
    key->protocol = kflow->fe_key.flow4_proto;
    key->family = family;
}

const vr_flow_entry *KSyncFlowMemory::GetValidKFlowEntry(const FlowKey &key,
                                                         uint32_t idx) const {
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
        /* TODO: If a flow is evicted from vrouter and later flow with same
         * key is assigned with same index, then we may end up reading
         * wrong stats */
    }
    return kflow;
}

const vr_flow_entry *KSyncFlowMemory::GetKernelFlowEntry
    (uint32_t idx, bool ignore_active_status) const {
    if (idx == FlowEntry::kInvalidFlowHandle) {
        return NULL;
    }

    if (idx >= flow_table_entries_count_) {
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

bool KSyncFlowMemory::GetFlowKey(uint32_t index, FlowKey *key) {
    const vr_flow_entry *kflow = GetKernelFlowEntry(index, false);
    if (!kflow) {
        return false;
    }
    key->nh = kflow->fe_key.flow4_nh_id;
    Address::Family family = (kflow->fe_key.flow_family == AF_INET)?
                              Address::INET : Address::INET6;
    CharArrayToIp(kflow->fe_key.flow_ip, sizeof(kflow->fe_key.flow_ip),
                  family, &key->src_addr, &key->dst_addr);
    key->src_port = ntohs(kflow->fe_key.flow4_sport);
    key->dst_port = ntohs(kflow->fe_key.flow4_dport);
    key->protocol = kflow->fe_key.flow4_proto;
    key->family = family;
    return true;
}

bool KSyncFlowMemory::AuditProcess() {
    uint32_t flow_idx;
    const vr_flow_entry *vflow_entry;
    // Get current time
    uint64_t t = UTCTimestampUsec();

    while (!audit_flow_list_.empty()) {
        std::pair<uint32_t, uint64_t> list_entry = audit_flow_list_.front();
        // audit_flow_list_ is sorted on last time of insertion in the list
        // So, break on finding first flow entry that cannot be aged
        if ((t - list_entry.second) < audit_timeout_) {
            /* Wait for audit_timeout_ to create short flow for the entry */
            break;
        }
        flow_idx = list_entry.first;
        audit_flow_list_.pop_front();

        vflow_entry = GetKernelFlowEntry(flow_idx, false);
        // Audit and remove flow entry if its still in HOLD state
        if (vflow_entry && vflow_entry->fe_action == VR_FLOW_ACTION_HOLD) {
            int family = (vflow_entry->fe_key.flow_family == AF_INET)?
                Address::INET : Address::INET6;
            IpAddress sip, dip;
            CharArrayToIp(vflow_entry->fe_key.flow_ip,
                          sizeof(vflow_entry->fe_key.flow_ip), family, &sip,
                          &dip);
            FlowKey key(vflow_entry->fe_key.flow_nh_id, sip, dip,
                        vflow_entry->fe_key.flow_proto,
                        ntohs(vflow_entry->fe_key.flow_sport),
                        ntohs(vflow_entry->fe_key.flow_dport));

            FlowProto *proto = ksync_->agent()->pkt()->get_flow_proto();
            proto->CreateAuditEntry(key, flow_idx);
            AGENT_ERROR(FlowLog, flow_idx, "FlowAudit : Converting HOLD "
                        "entry to short flow");
        }
    }

    uint32_t count = 0;
    assert(audit_yield_);
    while (count < audit_yield_) {
        vflow_entry = GetKernelFlowEntry(audit_flow_idx_, false);
        if (vflow_entry && vflow_entry->fe_action == VR_FLOW_ACTION_HOLD) {
            audit_flow_list_.push_back(std::make_pair(audit_flow_idx_, t));
        }

        count++;
        audit_flow_idx_++;
        if (audit_flow_idx_ == flow_table_entries_count_) {
            audit_flow_idx_ = 0;
        }
    }
    return true;
}

void KSyncFlowMemory::GetFlowTableSize() {
    struct nl_client *cl;
    vr_flow_req req;
    int attr_len;
    int encode_len, error;

    assert((cl = nl_register_client()) != NULL);
    cl->cl_genl_family_id = KSyncSock::GetNetlinkFamilyId();
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

    tcp::socket socket(*(ksync_->agent()->event_manager()->io_service()));
    tcp::endpoint endpoint(ksync_->agent()->vrouter_server_ip(),
                           ksync_->agent()->vrouter_server_port());
    boost::system::error_code ec;
    socket.connect(endpoint, ec);
    if (ec) {
        assert(0);
    }

    socket.send(boost::asio::buffer(cl->cl_buf, cl->cl_buf_offset), 0, ec);
    if (ec) {
        assert(0);
    }

    uint32_t len_read = 0;
    uint32_t data_len = sizeof(struct nlmsghdr);
    while (len_read < data_len) {
        len_read = socket.read_some(boost::asio::buffer(cl->cl_buf + len_read,
                                                        cl->cl_buf_len), ec);
        if (ec) {
            assert(0);
        }

        if (len_read > sizeof(struct nlmsghdr)) {
            const struct nlmsghdr *nlh =
                (const struct nlmsghdr *)((cl->cl_buf));
            data_len = nlh->nlmsg_len;
        }
    }

    KSyncSockNetlink::NetlinkDecoder(cl->cl_buf,
                                     KSyncSock::GetAgentSandeshContext());
    nl_free_client(cl);
}

void KSyncFlowMemory::MapSharedMemory() {
    GetFlowTableSize();

    int fd;
    if ((fd = open(flow_table_path_.c_str(), O_RDONLY | O_SYNC)) < 0) {
        LOG(DEBUG, "Error opening device " << flow_table_path_
            << ". Error <" << errno
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
    ksync_->agent()->set_flow_table_size(flow_table_entries_count_);
}

void vr_flow_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->FlowMsgHandler(this);
}
