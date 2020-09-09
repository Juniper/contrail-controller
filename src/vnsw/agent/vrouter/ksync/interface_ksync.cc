/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdio.h>
#include <string.h>

#include <net/if.h>

#include <boost/asio.hpp>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>
#include <vrouter/ksync/ksync_agent_sandesh.h>
#include <init/agent_param.h>
#include "vrouter/ksync/agent_ksync_types.h"
#include "vr_types.h"
#include "base/logging.h"
#include "oper/interface_common.h"
#include "oper/mirror_table.h"
#include "ksync/ksync_index.h"
#include "interface_ksync.h"
#include "vr_interface.h"
#include "vhost.h"
#include "pkt/pkt_handler.h"
#include "vrouter/ksync/nexthop_ksync.h"
#include "vrouter/ksync/mirror_ksync.h"
#include "vrouter/ksync/ksync_init.h"

// Name of clone device for creating tap interface
#define TUN_INTF_CLONE_DEV      "/dev/net/tun"
#define SOCK_RETRY_COUNT 4

#define VIF_FAT_FLOW_ENCODE_AGGR_PROTO_PORT(prefix_aggr, ignore_addr, protocol, port) \
                           (((prefix_aggr) << 28) | \
                            ((ignore_addr) << 24) | \
                            ((protocol) << 16) | \
                            (port))

InterfaceKSyncEntry::InterfaceKSyncEntry(InterfaceKSyncObject *obj,
                                         const InterfaceKSyncEntry *entry,
                                         uint32_t index) :
    KSyncNetlinkDBEntry(index), analyzer_name_(entry->analyzer_name_),
    drop_new_flows_(entry->drop_new_flows_),
    dhcp_enable_(entry->dhcp_enable_),
    fd_(kInvalidIndex),
    flow_key_nh_id_(entry->flow_key_nh_id_),
    has_service_vlan_(entry->has_service_vlan_),
    interface_id_(entry->interface_id_),
    interface_name_(entry->interface_name_),
    ip_(entry->ip_), primary_ip6_(entry->primary_ip6_),
    hc_active_(false), ipv4_active_(false),
    layer3_forwarding_(entry->layer3_forwarding_),
    ksync_obj_(obj), l2_active_(false),
    metadata_l2_active_(entry->metadata_l2_active_),
    metadata_ip_active_(entry->metadata_ip_active_),
    bridging_(entry->bridging_),
    proxy_arp_mode_(VmInterface::PROXY_ARP_NONE),
    mac_(entry->mac_),
    smac_(entry->smac_),
    mirror_direction_(entry->mirror_direction_),
    network_id_(entry->network_id_),
    os_index_(Interface::kInvalidIndex),
    parent_(entry->parent_),
    policy_enabled_(entry->policy_enabled_),
    sub_type_(entry->sub_type_),
    vmi_device_type_(entry->vmi_device_type_),
    vmi_type_(entry->vmi_type_),
    hbs_intf_type_(entry->hbs_intf_type_),
    type_(entry->type_),
    rx_vlan_id_(entry->rx_vlan_id_),
    tx_vlan_id_(entry->tx_vlan_id_),
    vrf_id_(entry->vrf_id_),
    multicast_vrf_id_(entry->multicast_vrf_id_),
    persistent_(entry->persistent_),
    subtype_(entry->subtype_),
    xconnect_(entry->xconnect_),
    no_arp_(entry->no_arp_),
    encap_type_(entry->encap_type_),
    display_name_(entry->display_name_),
    transport_(entry->transport_),
    flood_unknown_unicast_ (entry->flood_unknown_unicast_),
    qos_config_(entry->qos_config_),
    learning_enabled_(entry->learning_enabled_),
    isid_(entry->isid_), pbb_cmac_vrf_(entry->pbb_cmac_vrf_),
    pbb_mac_(entry->pbb_mac_), etree_leaf_(entry->etree_leaf_),
    pbb_interface_(entry->pbb_interface_),
    vhostuser_mode_(entry->vhostuser_mode_),
    igmp_enable_(entry->igmp_enable_),
    mac_ip_learning_enable_(false),
    os_guid_(entry->os_guid_) {
}

