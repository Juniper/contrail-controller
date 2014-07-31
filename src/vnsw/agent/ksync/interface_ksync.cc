/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdio.h>
#include <string.h>

#include <net/if.h>
#if defined(__linux__)
#include <linux/if_tun.h>
#endif

#include <boost/asio.hpp>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>
#include "ksync/agent_ksync_types.h"
#include "vr_types.h"
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
    KSyncNetlinkDBEntry(index), analyzer_name_(entry->analyzer_name_),
    dhcp_enable_(entry->dhcp_enable_), fd_(kInvalidIndex),
    flow_key_nh_id_(entry->flow_key_nh_id_),
    has_service_vlan_(entry->has_service_vlan_),
    interface_id_(entry->interface_id_),
    interface_name_(entry->interface_name_), 
    ip_(entry->ip_), ipv4_active_(false),
    ipv4_forwarding_(entry->ipv4_forwarding_),
    ksync_obj_(obj), l2_active_(false),
    layer2_forwarding_(entry->layer2_forwarding_),
    mac_(entry->mac_), mirror_direction_(entry->mirror_direction_), 
    network_id_(entry->network_id_), os_index_(Interface::kInvalidIndex),
    parent_(entry->parent_), policy_enabled_(entry->policy_enabled_),
    sub_type_(entry->sub_type_), type_(entry->type_), vlan_id_(entry->vlan_id_),
    vrf_id_(entry->vrf_id_), persistent_(entry->persistent_),
    xconnect_(entry->xconnect_) {
}

