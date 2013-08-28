/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdio.h>
#include <string.h>

#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
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
#include "nl_util.h"
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

static int vnsw_ioctl_sock;
static char vnsw_if_mac[ETHER_ADDR_LEN];
static int test_mode = 0;

IntfKSyncObject *IntfKSyncObject::singleton_;

void vr_response::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->SetErrno(ioc->VrResponseMsgHandler(this));
}

void vrouter_ops::Process(SandeshContext *context) {
    assert(0);
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

char *IntfKSyncEntry::Encode(sandesh_op::type op, int &len) {
    struct nl_client cl;
    vr_interface_req encoder;
    int encode_len, error, ret;
    uint8_t *buf;
    uint32_t buf_len;

    // Dont send message if interface index not known
    if (os_index_ == Interface::kInvalidIndex) {
        return NULL;
    }

    nl_init_generic_client_req(&cl, KSyncSock::GetNetlinkFamilyId());

    if ((ret = nl_build_header(&cl, &buf, &buf_len)) < 0) {
        LOG(DEBUG, "Error creating if message. Error : " << ret);
        return NULL;
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
        if (link_local_) {
            encoder.set_vifr_type(VIF_TYPE_XEN_LL_HOST);
        } else {
            encoder.set_vifr_type(VIF_TYPE_HOST); 
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

    encode_len = encoder.WriteBinary(buf, buf_len, &error);
    nl_update_header(&cl, encode_len);

    len = cl.cl_msg_len;
    return (char *)cl.cl_buf;
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

char *IntfKSyncEntry::AddMsg(int &len) {
    KSyncIntfInfo info;

    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(Intf, info);
    return Encode(sandesh_op::ADD, len);
}

char *IntfKSyncEntry::DeleteMsg(int &len) {
    KSyncIntfInfo info;
    FillObjectLog(sandesh_op::DELETE, info);
    KSYNC_TRACE(Intf, info);
    return Encode(sandesh_op::DELETE, len);
}

char *IntfKSyncEntry::ChangeMsg(int &len) {
    return AddMsg(len);
}

//////////////////////////////////////////////////////////////////////////////
// sub-interface related functions
//////////////////////////////////////////////////////////////////////////////
void PhysicalIntfInit() {
    Interface::SetTestMode(false);

    vnsw_ioctl_sock = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(vnsw_ioctl_sock >= 0);

    // Get MAC Address for vnsw interface
    GetPhyMac(Agent::GetVirtualHostInterfaceName().c_str(), vnsw_if_mac);
}

void PhysicalIntfInitTest() {
    Interface::SetTestMode(true);
    test_mode = 1;
}

const char *PhysicalIntfMac() { 
    return vnsw_if_mac; 
}

void GetPhyMac(const char *ifname, char *mac) {
    struct ifreq ifr;

    if (test_mode) {
        return;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IF_NAMESIZE);
    if (ioctl(vnsw_ioctl_sock, SIOCGIFHWADDR, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) << 
            "> quering mac-address for interface <" << ifname << ">");
        assert(0);
    }
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
             *(Agent::GetEventManager())->io_service(), "InterfaceKSnapTimer");
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
