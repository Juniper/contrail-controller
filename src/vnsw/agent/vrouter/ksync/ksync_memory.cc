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

#include <vr_types.h>
#include <nl_util.h>
#include <vr_flow.h>
#include <ini_parser.h>
#include <vr_genetlink.h>

#include "ksync_init.h"
#include "ksync_flow_memory.h"
#include "sandesh_ksync.h"
#include "init/agent_param.h"

using namespace boost::asio::ip;
static const int kTestFlowTableSize = 131072 * sizeof(vr_flow_entry);

KSyncMemory::KSyncMemory(KSync *ksync, uint32_t minor_id) :
    ksync_(ksync),
    table_path_(),
    major_devid_(0),
    minor_devid_(minor_id),
    table_size_(0),
    table_entries_count_(0),
    audit_timer_(TimerManager::CreateTimer
                 (*(ksync->agent()->event_manager())->io_service(),
                  " Audit Timer",
                  ksync->agent()->task_scheduler()->GetTaskId(kTaskFlowAudit),
                  0)),
    audit_timeout_(0),
    audit_yield_(0),
    audit_interval_(0),
    audit_idx_(0),
    audit_list_() {
}

KSyncMemory::~KSyncMemory() {
    TimerManager::DeleteTimer(audit_timer_);
}

void KSyncMemory::Init() {
    audit_interval_ = kAuditYieldTimer;
    audit_timeout_ = kAuditTimeout;
    uint32_t table_count = table_entries_count_;
    // Compute number of entries to visit per timer interval so that complete
    // table can be visited in kAuditSweepTime
    uint32_t timer_per_sec = 1000 / kAuditYieldTimer;
    uint32_t timer_per_sweep = kAuditSweepTime * timer_per_sec;
    audit_yield_ = table_count / timer_per_sweep;
    if (audit_yield_ > kAuditYieldMax)
        audit_yield_ = kAuditYieldMax;
    if (audit_yield_ < kAuditYieldMin)
        audit_yield_ = kAuditYieldMin;

    audit_timer_->Start(audit_interval_,
                        boost::bind(&KSyncMemory::AuditProcess, this));
}

void KSyncMemory::Mmap(bool unlink_node) {
    // Remove the existing /dev/ file first. We will add it again in vr_table_map
    if (unlink_node) {
        const char *error_msg = vr_table_unlink(table_path_.c_str());
        if (error_msg) {
            LOG(DEBUG, "Error unmapping KSync memory: " << error_msg);
            assert(0);
        }
    }
    parse_ini_file();

    const char *mmap_error_msg = vr_table_map(major_devid_, minor_devid_, table_path_.c_str(),
                                              table_size_, &table_);
    if (mmap_error_msg) {
        LOG(ERROR, "Error mapping KSync memory. Device: " << table_path_ << "; " << mmap_error_msg);
        assert(0);
    }
    LOG(INFO, "Mem mapped dev file:" << table_path_.c_str() << " to addr:" << table_ << "\n");

    table_entries_count_ = table_size_ / get_entry_size();
    SetTableSize();
}

int KSyncMemory::GetKernelTableSize() {
    struct nl_client *cl;
    int attr_len;
    int encode_len, ret;

    assert((cl = nl_register_client()) != NULL);

    assert(nl_socket(cl, AF_NETLINK, SOCK_DGRAM, NETLINK_GENERIC) > 0);
    assert(nl_connect(cl, 0, 0) == 0);

    assert(vrouter_obtain_family_id(cl) > 0);

    assert(nl_build_nlh(cl, cl->cl_genl_family_id, NLM_F_REQUEST) == 0);
    assert(nl_build_genlh(cl, SANDESH_REQUEST, 0) == 0);

    attr_len = nl_get_attr_hdr_size();

    encode_len = EncodeReq(cl, attr_len);
    nl_build_attr(cl, encode_len, NL_ATTR_VR_MESSAGE_PROTOCOL);
    nl_update_nlh(cl);

    if ((ret = nl_sendmsg(cl)) < 0) {
        LOG(DEBUG, "Error requesting  Table message. Error : " << ret);
        assert(0);
    }

    while ((ret = nl_recvmsg(cl)) > 0) {
        KSyncSockNetlink::NetlinkDecoder(cl->cl_buf,
                                         KSyncSock::GetAgentSandeshContext(0));
    }
    nl_free_client(cl);
    return table_size_;
}

