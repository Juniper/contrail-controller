/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdio.h>
#include <string.h>

#include <net/if.h>
#include <linux/if_tun.h>

#include <boost/asio.hpp>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_sock.h>
#include "ksync/agent_ksync_types.h"
#include "vr_types.h"
#include "vnsw_utils.h"
#include "base/logging.h"
#include "oper/interface.h"
#include "oper/mirror_table.h"
#include "ksync/ksync_index.h"
#include "interface_ksync.h"
#include "vr_interface.h"
#include "vhost.h"
#include "pkt/pkt_handler.h"
#include "ksync/nexthop_ksync.h"
#include "ksync/mirror_ksync.h"

// Name of clone device for creating tap interface
#define TUN_INTF_CLONE_DEV      "/dev/net/tun"
#define SOCK_RETRY_COUNT 4

IntfKSyncObject *IntfKSyncObject::singleton_;

void vr_response::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->SetErrno(ioc->VrResponseMsgHandler(this));
}

void vrouter_ops::Process(SandeshContext *context) {
    assert(0);
}

void IntfKSyncObject::Init(InterfaceTable *table) {
    Interface::SetTestMode(false);

    assert(singleton_ == NULL);
    singleton_ = new IntfKSyncObject(table);

    // Get MAC Address for vnsw interface
    GetPhyMac(Agent::GetInstance()->GetVirtualHostInterfaceName().c_str(),
              singleton_->vnsw_if_mac);
}

void IntfKSyncObject::InitTest(InterfaceTable *table) {
    Interface::SetTestMode(true);
    assert(singleton_ == NULL);
    singleton_ = new IntfKSyncObject(table);
    singleton_->test_mode = 1;
}

KSyncDBObject *IntfKSyncEntry::GetObject() { 
    return IntfKSyncObject::GetKSyncObject();
}

std::string IntfKSyncEntry::ToString() const {
    std::stringstream s;
    s << "Interface : " << ifname_ << " Index : " << intf_id_ 
        << " Vrf : " << vrf_id_;
    return s.str();
}

int IntfKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_interface_req encoder;
    int encode_len, error;

    // Dont send message if interface index not known
    if (os_index_ == Interface::kInvalidIndex) {
        return 0;
    }

    encoder.set_h_op(op);
    switch (type_) {
    case Interface::VMPORT: {
        encoder.set_vifr_type(VIF_TYPE_VIRTUAL); 
        std::vector<int8_t> intf_mac(agent_vrrp_mac, agent_vrrp_mac + ETHER_ADDR_LEN);
        encoder.set_vifr_mac(intf_mac);
        break;
    }

    case Interface::ETH: {
        encoder.set_vifr_type(VIF_TYPE_PHYSICAL); 
        std::vector<int8_t> intf_mac(GetMac(), GetMac() + ETHER_ADDR_LEN);
        encoder.set_vifr_mac(intf_mac);
        break;
    }

    case Interface::VHOST: {
        switch (sub_type_) {
        case VirtualHostInterface::GATEWAY:
            encoder.set_vifr_type(VIF_TYPE_GATEWAY);
            break;
        case VirtualHostInterface::LINK_LOCAL:
            encoder.set_vifr_type(VIF_TYPE_XEN_LL_HOST);
            break;
        default:
            encoder.set_vifr_type(VIF_TYPE_HOST); 
            break;

        }
        std::vector<int8_t> intf_mac(GetMac(), GetMac() + ETHER_ADDR_LEN);
        encoder.set_vifr_mac(intf_mac);
        break;
    }

    case Interface::HOST: {
        encoder.set_vifr_type(VIF_TYPE_AGENT); 
        std::vector<int8_t> intf_mac(agent_vrrp_mac, agent_vrrp_mac + ETHER_ADDR_LEN);
        encoder.set_vifr_mac(intf_mac);
        break;
    }

    default:
        assert(0);
        break;
    }

    uint32_t flags = 0;
    if (policy_enabled_) {
        flags |= VIF_FLAG_POLICY_ENABLED;
    }
    if (!analyzer_name_.empty()) {
        uint16_t idx = MirrorKSyncObject::GetIdx(analyzer_name_);
        flags |= VIF_FLAG_MIRROR_RX;
        flags |= VIF_FLAG_MIRROR_TX;
        encoder.set_vifr_mir_id(idx);
    }

    if (has_service_vlan_) {
        flags |= VIF_FLAG_SERVICE_IF;
    }
    encoder.set_vifr_flags(flags);

    encoder.set_vifr_vrf(vrf_id_);
    encoder.set_vifr_idx(intf_id_);
    encoder.set_vifr_rid(0);
    encoder.set_vifr_os_idx(os_index_);
    encoder.set_vifr_mtu(0);
    encoder.set_vifr_name(ifname_);
    encoder.set_vifr_ip(ip_);

    encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    return encode_len;
}

