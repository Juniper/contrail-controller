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
#include "ksync/ksync_init.h"

// Name of clone device for creating tap interface
#define TUN_INTF_CLONE_DEV      "/dev/net/tun"
#define SOCK_RETRY_COUNT 4

InterfaceKSyncEntry::InterfaceKSyncEntry(InterfaceKSyncObject *obj, 
                                         const InterfaceKSyncEntry *entry, 
                                         uint32_t index) :
    KSyncNetlinkDBEntry(index), ksync_obj_(obj), 
    interface_name_(entry->interface_name_), 
    type_(entry->type_), interface_id_(entry->interface_id_), 
    vrf_id_(entry->vrf_id_), fd_(kInvalidIndex),
    has_service_vlan_(entry->has_service_vlan_), mac_(entry->mac_),
    ip_(entry->ip_), policy_enabled_(entry->policy_enabled_),
    analyzer_name_(entry->analyzer_name_),
    mirror_direction_(entry->mirror_direction_), 
    ipv4_active_(false), l2_active_(false),
    os_index_(Interface::kInvalidIndex), network_id_(entry->network_id_),
    sub_type_(entry->sub_type_), ipv4_forwarding_(entry->ipv4_forwarding_),
    layer2_forwarding_(entry->layer2_forwarding_), vlan_id_(entry->vlan_id_),
    parent_(entry->parent_) {
}

InterfaceKSyncEntry::InterfaceKSyncEntry(InterfaceKSyncObject *obj, 
                                         const Interface *intf) :
    KSyncNetlinkDBEntry(kInvalidIndex), ksync_obj_(obj), 
    interface_name_(intf->name()),
    type_(intf->type()), interface_id_(intf->id()), vrf_id_(intf->vrf_id()),
    fd_(-1), has_service_vlan_(false), mac_(), ip_(0),
    policy_enabled_(false), analyzer_name_(),
    mirror_direction_(Interface::UNKNOWN), ipv4_active_(false), l2_active_(false),
    os_index_(intf->os_index()), sub_type_(InetInterface::VHOST),
    ipv4_forwarding_(true), layer2_forwarding_(true),
    vlan_id_(VmInterface::kInvalidVlanId), parent_(NULL) {
       
    network_id_ = 0;
    if (type_ == Interface::VM_INTERFACE) {
        const VmInterface *vmitf = 
            static_cast<const VmInterface *>(intf);
        if (vmitf->do_dhcp_relay()) {
            ip_ = vmitf->ip_addr().to_ulong();
        }
        network_id_ = vmitf->vxlan_id();
        vlan_id_ = vmitf->vlan_id();
        if (vmitf->parent()) {
            InterfaceKSyncEntry tmp(ksync_obj_, vmitf->parent());
            parent_ = ksync_obj_->GetReference(&tmp);
        }
    }
}

InterfaceKSyncEntry::~InterfaceKSyncEntry() {
}

KSyncDBObject *InterfaceKSyncEntry::GetObject() { 
    return ksync_obj_;
}

bool InterfaceKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const InterfaceKSyncEntry &entry = static_cast
        <const InterfaceKSyncEntry &>(rhs);
    return interface_name_ < entry.interface_name_;
}

std::string InterfaceKSyncEntry::ToString() const {
    std::stringstream s;
    s << "Interface : " << interface_name_ << " Index : " << interface_id_;
    const VrfEntry* vrf =
        ksync_obj_->ksync()->agent()->GetVrfTable()->FindVrfFromId(vrf_id_);
    if (vrf) {
        s << " Vrf : " << vrf->GetName();
    }
    return s.str();
}