// Steps to map  table entry
// - Query the  table parameters from kernel
// - Create device /dev/ with major-num and minor-num
// - Map device memory
void KSyncMemory::InitMem() {
    GetKernelTableSize();
    Mmap(true);
    return;
}

void KSyncMemory::InitTest() {
    assert(0);
}

void KSyncMemory::Shutdown() {
    assert(0);
}

bool KSyncMemory::AuditProcess() {
    // Get current time
    uint64_t t = UTCTimestampUsec();

    while (!audit_list_.empty()) {
        AuditEntry list_entry = audit_list_.front();
        // audit_list_ is sorted on last time of insertion in the list
        // So, break on finding first  entry that cannot be aged
        if ((t - list_entry.timeout) < audit_timeout_) {
            /* Wait for audit_timeout_ to create short  for the entry */
            break;
        }
        uint32_t idx = list_entry.audit_idx;
        uint32_t gen_id = list_entry.audit_gen_id;
        audit_list_.pop_front();
        DecrementHoldFlowCounter();
        CreateProtoAuditEntry(idx, gen_id);
    }

    uint32_t count = 0;
    uint8_t gen_id;
    assert(audit_yield_);
    while (count < audit_yield_) {
        if (IsInactiveEntry(audit_idx_, gen_id)) {
            IncrementHoldFlowCounter();
            audit_list_.push_back(AuditEntry(audit_idx_, gen_id, t));
        }

        count++;
        audit_idx_++;
        if (audit_idx_ == table_entries_count_) {
            UpdateAgentHoldFlowCounter();
            audit_idx_ = 0;
        }
    }
    return true;
}

void KSyncMemory::GetTableSize() {
    struct nl_client *cl;
    int attr_len;
    int encode_len;

    assert((cl = nl_register_client()) != NULL);
    cl->cl_genl_family_id = KSyncSock::GetNetlinkFamilyId();
    assert(nl_build_nlh(cl, cl->cl_genl_family_id, NLM_F_REQUEST) == 0);
    assert(nl_build_genlh(cl, SANDESH_REQUEST, 0) == 0);

    attr_len = nl_get_attr_hdr_size();
    encode_len = EncodeReq(cl, attr_len);
    nl_build_attr(cl, encode_len, NL_ATTR_VR_MESSAGE_PROTOCOL);
    nl_update_nlh(cl);
    string ksync_agent_vrouter_sock_path = KSYNC_AGENT_VROUTER_SOCK_PATH;
    ksync_agent_vrouter_sock_path =
    ksync_->agent()->params()->cat_is_agent_mocked()?
    ksync_->agent()->params()->cat_ksocketdir() + 
    "dpdk_netlink" : ksync_agent_vrouter_sock_path;

#ifdef AGENT_VROUTER_TCP
    tcp::socket socket(*(ksync_->agent()->event_manager()->io_service()));
    tcp::endpoint endpoint(ksync_->agent()->vrouter_server_ip(),
                           ksync_->agent()->vrouter_server_port());
#else
    boost::asio::local::stream_protocol::socket
        socket(*(ksync_->agent()->event_manager()->io_service()));
    boost::asio::local::stream_protocol::endpoint
        endpoint(ksync_agent_vrouter_sock_path);
#endif
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
                                     KSyncSock::GetAgentSandeshContext(0));
    nl_free_client(cl);
}

void KSyncMemory::MapSharedMemory() {
    GetTableSize();
    Mmap(false);
}
