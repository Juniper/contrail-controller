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
#include "oper/interface_common.h"
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
    Interface::set_test_mode(false);

    assert(singleton_ == NULL);
    singleton_ = new IntfKSyncObject(table);

    // Get MAC Address for vnsw interface
    GetPhyMac(Agent::GetInstance()->vhost_interface_name().c_str(),
              singleton_->vnsw_if_mac);
}

void IntfKSyncObject::InitTest(InterfaceTable *table) {
    Interface::set_test_mode(true);
    assert(singleton_ == NULL);
    singleton_ = new IntfKSyncObject(table);
    singleton_->test_mode = 1;
}

IntfKSyncEntry::IntfKSyncEntry(const IntfKSyncEntry *entry, uint32_t index) :
    KSyncNetlinkDBEntry(index), ifname_(entry->ifname_), type_(entry->type_),
    intf_id_(entry->intf_id_), vrf_id_(entry->vrf_id_), fd_(kInvalidIndex),
    has_service_vlan_(entry->has_service_vlan_), mac_(entry->mac_),
    ip_(entry->ip_), policy_enabled_(entry->policy_enabled_),
    analyzer_name_(entry->analyzer_name_),
    mirror_direction_(entry->mirror_direction_), 
    l3_active_(false), l2_active_(false),
    os_index_(Interface::kInvalidIndex), network_id_(entry->network_id_),
    sub_type_(entry->sub_type_), ipv4_forwarding_(entry->ipv4_forwarding_),
    layer2_forwarding_(entry->layer2_forwarding_), vlan_id_(entry->vlan_id_),
    parent_(entry->parent_) {
}