bool InterfaceKSyncEntry::Sync(DBEntry *e) {
    Interface *intf = static_cast<Interface *>(e);
    bool ret = false;

    if (ipv4_active_ != intf->ipv4_active()) {
        ipv4_active_ = intf->ipv4_active();
        ret = true;
    }

    if (l2_active_ != intf->l2_active()) {
        l2_active_ = intf->l2_active();
        ret = true;
    }

    if (os_index_ != intf->os_index()) {
        os_index_ = intf->os_index();
        ret = true;
    }

    if (intf->type() == Interface::VM_INTERFACE) {
        VmInterface *vm_port = static_cast<VmInterface *>(intf);
        if (vm_port->do_dhcp_relay()) {
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
    if (l2_active_ || ipv4_active_) {
        vrf_id = intf->vrf_id();
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

    InterfaceTable *table = static_cast<InterfaceTable *>(intf->get_table());
    uint8_t smac[ETHER_ADDR_LEN];

    switch (intf->type()) {
    case Interface::VM_INTERFACE:
    case Interface::PACKET:
        memcpy(smac, table->agent()->vrrp_mac(), ETHER_ADDR_LEN);
        break;

    case Interface::PHYSICAL:
    case Interface::INET:
        memcpy(smac, intf->mac().ether_addr_octet, ETHER_ADDR_LEN);
        break;
    default:
        assert(0);
    }

    if (memcmp(smac, mac(), ETHER_ADDR_LEN)) {
        memcpy(mac_.ether_addr_octet, smac, ETHER_ADDR_LEN);
        ret = true;
    }

    return ret;
}

KSyncEntry *InterfaceKSyncEntry::UnresolvedReference() {
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

bool IsValidOsIndex(size_t os_index, Interface::Type type, uint16_t vlan_id) {
    if (os_index != Interface::kInvalidIndex)
        return true;

    if (type == Interface::VM_INTERFACE &&
        vlan_id != VmInterface::kInvalidVlanId) {
        return true;
    }

    return false;
}

int InterfaceKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_interface_req encoder;
    int encode_len, error;

    // Dont send message if interface index not known
    if (IsValidOsIndex(os_index_, type_, vlan_id_) == false) {
        return 0;
    }

    uint32_t flags = 0;
    encoder.set_h_op(op);
    switch (type_) {
    case Interface::VM_INTERFACE: {
        if (layer2_forwarding_) {
            flags |= VIF_FLAG_L2_ENABLED;
        }
        if (vlan_id_ == VmInterface::kInvalidVlanId) {
            encoder.set_vifr_type(VIF_TYPE_VIRTUAL);
        } else {
            encoder.set_vifr_type(VIF_TYPE_VLAN);
            encoder.set_vifr_vlan_id(vlan_id_);
            encoder.set_vifr_parent_vif_idx
                (static_cast<InterfaceKSyncEntry *>
                     (parent_.get())->interface_id());
        }
        break;
    }

    case Interface::PHYSICAL: {
        encoder.set_vifr_type(VIF_TYPE_PHYSICAL); 
        flags |= VIF_FLAG_L3_ENABLED;
        break;
    }

    case Interface::INET: {
        switch (sub_type_) {
        case InetInterface::SIMPLE_GATEWAY:
            encoder.set_vifr_type(VIF_TYPE_GATEWAY);
            break;
        case InetInterface::LINK_LOCAL:
            encoder.set_vifr_type(VIF_TYPE_XEN_LL_HOST);
            break;
        default:
            encoder.set_vifr_type(VIF_TYPE_HOST); 
            break;

        }
        flags |= VIF_FLAG_L3_ENABLED;
        break;
    }

    case Interface::PACKET: {
        encoder.set_vifr_type(VIF_TYPE_AGENT); 
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
        MirrorKSyncObject* obj = ksync_obj_->ksync()->mirror_ksync_obj();
        if (!analyzer_name_.empty()) {
            uint16_t idx = obj->GetIdx(analyzer_name_);
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

    encoder.set_vifr_mac(std::vector<int8_t>(mac(), mac() + ETHER_ADDR_LEN));
    encoder.set_vifr_flags(flags);

    encoder.set_vifr_vrf(vrf_id_);
    encoder.set_vifr_idx(interface_id_);
    encoder.set_vifr_rid(0);
    encoder.set_vifr_os_idx(os_index_);
    encoder.set_vifr_mtu(0);
    encoder.set_vifr_name(interface_name_);
    encoder.set_vifr_ip(ip_);

    encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    return encode_len;
}

void InterfaceKSyncEntry::FillObjectLog(sandesh_op::type op, 
                                   KSyncIntfInfo &info) const {
    info.set_name(interface_name_);
    info.set_idx(interface_id_);

    if (op == sandesh_op::ADD) {
        info.set_operation("ADD/CHANGE");
    } else {
        info.set_operation("DELETE");
    }

    if (op == sandesh_op::ADD) {
        info.set_os_idx(os_index_);
        info.set_vrf_id(vrf_id_);
        info.set_l2_active(l2_active_);
        info.set_active(ipv4_active_);
        info.set_policy_enabled(policy_enabled_);
        info.set_service_enabled(has_service_vlan_);
        info.set_analyzer_name(analyzer_name_);
    }
}

int InterfaceKSyncEntry::AddMsg(char *buf, int buf_len) {
    KSyncIntfInfo info;

    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(Intf, info);
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int InterfaceKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    KSyncIntfInfo info;
    FillObjectLog(sandesh_op::DELETE, info);
    KSYNC_TRACE(Intf, info);
    return Encode(sandesh_op::DELETE, buf, buf_len);
}

int InterfaceKSyncEntry::ChangeMsg(char *buf, int buf_len) {
    return AddMsg(buf, buf_len);
}

InterfaceKSyncObject::InterfaceKSyncObject(KSync *ksync) :
    KSyncDBObject(), ksync_(ksync), test_mode(false) {
}

InterfaceKSyncObject::~InterfaceKSyncObject() {
}

void InterfaceKSyncObject::RegisterDBClients() {
    RegisterDb(ksync_->agent()->GetInterfaceTable());
}

KSyncEntry *InterfaceKSyncObject::Alloc(const KSyncEntry *entry, 
                                        uint32_t index) {
    const InterfaceKSyncEntry *intf = 
        static_cast<const InterfaceKSyncEntry *>(entry);
    InterfaceKSyncEntry *ksync = new InterfaceKSyncEntry(this, intf, index);
    return static_cast<KSyncEntry *>(ksync);
}

KSyncEntry *InterfaceKSyncObject::DBToKSyncEntry(const DBEntry *e) {
    const Interface *intf = static_cast<const Interface *>(e);
    InterfaceKSyncEntry *key = NULL;

    switch (intf->type()) {
        case Interface::PHYSICAL:
        case Interface::VM_INTERFACE:
        case Interface::PACKET:
        case Interface::INET:
            key = new InterfaceKSyncEntry(this, intf);
            break;

        default:
            assert(0);
            break;
    }
    return static_cast<KSyncEntry *>(key);
}

void InterfaceKSyncObject::Init() {
    // Get MAC Address for vnsw interface
    test_mode = 0;
    Interface::set_test_mode(false);
}

void InterfaceKSyncObject::InitTest() {
    test_mode = 1;
    Interface::set_test_mode(true);
}

//////////////////////////////////////////////////////////////////////////////
// sandesh routines
//////////////////////////////////////////////////////////////////////////////
void vr_response::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->SetErrno(ioc->VrResponseMsgHandler(this));
}

void vrouter_ops::Process(SandeshContext *context) {
    assert(0);
}