InterfaceKSyncEntry::InterfaceKSyncEntry(InterfaceKSyncObject *obj,
                                         const Interface *intf) :
    KSyncNetlinkDBEntry(kInvalidIndex),
    analyzer_name_(),
    drop_new_flows_(false),
    dhcp_enable_(true),
    fd_(-1),
    flow_key_nh_id_(0),
    has_service_vlan_(false),
    interface_id_(intf->id()),
    interface_name_(intf->name()),
    ip_(0),
    hc_active_(false),
    ipv4_active_(false),
    layer3_forwarding_(true),
    ksync_obj_(obj),
    l2_active_(false),
    metadata_l2_active_(false),
    metadata_ip_active_(false),
    bridging_(true),
    proxy_arp_mode_(VmInterface::PROXY_ARP_NONE),
    mac_(),
    smac_(),
    mirror_direction_(Interface::UNKNOWN),
    os_index_(intf->os_index()),
    parent_(NULL),
    policy_enabled_(false),
    sub_type_(InetInterface::VHOST),
    vmi_device_type_(VmInterface::DEVICE_TYPE_INVALID),
    vmi_type_(VmInterface::VMI_TYPE_INVALID),
    hbs_intf_type_(VmInterface::HBS_INTF_INVALID),
    type_(intf->type()),
    rx_vlan_id_(VmInterface::kInvalidVlanId),
    tx_vlan_id_(VmInterface::kInvalidVlanId),
    vrf_id_(intf->vrf_id()),
    multicast_vrf_id_(VrfEntry::kInvalidIndex),
    persistent_(false),
    subtype_(PhysicalInterface::INVALID),
    xconnect_(NULL),
    no_arp_(false),
    encap_type_(PhysicalInterface::ETHERNET),
    transport_(Interface::TRANSPORT_INVALID),
    flood_unknown_unicast_(false), qos_config_(NULL),
    learning_enabled_(false), isid_(VmInterface::kInvalidIsid),
    pbb_cmac_vrf_(VrfEntry::kInvalidIndex), pbb_mac_(), etree_leaf_(false),
    pbb_interface_(false), vhostuser_mode_(VmInterface::vHostUserClient),
    igmp_enable_(false), mac_ip_learning_enable_(false),
    os_guid_(intf->os_guid()) {

    if (intf->flow_key_nh()) {
        flow_key_nh_id_ = intf->flow_key_nh()->id();
    }
    network_id_ = 0;
    if (type_ == Interface::VM_INTERFACE) {
        const VmInterface *vmitf =
            static_cast<const VmInterface *>(intf);
        ip_ = vmitf->primary_ip_addr().to_ulong();
        primary_ip6_ = vmitf->primary_ip6_addr();
        network_id_ = vmitf->vxlan_id();
        rx_vlan_id_ = vmitf->rx_vlan_id();
        tx_vlan_id_ = vmitf->tx_vlan_id();
        vhostuser_mode_ = vmitf->vhostuser_mode();
        if (vmitf->parent()) {
            InterfaceKSyncEntry tmp(ksync_obj_, vmitf->parent());
            parent_ = ksync_obj_->GetReference(&tmp);
        }
        vmi_device_type_ = vmitf->device_type();
        vmi_type_ = vmitf->vmi_type();
        if (vmi_type_ == VmInterface::VHOST) {
            InterfaceKSyncEntry tmp(ksync_obj_, vmitf->parent());
            xconnect_ = ksync_obj_->GetReference(&tmp);
            parent_ = NULL;
            InterfaceKSyncEntry *xconnect = static_cast<InterfaceKSyncEntry *>
                (xconnect_.get());
            encap_type_ = xconnect->encap_type();
            no_arp_ = xconnect->no_arp();
        }
        hbs_intf_type_ = vmitf->hbs_intf_type();
    } else if (type_ == Interface::INET) {
        const InetInterface *inet_intf =
        static_cast<const InetInterface *>(intf);
        sub_type_ = inet_intf->sub_type();
        ip_ = inet_intf->ip_addr().to_ulong();
        if (sub_type_ == InetInterface::VHOST) {
            InterfaceKSyncEntry tmp(ksync_obj_, inet_intf->xconnect());
            xconnect_ = ksync_obj_->GetReference(&tmp);
            InterfaceKSyncEntry *xconnect = static_cast<InterfaceKSyncEntry *>
                (xconnect_.get());
            encap_type_ = xconnect->encap_type();
            no_arp_ = xconnect->no_arp();
        }
    } else if (type_ == Interface::PHYSICAL) {
        const PhysicalInterface *physical_intf =
            static_cast<const PhysicalInterface *>(intf);
        encap_type_ = physical_intf->encap_type();
        no_arp_ = physical_intf->no_arp();
        display_name_ = physical_intf->display_name();
        ip_ = physical_intf->ip_addr().to_ulong();
    }
}

InterfaceKSyncEntry::~InterfaceKSyncEntry() {
}

KSyncDBObject *InterfaceKSyncEntry::GetObject() const {
    return ksync_obj_;
}