void IntfKSyncEntry::FillObjectLog(sandesh_op::type op, KSyncIntfInfo &info) {
    info.set_name(ifname_);
    info.set_idx(intf_id_);

    if (op == sandesh_op::ADD) {
        info.set_operation("ADD/CHANGE");
    } else {
        info.set_operation("DELETE");
    }

    if (op == sandesh_op::ADD) {
        info.set_os_idx(os_index_);
        info.set_vrf_id(vrf_id_);
        info.set_active(active_);
        info.set_policy_enabled(policy_enabled_);
        info.set_service_enabled(has_service_vlan_);
        info.set_analyzer_name(analyzer_name_);
    }
}

int IntfKSyncEntry::AddMsg(char *buf, int buf_len) {
    KSyncIntfInfo info;

    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(Intf, info);
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int IntfKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    KSyncIntfInfo info;
    FillObjectLog(sandesh_op::DELETE, info);
    KSYNC_TRACE(Intf, info);
    return Encode(sandesh_op::DELETE, buf, buf_len);
}

int IntfKSyncEntry::ChangeMsg(char *buf, int buf_len) {
    return AddMsg(buf, buf_len);
}

//////////////////////////////////////////////////////////////////////////////
// sub-interface related functions
//////////////////////////////////////////////////////////////////////////////
void GetPhyMac(const char *ifname, char *mac) {
    struct ifreq ifr;

    if (IntfKSyncObject::GetKSyncObject()->GetTestMode()) {
        return;
    }

    int fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(fd >= 0);

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IF_NAMESIZE);
    if (ioctl(fd, SIOCGIFHWADDR, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) << 
            "> quering mac-address for interface <" << ifname << ">");
        assert(0);
    }
    close(fd);

    memcpy(mac, ifr.ifr_hwaddr.sa_data, ETHER_ADDR_LEN);
}

//////////////////////////////////////////////////////////////////////////////
// Snapshot of Kernel Interface data
//////////////////////////////////////////////////////////////////////////////
InterfaceKSnap *InterfaceKSnap::singleton_;

void InterfaceKSnap::Init() {
    assert(singleton_ == NULL);
    singleton_ = new InterfaceKSnap();
}

void InterfaceKSnap::Shutdown() {
    if (singleton_)
        delete singleton_;
    singleton_ = NULL;
}

InterfaceKSnap::InterfaceKSnap() {
    timer_ = TimerManager::CreateTimer(
             *(Agent::GetInstance()->GetEventManager())->io_service(), "InterfaceKSnapTimer");
    timer_->Start(timeout_, boost::bind(&InterfaceKSnap::Reset, this));
}

InterfaceKSnap::~InterfaceKSnap() {
    timer_->Cancel();
    TimerManager::DeleteTimer(timer_);
}

void InterfaceKSnap::KernelInterfaceData(vr_interface_req *r) {
    char name[IF_NAMESIZE + 1];
    tbb::mutex::scoped_lock lock(mutex_);
    if (r->get_vifr_os_idx() >= 0 && r->get_vifr_type() == VIF_TYPE_VIRTUAL) {
        uint32_t ipaddr = r->get_vifr_ip();
        if (ipaddr && if_indextoname(r->get_vifr_os_idx(), name)) {
            std::string itf_name(name);
            data_map_.insert(InterfaceKSnapPair(itf_name, ipaddr));
        }
    }
}

bool InterfaceKSnap::FindInterfaceKSnapData(std::string &name, uint32_t &ip) { 
    tbb::mutex::scoped_lock lock(mutex_);
    InterfaceKSnapIter it = data_map_.find(name);
    if (it != data_map_.end()) {
        ip = it->second;
        return true;
    }
    return false;
}

bool InterfaceKSnap::Reset() { 
    tbb::mutex::scoped_lock lock(mutex_);
    data_map_.clear();
    return false;
}
