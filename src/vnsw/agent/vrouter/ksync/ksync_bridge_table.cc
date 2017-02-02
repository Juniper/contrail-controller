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
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>
#include <ksync/ksync_sock_user.h>

#include <vr_types.h>
#include <nl_util.h>
#include <vr_bridge.h>
#include <vr_genetlink.h>

#include "ksync_init.h"
#include "ksync_bridge_table.h"
#include "sandesh_ksync.h"

using namespace boost::asio::ip;
static const uint32_t kTestBridgeTableSize = 65535;

KSyncBridgeMemory::KSyncBridgeMemory(KSync *ksync, uint32_t minor_id):
    KSyncMemory(ksync, minor_id) {
    table_path_ = "/dev/vr_bridge";
}

KSyncBridgeMemory::~KSyncBridgeMemory() {
}

int KSyncBridgeMemory::EncodeReq(nl_client *cl, uint32_t attr_len) {
    vr_bridge_table_data info;
    int encode_len, error;

    info.set_btable_op(sandesh_op::GET);
    info.set_btable_size(0);
    info.set_btable_dev(0);
    info.set_btable_file_path("");

    encode_len = info.WriteBinary(nl_get_buf_ptr(cl) + attr_len,
                                 nl_get_buf_len(cl), &error);
    return encode_len;
}

bool KSyncBridgeMemory::IsInactiveEntry(uint32_t idx, uint8_t &gen_id) {
    return false;
}

void KSyncBridgeMemory::CreateProtoAuditEntry(uint32_t idx, uint8_t gen_id) {

}

vr_bridge_entry*
KSyncBridgeMemory::GetBridgeEntry(uint32_t idx) {
    if (idx >= table_entries_count_) {
        assert(0);
    }
    return &bridge_table_[idx];
}

void KSyncBridgeMemory::InitTest() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    table_ = sock->BridgeMmapAlloc(kTestBridgeTableSize);
    table_entries_count_ = kTestBridgeTableSize / get_entry_size();
    audit_yield_ = table_entries_count_;
    audit_timeout_ = 10; // timout immediately.
    SetTableSize();
}

void KSyncBridgeMemory::Shutdown() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    sock->BridgeMmapFree();
}

int KSyncBridgeMemory::get_entry_size() {
    return sizeof(vr_bridge_entry);
}

void KSyncBridgeMemory::SetTableSize() {
    bridge_table_ = static_cast<vr_bridge_entry *>(table_);
}

void vr_bridge_table_data::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->BridgeTableInfoHandler(this);
}