bool InterfaceKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const InterfaceKSyncEntry &entry = static_cast
        <const InterfaceKSyncEntry &>(rhs);
    if (interface_name_ != entry.interface_name_ ) {
        return interface_name_ < entry.interface_name_;
    } else {
        return interface_id_ < entry.interface_id_;
    }
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

    if (hc_active_ != intf->is_hc_active()) {
        hc_active_ = intf->is_hc_active();
        ret = true;
    }

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
        if (vmi_device_type_ != vm_port->device_type()) {
            vmi_device_type_ = vm_port->device_type();
            ret = true;
        }

        if (vmi_type_ != vm_port->vmi_type()) {
            vmi_type_ = vm_port->vmi_type();
            ret = true;
        }

        if (hbs_intf_type_ != vm_port->hbs_intf_type()) {
            hbs_intf_type_ = vm_port->hbs_intf_type();
            ret = true;
        }

        if (drop_new_flows_ != vm_port->drop_new_flows()) {
            drop_new_flows_ = vm_port->drop_new_flows();
            ret = true;
        }

        if (dhcp_enable_ != vm_port->dhcp_enabled()) {
            dhcp_enable_ = vm_port->dhcp_enabled();
            ret = true;
        }

        if (ip_ != vm_port->primary_ip_addr().to_ulong()) {
            ip_ = vm_port->primary_ip_addr().to_ulong();
            ret = true;
        }

         if (primary_ip6_ != vm_port->primary_ip6_addr()) {
             primary_ip6_ = vm_port->primary_ip6_addr();
             ret = true;
        }

         if (layer3_forwarding_ != vm_port->layer3_forwarding()) {
            layer3_forwarding_ = vm_port->layer3_forwarding();
            ret = true;
        }

        if (flood_unknown_unicast_ != vm_port->flood_unknown_unicast()) {
            flood_unknown_unicast_ = vm_port->flood_unknown_unicast();
            ret = true;
        }

        if (bridging_ != vm_port->bridging()) {
            bridging_ = vm_port->bridging();
            ret = true;
        }

        if (proxy_arp_mode_ != vm_port->proxy_arp_mode()) {
            proxy_arp_mode_ = vm_port->proxy_arp_mode();
            ret = true;
        }

        if (rx_vlan_id_ != vm_port->rx_vlan_id()) {
            rx_vlan_id_ = vm_port->rx_vlan_id();
            ret = true;
        }

        if (tx_vlan_id_ != vm_port->tx_vlan_id()) {
            tx_vlan_id_ = vm_port->tx_vlan_id();
            ret = true;
        }

        if (vhostuser_mode_ != vm_port->vhostuser_mode()) {
            vhostuser_mode_ = vm_port->vhostuser_mode();
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

        if (metadata_l2_active_ !=
            vm_port->metadata_l2_active()) {
            metadata_l2_active_ =
                vm_port->metadata_l2_active();
            ret = true;
        }

        if (metadata_ip_active_ !=
            vm_port->metadata_ip_active()) {
            metadata_ip_active_ =
                vm_port->metadata_ip_active();
            ret = true;
        }

        if (learning_enabled_ != vm_port->learning_enabled()) {
            learning_enabled_ = vm_port->learning_enabled();
            ret = true;
        }

        if (pbb_interface_ != vm_port->pbb_interface()) {
            pbb_interface_ = vm_port->pbb_interface();
            ret = true;
        }

        if (l2_active_ && pbb_interface_ && isid_ != vm_port->GetIsid()) {
            isid_ = vm_port->GetIsid();
            ret = true;
        }

        if (l2_active_ && pbb_interface_ &&
            pbb_cmac_vrf_ != vm_port->GetPbbVrf()) {
            pbb_cmac_vrf_ = vm_port->GetPbbVrf();
            ret = true;
        }

        if (etree_leaf_ != vm_port->etree_leaf()) {
            etree_leaf_ = vm_port->etree_leaf();
            ret = true;
        }

        if (igmp_enable_ != vm_port->igmp_enabled()) {
            igmp_enable_ = vm_port->igmp_enabled();
            ret = true;
        }

        if (mac_ip_learning_enable_ != vm_port->mac_ip_learning_enable()) {
            mac_ip_learning_enable_ = vm_port->mac_ip_learning_enable();
            ret = true;
        }
    }

    uint32_t vrf_id = VIF_VRF_INVALID;
    uint32_t multicast_vrf_id = VIF_VRF_INVALID;
    bool policy_enabled = false;
    std::string analyzer_name;
    Interface::MirrorDirection mirror_direction = Interface::UNKNOWN;
    bool has_service_vlan = false;
    if (l2_active_ || ipv4_active_ || metadata_l2_active_ || metadata_ip_active_) {
        vrf_id = intf->vrf_id();
        if (vrf_id == VrfEntry::kInvalidIndex) {
            vrf_id = VIF_VRF_INVALID;
        }

        if (intf->type() == Interface::VM_INTERFACE) {
            VmInterface *vm_port = static_cast<VmInterface *>(intf);
            if (vm_port->forwarding_vrf()) {
                vrf_id = vm_port->forwarding_vrf()->vrf_id();
            }

            if (vmi_type_ != VmInterface::VHOST) {
                multicast_vrf_id = intf->vrf_id();
                if (pbb_interface_ && l2_active_) {
                    multicast_vrf_id =  vm_port->GetPbbVrf();;
                }

                if (multicast_vrf_id == VrfEntry::kInvalidIndex) {
                    multicast_vrf_id = VIF_VRF_INVALID;
                }
            }

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

        if (ip_ != vhost->ip_addr().to_ulong()) {
            ip_ = vhost->ip_addr().to_ulong();
            ret = true;
        }

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

    KSyncEntryPtr qos_config = NULL;
    QosConfigKSyncObject *qos_object =
        static_cast<InterfaceKSyncObject *>(ksync_obj_)->ksync()->
        qos_config_ksync_obj();
    if (intf->qos_config() != NULL) {
        QosConfigKSyncEntry tmp(qos_object, intf->qos_config());
        qos_config = qos_object->GetReference(&tmp);
    }
    if (qos_config != qos_config_) {
        qos_config_ = qos_config;
        ret = true;
    }

    if (vrf_id != vrf_id_) {
        vrf_id_ = vrf_id;
        ret = true;
    }

    if (multicast_vrf_id_ != multicast_vrf_id) {
        multicast_vrf_id_ = multicast_vrf_id;
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
    MacAddress dmac;

    switch (intf->type()) {
    case Interface::VM_INTERFACE:
    {
        const VmInterface *vm_intf = static_cast<const VmInterface *>(intf);
        if (fat_flow_list_.list_ != vm_intf->fat_flow_list().list_) {
            fat_flow_list_ = vm_intf->fat_flow_list();
            ret = true;
        }
        vm_intf->BuildFatFlowExcludeList(&fat_flow_exclude_list_);
        std::set<MacAddress> aap_mac_list;
        for (VmInterface::AllowedAddressPairSet::iterator aap_it =
             vm_intf->allowed_address_pair_list().list_.begin();
             aap_it != vm_intf->allowed_address_pair_list().list_.end();
             ++aap_it) {
            aap_mac_list.insert(aap_it->mac_);
        }
        if (aap_mac_list_ != aap_mac_list) {
            aap_mac_list_.swap(aap_mac_list);
            ret = true;
        }
        MacAddress pbb_mac;
        if (pbb_interface_) {
            pbb_mac = MacAddress(vm_intf->vm_mac());
        }
        if (pbb_mac != pbb_mac_) {
            pbb_mac_ = pbb_mac;
            ret = true;
        }
    }
    case Interface::PACKET:
        dmac = table->agent()->vrrp_mac();
        break;

    case Interface::PHYSICAL:
    {
        dmac = intf->mac();
        PhysicalInterface *phy_intf = static_cast<PhysicalInterface *>(intf);
        persistent_ = phy_intf->persistent();
        subtype_ = phy_intf->subtype();
        break;
    }
    case Interface::INET: {
        dmac = intf->mac();

        bool no_arp = false;
        PhysicalInterface::EncapType encap = PhysicalInterface::ETHERNET;
        InterfaceKSyncEntry *xconnect = static_cast<InterfaceKSyncEntry *>
            (xconnect_.get());
        if (xconnect) {
            no_arp = xconnect->no_arp();
            encap = xconnect->encap_type();
        }

        if (no_arp_ != no_arp) {
            no_arp_ = no_arp;
            ret = true;
        }
        if (encap_type_ != encap) {
            encap_type_ = encap;
            ret = true;
        }

        break;
    }

    case Interface::LOGICAL:
    case Interface::REMOTE_PHYSICAL:
        dmac = intf->mac();
        break;

    default:
        assert(0);
    }

    if (dmac != mac()) {
        mac_ = dmac;
        ret = true;
    }

    // In VMWare VCenter mode, interface is assigned using the SMAC
    // in packet. Store the SMAC for interface
    MacAddress smac;
    if (intf->type() == Interface::VM_INTERFACE &&
        (ksync_obj_->ksync()->agent()->isVmwareVcenterMode() ||
         ksync_obj_->ksync()->agent()->server_gateway_mode() ||
         ksync_obj_->ksync()->agent()->pbb_gateway_mode())) {
        const VmInterface *vm_intf = static_cast<const VmInterface *>(intf);
        smac = MacAddress(vm_intf->vm_mac());
    }

    if (smac != smac_) {
        smac_ = smac;
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

    if (transport_ != intf->transport()) {
        transport_ = intf->transport();
        ret = true;
    }

    if (os_guid_ != intf->os_guid()) {
        os_guid_ = intf->os_guid();
        ret = true;
    }

    return ret;
}

KSyncEntry *InterfaceKSyncEntry::UnresolvedReference() {
    if (qos_config_.get() && qos_config_->IsResolved() == false) {
        return qos_config_.get();
    }

    if (type_ == Interface::INET && sub_type_ == InetInterface::VHOST) {
        if (xconnect_.get() && !xconnect_->IsResolved()) {
            return xconnect_.get();
        }
    }

    if (type_ != Interface::VM_INTERFACE) {
        return NULL;
    }

    if (parent_.get() && !parent_->IsResolved()) {
        return parent_.get();
    }

    if (!analyzer_name_.empty()) {
        MirrorKSyncObject *mirror_object =
                         ksync_obj_->ksync()->mirror_ksync_obj();
        MirrorKSyncEntry mksync1(mirror_object, analyzer_name_);
        MirrorKSyncEntry *mirror =
            static_cast<MirrorKSyncEntry *>(mirror_object->GetReference(&mksync1));
        if (mirror && !mirror->IsResolved() &&
            mirror->mirror_index() == MirrorTable::kInvalidIndex) {
            return mirror;
        }
    }
    return NULL;
}

bool IsValidOsIndex(size_t os_index, Interface::Type type, uint16_t vlan_id,
                    VmInterface::VmiType vmi_type, Interface::Transport transport) {
    if (transport != Interface::TRANSPORT_ETHERNET) {
        return true;
    }

    if (os_index != Interface::kInvalidIndex)
        return true;

    if (type == Interface::VM_INTERFACE &&
        vlan_id != VmInterface::kInvalidVlanId) {
        return true;
    }

    if (vmi_type == VmInterface::GATEWAY ||
        vmi_type == VmInterface::REMOTE_VM) {
        return true;
    }

    return false;
}

int InterfaceKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_interface_req encoder;
    int encode_len;
    uint32_t vrf_id = vrf_id_;

    uint32_t flags = 0;
    encoder.set_h_op(op);
    if (op == sandesh_op::DEL) {
        encoder.set_vifr_idx(interface_id_);
        int error = 0;
        encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
        assert(error == 0);
        assert(encode_len <= buf_len);
        return encode_len;
    }

    if (qos_config_.get() != NULL) {
        QosConfigKSyncEntry *qos_config =
            static_cast<QosConfigKSyncEntry *>(qos_config_.get());
        encoder.set_vifr_qos_map_index(qos_config->id());
    } else {
        encoder.set_vifr_qos_map_index(-1);
    }

    if (etree_leaf_ == false) {
        flags |= VIF_FLAG_ETREE_ROOT;
    }

    switch (type_) {
    case Interface::VM_INTERFACE: {
        if (vmi_device_type_ == VmInterface::TOR)
            return 0;
        if (drop_new_flows_) {
            flags |= VIF_FLAG_DROP_NEW_FLOWS;
        }
        if (dhcp_enable_) {
            flags |= VIF_FLAG_DHCP_ENABLED;
        }
        if (bridging_) {
            flags |= VIF_FLAG_L2_ENABLED;
        }
        if (vmi_type_ == VmInterface::GATEWAY) {
            flags |= VIF_FLAG_NO_ARP_PROXY;
        }
        if (hbs_intf_type_ != VmInterface::HBS_INTF_INVALID) {
            flags |= (hbs_intf_type_ == VmInterface::HBS_INTF_RIGHT)?
                VIF_FLAG_HBS_RIGHT:VIF_FLAG_HBS_LEFT;
        }
        if (flood_unknown_unicast_) {
            flags |= VIF_FLAG_UNKNOWN_UC_FLOOD;
        }
        if (learning_enabled_) {
            flags |= VIF_FLAG_MAC_LEARN;
        }
        if (proxy_arp_mode_ == VmInterface::PROXY_ARP_UNRESTRICTED) {
            flags |= VIF_FLAG_MAC_PROXY;
        }
        if (igmp_enable_) {
            flags |= VIF_FLAG_IGMP_ENABLED;
        }
        if (mac_ip_learning_enable_) {
            flags |= VIF_FLAG_MAC_IP_LEARNING;
        }
        MacAddress mac;
        if (parent_.get() != NULL) {
            encoder.set_vifr_type(VIF_TYPE_VIRTUAL_VLAN);
            if ((vmi_type_ == VmInterface::GATEWAY ||
                 vmi_type_ == VmInterface::REMOTE_VM) &&
                tx_vlan_id_ == VmInterface::kInvalidVlanId) {
                //By default in case of gateway, untagged packet
                //would be considered as belonging to interface
                //at tag 0
                encoder.set_vifr_vlan_id(0);
                encoder.set_vifr_ovlan_id(0);
            } else {
                encoder.set_vifr_vlan_id(rx_vlan_id_);
                encoder.set_vifr_ovlan_id(tx_vlan_id_);
            }
            InterfaceKSyncEntry *parent =
                (static_cast<InterfaceKSyncEntry *> (parent_.get()));
            encoder.set_vifr_parent_vif_idx(parent->interface_id());
            mac = parent->mac();
        } else {
            mac = ksync_obj_->ksync()->agent()->vrrp_mac();
            encoder.set_vifr_type(VIF_TYPE_VIRTUAL);
        }

        if (vmi_type_ == VmInterface::VHOST) {
            encoder.set_vifr_type(VIF_TYPE_HOST);
            if (xconnect_.get()) {
                InterfaceKSyncEntry *xconnect =
                    static_cast<InterfaceKSyncEntry *>(xconnect_.get());
                encoder.set_vifr_cross_connect_idx(xconnect->os_index_);
            } else {
                encoder.set_vifr_cross_connect_idx(Interface::kInvalidIndex);
            }
        }

        std::vector<int8_t> intf_mac((int8_t *)mac,
                                     (int8_t *)mac + mac.size());
        encoder.set_vifr_mac(intf_mac);

        if (ksync_obj_->ksync()->agent()->isVmwareVcenterMode() ||
            ((ksync_obj_->ksync()->agent()->server_gateway_mode() ||
             ksync_obj_->ksync()->agent()->pbb_gateway_mode()) &&
             vmi_type_ == VmInterface::REMOTE_VM)) {
            std::vector<int8_t> mac;
            mac.insert(mac.begin(), (const int8_t *)smac(),
                       (const int8_t *)smac() + smac().size());
            for (std::set<MacAddress>::iterator it = aap_mac_list_.begin();
                 it != aap_mac_list_.end(); ++it) {
                mac.insert(mac.end(), (const int8_t *)(*it),
                           (const int8_t *)(*it) + (*it).size());
            }
            encoder.set_vifr_src_mac(mac);
            flags |= VIF_FLAG_MIRROR_NOTAG;
        }

        // Disable fat-flow when health-check status is inactive
        // If fat-flow is active, then following problem happens,
        //    1. Health Check Request from vhost0 interface creates a flow
        //    2. Health Check response is received from VMI with fat-flow
        //       - Response creates new flow due to fat-flow configuration
        //       - If health-check status is inactive routes for interface are
        //         withdrawn
         //      - Flow created from response fails due to missing routes
        // If fat-flow is disabled, the reverse packet hits flow created (1)
        // and succeeds
        if (hc_active_ && fat_flow_list_.list_.size() != 0) {
            std::vector<int32_t> fat_flow_protocol_port_list;
            std::vector<uint64_t> fat_flow_src_prefix_h_list;
            std::vector<uint64_t> fat_flow_src_prefix_l_list;
            std::vector<int8_t> fat_flow_src_prefix_mask_list;
            std::vector<int8_t> fat_flow_src_aggregate_plen_list;
            std::vector<uint64_t> fat_flow_dst_prefix_h_list;
            std::vector<uint64_t> fat_flow_dst_prefix_l_list;
            std::vector<int8_t> fat_flow_dst_prefix_mask_list;
            std::vector<int8_t> fat_flow_dst_aggregate_plen_list;
            uint64_t tmp_ip6[2];

            for (VmInterface::FatFlowEntrySet::const_iterator it =
                 fat_flow_list_.list_.begin();
                 it != fat_flow_list_.list_.end(); it++) {
                if (it->prefix_aggregate == VmInterface::AGGREGATE_NONE) {
                    fat_flow_protocol_port_list.push_back(
                        VIF_FAT_FLOW_ENCODE_AGGR_PROTO_PORT(it->prefix_aggregate,
                                                        it->ignore_address,
                                                        it->protocol,
                                                        it->port));
                    fat_flow_src_prefix_l_list.push_back(0);
                    fat_flow_src_prefix_h_list.push_back(0);
                    fat_flow_src_prefix_mask_list.push_back(0);
                    fat_flow_src_aggregate_plen_list.push_back(0);
                    fat_flow_dst_prefix_l_list.push_back(0);
                    fat_flow_dst_prefix_h_list.push_back(0);
                    fat_flow_dst_prefix_mask_list.push_back(0);
                    fat_flow_dst_aggregate_plen_list.push_back(0);
                } else if ((it->prefix_aggregate ==
                            VmInterface::AGGREGATE_SRC_IPV4) ||
                           (it->prefix_aggregate ==
                            VmInterface::AGGREGATE_DST_IPV4) ||
                           (it->prefix_aggregate ==
                            VmInterface::AGGREGATE_SRC_DST_IPV4)) {
                    fat_flow_protocol_port_list.push_back(
                        VIF_FAT_FLOW_ENCODE_AGGR_PROTO_PORT(it->prefix_aggregate,
                                                        it->ignore_address,
                                                        it->protocol,
                                                        it->port));
                    fat_flow_src_prefix_l_list.push_back(
                        (uint64_t) htonl(it->src_prefix.to_v4().to_ulong()));
                    fat_flow_src_prefix_h_list.push_back(0);
                    fat_flow_src_prefix_mask_list.push_back(it->src_prefix_mask);
                    fat_flow_src_aggregate_plen_list.push_back(it->src_aggregate_plen);
                    fat_flow_dst_prefix_l_list.push_back(
                        (uint64_t) htonl(it->dst_prefix.to_v4().to_ulong()));
                    fat_flow_dst_prefix_h_list.push_back(0);
                    fat_flow_dst_prefix_mask_list.push_back(it->dst_prefix_mask);
                    fat_flow_dst_aggregate_plen_list.push_back(it->dst_aggregate_plen);
                } else {
                    fat_flow_protocol_port_list.push_back(
                        VIF_FAT_FLOW_ENCODE_AGGR_PROTO_PORT(it->prefix_aggregate,
                                                        it->ignore_address,
                                                        it->protocol,
                                                        it->port));
                    if (it->prefix_aggregate == VmInterface::AGGREGATE_DST_IPV6) {
                        // src prefix is not valid
                        fat_flow_src_prefix_h_list.push_back(0);
                        fat_flow_src_prefix_l_list.push_back(0);
                        fat_flow_src_prefix_mask_list.push_back(0);
                        fat_flow_src_aggregate_plen_list.push_back(0);
                    } else {
                        Ip6AddressToU64Array(it->src_prefix.to_v6(), tmp_ip6, 2);
                        fat_flow_src_prefix_h_list.push_back(tmp_ip6[0]);
                        fat_flow_src_prefix_l_list.push_back(tmp_ip6[1]);
                        fat_flow_src_prefix_mask_list.push_back(
                                                        it->src_prefix_mask);
                        fat_flow_src_aggregate_plen_list.push_back(
                                                        it->src_aggregate_plen);
                    }
                    if (it->prefix_aggregate == VmInterface::AGGREGATE_SRC_IPV6) {
                        // dst prefix is not valid
                        fat_flow_dst_prefix_h_list.push_back(0);
                        fat_flow_dst_prefix_l_list.push_back(0);
                        fat_flow_dst_prefix_mask_list.push_back(0);
                        fat_flow_dst_aggregate_plen_list.push_back(0);
                    } else {
                        Ip6AddressToU64Array(it->dst_prefix.to_v6(), tmp_ip6, 2);
                        fat_flow_dst_prefix_h_list.push_back(tmp_ip6[0]);
                        fat_flow_dst_prefix_l_list.push_back(tmp_ip6[1]);
                        fat_flow_dst_prefix_mask_list.push_back(it->dst_prefix_mask);
                        fat_flow_dst_aggregate_plen_list.push_back(
                                                         it->dst_aggregate_plen);
                    }
                }
            }
            encoder.set_vifr_fat_flow_protocol_port(
                    fat_flow_protocol_port_list);
            encoder.set_vifr_fat_flow_src_prefix_h(
                    fat_flow_src_prefix_h_list);
            encoder.set_vifr_fat_flow_src_prefix_l(
                    fat_flow_src_prefix_l_list);
            encoder.set_vifr_fat_flow_src_prefix_mask(
                    fat_flow_src_prefix_mask_list);
            encoder.set_vifr_fat_flow_src_aggregate_plen(
                    fat_flow_src_aggregate_plen_list);
            encoder.set_vifr_fat_flow_dst_prefix_h(
                    fat_flow_dst_prefix_h_list);
            encoder.set_vifr_fat_flow_dst_prefix_l(
                    fat_flow_dst_prefix_l_list);
            encoder.set_vifr_fat_flow_dst_prefix_mask(
                    fat_flow_dst_prefix_mask_list);
            encoder.set_vifr_fat_flow_dst_aggregate_plen(
                    fat_flow_dst_aggregate_plen_list);

            encoder.set_vifr_fat_flow_exclude_ip_list
                (fat_flow_exclude_list_.fat_flow_v4_exclude_list_);
            encoder.set_vifr_fat_flow_exclude_ip6_u_list
                (fat_flow_exclude_list_.fat_flow_v6_exclude_upper_list_);
            encoder.set_vifr_fat_flow_exclude_ip6_l_list
                (fat_flow_exclude_list_.fat_flow_v6_exclude_lower_list_);
            encoder.set_vifr_fat_flow_exclude_ip6_plen_list
                (fat_flow_exclude_list_.fat_flow_v6_exclude_plen_list_);
        }

        if (pbb_interface_) {
            std::vector<int8_t> pbb_mac((int8_t *)pbb_mac_,
                                        (int8_t *)pbb_mac_ + mac.size());
            encoder.set_vifr_pbb_mac(pbb_mac);
            encoder.set_vifr_isid(isid_);
            if (pbb_cmac_vrf_ != VrfEntry::kInvalidIndex) {
                vrf_id = pbb_cmac_vrf_;
            }
        }

        if (!primary_ip6_.is_unspecified()) {
            uint64_t data[2];
            Ip6AddressToU64Array(primary_ip6_, data, 2);
            encoder.set_vifr_ip6_u(data[0]);
            encoder.set_vifr_ip6_l(data[1]);
        }
        encoder.set_vifr_vhostuser_mode(vhostuser_mode_);
        break;
    }

    case Interface::PHYSICAL: {
        encoder.set_vifr_type(VIF_TYPE_PHYSICAL);
        flags |= VIF_FLAG_L3_ENABLED;
        flags |= VIF_FLAG_L2_ENABLED;
        if (!persistent_) {
            flags |= VIF_FLAG_VHOST_PHYS;
        }

        if (subtype_ == PhysicalInterface::VMWARE ||
            ksync_obj_->ksync()->agent()->server_gateway_mode() ||
            ksync_obj_->ksync()->agent()->pbb_gateway_mode()) {
            flags |= VIF_FLAG_PROMISCOUS;
        }
        if (subtype_ == PhysicalInterface::CONFIG) {
            flags |= VIF_FLAG_NATIVE_VLAN_TAG;
            flags |= VIF_FLAG_FAB_GW_MODE;
        }
        encoder.set_vifr_name(display_name_);

        AgentParam *params = ksync_obj_->ksync()->agent()->params();
        std::vector<int16_t> nic_queue_list;
        for (std::set<uint16_t>::const_iterator it =
                 params->nic_queue_list().begin();
                 it != params->nic_queue_list().end(); it++) {
            nic_queue_list.push_back(*it);
        }
        encoder.set_vifr_hw_queues(nic_queue_list);
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
        flags |= VIF_FLAG_L2_ENABLED;
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

    if (layer3_forwarding_) {
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

            const VrfEntry* vrf =
                ksync_obj_->ksync()->agent()->vrf_table()->FindVrfFromId(vrf_id_);
            if (vrf && vrf->vn()) {
                const std::string &vn_name = vrf->vn()->GetName() ;
                // TLV for RX Mirror
                std::vector<int8_t> srcdata;
                srcdata.push_back(FlowEntry::PCAP_SOURCE_VN);
                srcdata.push_back(vn_name.size());
                srcdata.insert(srcdata.end(), vn_name.begin(),
                               vn_name.end());
                srcdata.push_back(FlowEntry::PCAP_TLV_END);
                srcdata.push_back(0x0);
                encoder.set_vifr_in_mirror_md(srcdata);
                // TLV for TX Mirror
                std::vector<int8_t> destdata;
                destdata.push_back(FlowEntry::PCAP_DEST_VN);
                destdata.push_back(vn_name.size());
                destdata.insert(destdata.end(), vn_name.begin(),
                                vn_name.end());
                destdata.push_back(FlowEntry::PCAP_TLV_END);
                destdata.push_back(0x0);
                encoder.set_vifr_out_mirror_md(destdata);
            }
        }

        if (has_service_vlan_) {
            flags |= VIF_FLAG_SERVICE_IF;
        }
    }

    encoder.set_vifr_mac(std::vector<int8_t>((const int8_t *)mac(),
                                             (const int8_t *)mac() + mac().size()));

    switch(transport_) {
    case Interface::TRANSPORT_ETHERNET: {
        encoder.set_vifr_transport(VIF_TRANSPORT_ETH);
        break;
    }

    case Interface::TRANSPORT_SOCKET: {
        encoder.set_vifr_transport(VIF_TRANSPORT_SOCKET);
        break;
    }

    case Interface::TRANSPORT_PMD: {
        encoder.set_vifr_transport(VIF_TRANSPORT_PMD);
        break;
    }
    default:
        break;
    }

    encoder.set_vifr_flags(flags);

    encoder.set_vifr_vrf(vrf_id);
    encoder.set_vifr_mcast_vrf(multicast_vrf_id_);
    encoder.set_vifr_idx(interface_id_);
    encoder.set_vifr_rid(0);
    encoder.set_vifr_os_idx(os_index_);
    encoder.set_vifr_mtu(0);
    if (type_ != Interface::PHYSICAL) {
        encoder.set_vifr_name(interface_name_);
    }
    encoder.set_vifr_ip(htonl(ip_));
    encoder.set_vifr_nh_id(flow_key_nh_id_);

    // GUIDs are used as interface identifiers on Windows
    if (os_guid_) {
        InterfaceOsParams::IfGuid& os_guid = os_guid_.get();

        std::vector<int8_t> raw_guid(os_guid.size(), 0);
        memcpy(raw_guid.data(), &os_guid, os_guid.size());

        encoder.set_vifr_if_guid(raw_guid);
    }

    int error = 0;
    encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    assert(error == 0);
    assert(encode_len <= buf_len);
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
        info.set_hc_active(hc_active_);
        info.set_l2_active(l2_active_);
        info.set_active(ipv4_active_);
        info.set_policy_enabled(policy_enabled_);
        info.set_service_enabled(has_service_vlan_);
        info.set_analyzer_name(analyzer_name_);

        std::vector<KSyncIntfFatFlowInfo> fat_flows;
        for (VmInterface::FatFlowEntrySet::const_iterator it =
             fat_flow_list_.list_.begin(); it != fat_flow_list_.list_.end();
             it++) {
            KSyncIntfFatFlowInfo info;
            info.set_protocol(it->protocol);
            info.set_port(it->port);
            info.set_ignore_address(it->ignore_address);
            fat_flows.push_back(info);
        }
        info.set_fat_flows(fat_flows);
        info.set_fat_flow_v4_exclude_list
            (fat_flow_exclude_list_.fat_flow_v4_exclude_list_);
        info.set_fat_flow_v6_exclude_upper_list
            (fat_flow_exclude_list_.fat_flow_v6_exclude_upper_list_);
        info.set_fat_flow_v6_exclude_lower_list
            (fat_flow_exclude_list_.fat_flow_v6_exclude_lower_list_);
        info.set_fat_flow_v6_exclude_plen_list
            (fat_flow_exclude_list_.fat_flow_v6_exclude_plen_list_);
    }
}

int InterfaceKSyncEntry::AddMsg(char *buf, int buf_len) {
    KSyncIntfInfo info;

    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(Intf, GetObject(), info);
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int InterfaceKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    KSyncIntfInfo info;
    FillObjectLog(sandesh_op::DEL, info);
    KSYNC_TRACE(Intf, GetObject(), info);
    return Encode(sandesh_op::DEL, buf, buf_len);
}

int InterfaceKSyncEntry::ChangeMsg(char *buf, int buf_len) {
    return AddMsg(buf, buf_len);
}

InterfaceKSyncObject::InterfaceKSyncObject(KSync *ksync) :
    KSyncDBObject("KSync Interface"), ksync_(ksync) {
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
        case Interface::LOGICAL:
        case Interface::REMOTE_PHYSICAL:
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

KSyncDBObject::DBFilterResp
InterfaceKSyncObject::DBEntryFilter(const DBEntry *entry,
                                    const KSyncDBEntry *ksync) {

    const Interface *intf = dynamic_cast<const Interface *>(entry);
    const VmInterface *vm_intf = dynamic_cast<const VmInterface *>(intf);

    uint32_t rx_vlan_id = VmInterface::kInvalidVlanId;
    VmInterface::VmiType vmi_type = VmInterface::VMI_TYPE_INVALID;

    if (vm_intf) {
        rx_vlan_id = vm_intf->rx_vlan_id();
        vmi_type = vm_intf->vmi_type();
    }

    if (IsValidOsIndex(intf->os_index(), intf->type(), rx_vlan_id,
                       vmi_type, intf->transport()) == false) {
        return DBFilterIgnore;
    }

    if (intf->type() == Interface::LOGICAL ||
        intf->type() == Interface::REMOTE_PHYSICAL) {
        return DBFilterIgnore;
    }

    // No need to add VLAN sub-interface if there is no parent
    if (vm_intf && vm_intf->device_type() == VmInterface::VM_VLAN_ON_VMI &&
        vm_intf->parent() == NULL) {
        return DBFilterIgnore;
    }

    return DBFilterAccept;
}

//////////////////////////////////////////////////////////////////////////////
// sandesh routines
//////////////////////////////////////////////////////////////////////////////
void vr_response::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->SetErrno(ioc->VrResponseMsgHandler(this));
}

void InterfaceKSyncEntry::SetKsyncItfSandeshData(KSyncItfSandeshData *data) const {

    data->set_analyzer_name(analyzer_name_);

    if (drop_new_flows_) {
        data->set_drop_new_flows("Enable");
    } else {
        data->set_drop_new_flows("Disable");
    }

    if (dhcp_enable_) {
        data->set_dhcp_service("Enable");
    } else {
        data->set_dhcp_service("Disable");
    }

    data->set_fd(fd_);
    data->set_flow_key_nh_id(flow_key_nh_id_);

    if (has_service_vlan_) {
        data->set_has_service_vlan("Enable");
    } else {
        data->set_has_service_vlan("Disable");
    }

    data->set_interface_id(interface_id_);
    data->set_interface_name(interface_name_);
    data->set_ip(ip_);

    if (hc_active_) {
        data->set_health_check("Active");
    } else {
        data->set_health_check("Inactive");
    }

    if (ipv4_active_) {
        data->set_ipv4_active("Active");
    } else {
        data->set_ipv4_active("Inactive");
    }

    if (layer3_forwarding_) {
        data->set_layer3_forwarding("Enable");
    } else {
        data->set_layer3_forwarding("Disable");
    }

    if (l2_active_) {
        data->set_l2_active("Active");
    } else {
        data->set_l2_active("Inactive");
    }

    if (metadata_l2_active_) {
        data->set_metadata_l2_active("Active");
    } else {
        data->set_metadata_l2_active("Inactive");
    }


    if (metadata_ip_active_) {
        data->set_metadata_ip_active("Active");
    } else {
        data->set_metadata_ip_active("Inactive");
    }

    if (bridging_) {
        data->set_bridging("Enable");
    } else {
        data->set_l2_active("Disable");
    }

    data->set_mac(mac_.ToString());
    data->set_smac(smac_.ToString());
    data->set_network_id(network_id_);
    data->set_os_index(os_index_);

    if (policy_enabled_) {
        data->set_policy_enabled("Enable");
    } else {
        data->set_policy_enabled("Disable");
    }

    data->set_rx_vlan_id(rx_vlan_id_);
    data->set_tx_vlan_id(tx_vlan_id_);
    data->set_vrf_id(vrf_id_);

    if (persistent_) {
        data->set_persistent("Enable");
    } else {
        data->set_persistent("Disable");
    }

    if (no_arp_) {
        data->set_no_arp("Enable");
    } else {
        data->set_no_arp("Disable");
    }

    data->set_display_name(display_name_);

    if (flood_unknown_unicast_) {
        data->set_flood_unknown_unicast("Enable");
    } else {
        data->set_flood_unknown_unicast("Disable");
    }

    if (learning_enabled_) {
        data->set_learning("Enable");
    } else {
        data->set_learning("Disable");
    }

    data->set_isid(isid_);
    data->set_pbb_cmac_vrf(pbb_cmac_vrf_);
    data->set_pbb_mac(pbb_mac_.ToString());

    if (etree_leaf_) {
        data->set_etree_leaf("Enable");
    } else {
        data->set_etree_leaf("Disable");
    }

    if (pbb_interface_) {
        data->set_pbb_interface("Enable");
    } else {
        data->set_pbb_interface("Disable");
    }

    if (igmp_enable_) {
        data->set_igmp_enable("Enable");
    } else {
        data->set_igmp_enable("Disable");
    }

    if (mac_ip_learning_enable_) {
        data->set_mac_ip_learning_enable("Enable");
    } else {
        data->set_mac_ip_learning_enable("Disable");
    }
}

bool InterfaceKSyncEntry::KSyncEntrySandesh(Sandesh *sresp) {
    KSyncItfResp *resp = static_cast<KSyncItfResp *> (sresp);

    KSyncItfSandeshData data;
    SetKsyncItfSandeshData(&data);
    std::vector<KSyncItfSandeshData> &list =
            const_cast<std::vector<KSyncItfSandeshData>&>(resp->get_KSyncitf_list());
    list.push_back(data);

    return true;
}

void KSyncItfReq::HandleRequest() const {
    AgentKsyncSandeshPtr sand(new AgentKsyncIntfSandesh(context()));
    sand->DoKsyncSandesh(sand);

}