InterfaceKSyncEntry::InterfaceKSyncEntry(InterfaceKSyncObject *obj, 
                                         const Interface *intf) :
    KSyncNetlinkDBEntry(kInvalidIndex), analyzer_name_(),
    dhcp_enable_(true), fd_(-1), flow_key_nh_id_(0),
    has_service_vlan_(false), interface_id_(intf->id()),
    interface_name_(intf->name()), ip_(0), ipv4_active_(false),
    ipv4_forwarding_(true), ksync_obj_(obj), l2_active_(false),
    layer2_forwarding_(true), mac_(),
    mirror_direction_(Interface::UNKNOWN), os_index_(intf->os_index()),
    parent_(NULL), policy_enabled_(false), sub_type_(InetInterface::VHOST),
    type_(intf->type()), vlan_id_(VmInterface::kInvalidVlanId),
    vrf_id_(intf->vrf_id()), persistent_(false), xconnect_(NULL) {

    if (intf->flow_key_nh()) {
        flow_key_nh_id_ = intf->flow_key_nh()->id();
    }
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
    } else if (type_ == Interface::INET) {
        const InetInterface *inet_intf =
        static_cast<const InetInterface *>(intf);
        sub_type_ = inet_intf->sub_type();
        if (sub_type_ == InetInterface::VHOST) {
            InterfaceKSyncEntry tmp(ksync_obj_, inet_intf->xconnect());
            xconnect_ = ksync_obj_->GetReference(&tmp);
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
        ksync_obj_->ksync()->agent()->vrf_table()->FindVrfFromId(vrf_id_);
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
        if (dhcp_enable_ != vm_port->dhcp_enable_config()) {
            dhcp_enable_ = vm_port->dhcp_enable_config();
            ret = true;
        }
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

        KSyncEntryPtr parent = NULL;
        if (vm_port->parent()) {
            InterfaceKSyncEntry tmp(ksync_obj_, vm_port->parent());
            parent = ksync_obj_->GetReference(&tmp);
        }
        if (parent_ != parent) {
            parent_ = parent;
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
            policy_enabled = vm_port->policy_enabled();
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

        InetInterface *inet_interface = static_cast<InetInterface *>(intf);
        if (sub_type_ == InetInterface::VHOST) {
            KSyncEntryPtr xconnect = NULL;
            if (inet_interface->xconnect()) {
                InterfaceKSyncEntry tmp(ksync_obj_, inet_interface->xconnect());
                xconnect = ksync_obj_->GetReference(&tmp);
            }
            if (xconnect_ != xconnect) {
                xconnect_ = xconnect;
                ret = true;
            }
        }
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
    {
#if defined(__linux__)
        memcpy(smac, intf->mac().ether_addr_octet, ETHER_ADDR_LEN);
#elif defined(__FreeBSD__)
        memcpy(smac, intf->mac().octet, ETHER_ADDR_LEN);
#else
#error "Unsupported platform"
#endif
        PhysicalInterface *phy_intf = static_cast<PhysicalInterface *>(intf);
        persistent_ = phy_intf->persistent();
        break;
    }
    case Interface::INET:
#if defined(__linux__)
        memcpy(smac, intf->mac().ether_addr_octet, ETHER_ADDR_LEN);
#elif defined(__FreeBSD__)
        memcpy(smac, intf->mac().octet, ETHER_ADDR_LEN);
#else
#error "Unsupported platform"
#endif
        break;
    default:
        assert(0);
    }

    if (memcmp(smac, mac(), ETHER_ADDR_LEN)) {
#if defined(__linux__)
        memcpy(mac_.ether_addr_octet, smac, ETHER_ADDR_LEN);
#elif defined(__FreeBSD__)
        memcpy(mac_.octet, smac, ETHER_ADDR_LEN);
#else
#error "Unsupported platform"
#endif
        ret = true;
    }

    //Nexthop index gets used in flow key, vrouter just treats
    //it as an index and doesnt cross check against existence of nexthop,
    //hence there is no need to make sure that nexthop is programmed
    //before flow key nexthop index is set in interface
    //Nexthop index 0 if set in interface is treated as invalid index,
    //and packet would be dropped if such a packet needs to go thru
    //flow lookup
    uint32_t nh_id = 0;
    if (intf->flow_key_nh()) {
        nh_id = intf->flow_key_nh()->id();
    }
    if (nh_id != flow_key_nh_id_) {
        flow_key_nh_id_ = nh_id;
        ret = true;
    }

    return ret;
}

KSyncEntry *InterfaceKSyncEntry::UnresolvedReference() {
    if (type_ == Interface::INET && sub_type_ == InetInterface::VHOST) {
        if (xconnect_.get() && !xconnect_->IsResolved()) {
            return xconnect_.get();
        }
    }

    if (type_ != Interface::VM_INTERFACE) {
        return NULL;
    }

    if (vlan_id_ == VmInterface::kInvalidVlanId)
        return NULL;

    if (parent_.get() && !parent_->IsResolved()) {
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
        if (dhcp_enable_) {
            flags |= VIF_FLAG_DHCP_ENABLED;
        }
        if (layer2_forwarding_) {
            flags |= VIF_FLAG_L2_ENABLED;
        }
        int8_t mac[ETHER_ADDR_LEN];
        if (vlan_id_ == VmInterface::kInvalidVlanId) {
            memcpy(mac, ksync_obj_->ksync()->agent()->vrrp_mac(),
                   ETHER_ADDR_LEN);
            encoder.set_vifr_type(VIF_TYPE_VIRTUAL);
        } else {
            encoder.set_vifr_type(VIF_TYPE_VIRTUAL_VLAN);
            encoder.set_vifr_vlan_id(vlan_id_);
            InterfaceKSyncEntry *parent =
                (static_cast<InterfaceKSyncEntry *> (parent_.get()));
            encoder.set_vifr_parent_vif_idx(parent->interface_id());
            memcpy(mac, parent->mac(), ETHER_ADDR_LEN);

        }
        std::vector<int8_t> intf_mac(mac, mac + ETHER_ADDR_LEN);
        encoder.set_vifr_mac(intf_mac);

        break;
    }

    case Interface::PHYSICAL: {
        encoder.set_vifr_type(VIF_TYPE_PHYSICAL); 
        flags |= VIF_FLAG_L3_ENABLED;
        if (!persistent_) {
            flags |= VIF_FLAG_VHOST_PHYS;
        }
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
        case InetInterface::VHOST:
            encoder.set_vifr_type(VIF_TYPE_HOST);
            if (xconnect_.get()) {
                InterfaceKSyncEntry *xconnect = 
                   static_cast<InterfaceKSyncEntry *>(xconnect_.get());
                encoder.set_vifr_cross_connect_idx(xconnect->os_index_); 
            } else {
                encoder.set_vifr_cross_connect_idx(Interface::kInvalidIndex);
            }
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
    encoder.set_vifr_nh_id(flow_key_nh_id_);

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
    KSyncDBObject(), ksync_(ksync) {
}

InterfaceKSyncObject::~InterfaceKSyncObject() {
}

void InterfaceKSyncObject::RegisterDBClients() {
    RegisterDb(ksync_->agent()->interface_table());
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
    ksync_->agent()->set_test_mode(false);
}

void InterfaceKSyncObject::InitTest() {
    ksync_->agent()->set_test_mode(true);
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