IntfKSyncEntry::IntfKSyncEntry(const Interface *intf) :
    KSyncNetlinkDBEntry(kInvalidIndex), ifname_(intf->name()),
    type_(intf->type()), intf_id_(intf->id()), vrf_id_(intf->GetVrfId()),
    fd_(-1), has_service_vlan_(false), mac_(intf->mac()), ip_(0),
    policy_enabled_(false), analyzer_name_(),
    mirror_direction_(Interface::UNKNOWN), l3_active_(false), l2_active_(false),
    os_index_(intf->os_index()), sub_type_(InetInterface::VHOST),
    ipv4_forwarding_(true), layer2_forwarding_(true),
    vlan_id_(VmInterface::kInvalidVlanId), parent_(NULL) {
       
    // Max name size supported by kernel
    assert(strlen(ifname_.c_str()) < IF_NAMESIZE);
    network_id_ = 0;
    if (type_ == Interface::VM_INTERFACE) {
        const VmInterface *vmitf = 
            static_cast<const VmInterface *>(intf);
        if (vmitf->dhcp_snoop_ip()) {
            ip_ = vmitf->ip_addr().to_ulong();
        }
        network_id_ = vmitf->vxlan_id();
        vlan_id_ = vmitf->vlan_id();
        if (vmitf->parent()) {
            IntfKSyncEntry tmp(vmitf->parent());
            parent_ = IntfKSyncObject::GetKSyncObject()->GetReference(&tmp);
        }
    }

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

bool IntfKSyncEntry::Sync(DBEntry *e) {
    Interface *intf = static_cast<Interface *>(e);
    bool ret = false;

    if (l3_active_ != intf->l3_active()) {
        l3_active_ = intf->l3_active();
        ret = true;
    }

    if (l2_active_ != intf->l2_active()) {
        l2_active_ = intf->l2_active();
        ret = true;
    }

    if (os_index_ != intf->os_index()) {
        os_index_ = intf->os_index();
        mac_ = intf->mac();
        ret = true;
    }

    if (intf->type() == Interface::VM_INTERFACE) {
        VmInterface *vm_port = static_cast<VmInterface *>(intf);
        if (vm_port->dhcp_snoop_ip()) {
            if (ip_ != vm_port->ip_addr().to_ulong()) {
                ip_ = vm_port->ip_addr().to_ulong();
                ret = true;
            }
        } else {
            if (ip_) {
                ip_ = 0;
                ret = true;
            }
        }
        if (ipv4_forwarding_ != vm_port->ipv4_forwarding()) {
            ipv4_forwarding_ = vm_port->ipv4_forwarding();
            ret = true;
        }
        if (layer2_forwarding_ != vm_port->layer2_forwarding()) {
            layer2_forwarding_ = vm_port->layer2_forwarding();
            ret = true;
        }
    }

    uint32_t vrf_id = VIF_VRF_INVALID;
    bool policy_enabled = false;
    std::string analyzer_name;    
    Interface::MirrorDirection mirror_direction = Interface::UNKNOWN;
    bool has_service_vlan = false;
    if (l2_active_ || l3_active_) {
        vrf_id = intf->GetVrfId();
        if (vrf_id == VrfEntry::kInvalidIndex) {
            vrf_id = VIF_VRF_INVALID;
        }

        if (intf->type() == Interface::VM_INTERFACE) {
            VmInterface *vm_port = static_cast<VmInterface *>(intf);
            has_service_vlan = vm_port->HasServiceVlan();
            // Policy is not supported on service-vm interfaces.
            // So, disable policy if service-vlan interface
            if (has_service_vlan) {
                policy_enabled = false;
            } else {
                policy_enabled = vm_port->policy_enabled();
            }
            analyzer_name = vm_port->GetAnalyzer();
            mirror_direction = vm_port->mirror_direction();
            if (network_id_ != vm_port->vxlan_id()) {
                network_id_ = vm_port->vxlan_id();
                ret = true;
            }
        }
    }

    if (intf->type() == Interface::INET) {
        InetInterface *vhost = static_cast<InetInterface *>(intf);
        sub_type_ = vhost->sub_type();
    }

    if (vrf_id != vrf_id_) {
        vrf_id_ = vrf_id;
        ret = true;
    }

    if (policy_enabled_ != policy_enabled) {
        policy_enabled_ = policy_enabled;
        ret = true;
    }

    if (analyzer_name_ != analyzer_name) {
        analyzer_name_ = analyzer_name;
        ret = true;
    }

    if (mirror_direction_ != mirror_direction) {
        mirror_direction_ = mirror_direction;
        ret = true;
    }

    if (has_service_vlan_ != has_service_vlan) {
        has_service_vlan_ = has_service_vlan;
        ret = true;
    }

    return ret;
}

KSyncEntry *IntfKSyncEntry::UnresolvedReference() {
    if (type_ != Interface::VM_INTERFACE) {
        return NULL;
    }

    if (vlan_id_ == VmInterface::kInvalidVlanId)
        return NULL;

    if (!parent_->IsResolved()) {
        return parent_.get();
    }

    return NULL;
}

int IntfKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_interface_req encoder;
    int encode_len, error;

    // Dont send message if interface index not known
    if (os_index_ == Interface::kInvalidIndex) {
        return 0;
    }

    uint32_t flags = 0;
    encoder.set_h_op(op);
    switch (type_) {
    case Interface::VM_INTERFACE: {
        std::vector<int8_t> intf_mac(agent_vrrp_mac, agent_vrrp_mac + ETHER_ADDR_LEN);
        encoder.set_vifr_mac(intf_mac);
        if (layer2_forwarding_) {
            flags |= VIF_FLAG_L2_ENABLED;
        }
        if (vlan_id_ == VmInterface::kInvalidVlanId) {
            encoder.set_vifr_type(VIF_TYPE_VIRTUAL);
        } else {
            encoder.set_vifr_type(VIF_TYPE_VLAN);
            encoder.set_vifr_vlan_id(vlan_id_);
            encoder.set_vifr_parent_vif_idx
                (static_cast<IntfKSyncEntry *>(parent_.get())->id());
        }
        break;
    }

    case Interface::PHYSICAL: {
        encoder.set_vifr_type(VIF_TYPE_PHYSICAL); 
        std::vector<int8_t> intf_mac(GetMac(), GetMac() + ETHER_ADDR_LEN);
        encoder.set_vifr_mac(intf_mac);
        flags |= VIF_FLAG_L3_ENABLED;
        // flags |= VIF_FLAG_POLICY_ENABLED;
        break;
    }

    case Interface::INET: {
        switch (sub_type_) {
        case InetInterface::GATEWAY:
            encoder.set_vifr_type(VIF_TYPE_GATEWAY);
            break;
        case InetInterface::LINK_LOCAL:
            encoder.set_vifr_type(VIF_TYPE_XEN_LL_HOST);
            break;
        default:
            encoder.set_vifr_type(VIF_TYPE_HOST); 
            break;

        }
        std::vector<int8_t> intf_mac(GetMac(), GetMac() + ETHER_ADDR_LEN);
        encoder.set_vifr_mac(intf_mac);
        flags |= VIF_FLAG_L3_ENABLED;
        break;
    }

    case Interface::PACKET: {
        encoder.set_vifr_type(VIF_TYPE_AGENT); 
        std::vector<int8_t> intf_mac(agent_vrrp_mac, agent_vrrp_mac + ETHER_ADDR_LEN);
        encoder.set_vifr_mac(intf_mac);
        flags |= VIF_FLAG_L3_ENABLED;
        break;
    }

    default:
        assert(0);
        break;
    }

    if (ipv4_forwarding_) {
        flags |= VIF_FLAG_L3_ENABLED;
        if (policy_enabled_) {
            flags |= VIF_FLAG_POLICY_ENABLED;
        }
        if (!analyzer_name_.empty()) {
            uint16_t idx = MirrorKSyncObject::GetIdx(analyzer_name_);
            if (Interface::MIRROR_TX == mirror_direction_) {
                flags |= VIF_FLAG_MIRROR_TX;
            } else if (Interface::MIRROR_RX == mirror_direction_) {
                flags |= VIF_FLAG_MIRROR_RX;
            } else {
                flags |= VIF_FLAG_MIRROR_RX;
                flags |= VIF_FLAG_MIRROR_TX;
            }
            encoder.set_vifr_mir_id(idx);
        }

        if (has_service_vlan_) {
            flags |= VIF_FLAG_SERVICE_IF;
        }
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
        info.set_l2_active(l2_active_);
        info.set_active(l3_active_);
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
