/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/types.h>
#include <net/ethernet.h>
#if defined(__FreeBSD__)
# include "vr_os.h"
#endif
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <base/logging.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>

#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/mirror_table.h"
#include "oper/tunnel_nh.h"
#include "vrouter/ksync/nexthop_ksync.h"
#include "vrouter/ksync/ksync_init.h"
#include "vr_types.h"
#include "oper/ecmp_load_balance.h"
#include "vrouter/ksync/agent_ksync_types.h"
#include <vrouter/ksync/ksync_agent_sandesh.h>

NHKSyncEntry::NHKSyncEntry(NHKSyncObject *obj, const NHKSyncEntry *entry,
                           uint32_t index) :
    KSyncNetlinkDBEntry(index), ksync_obj_(obj), type_(entry->type_),
    vrf_id_(entry->vrf_id_), label_(entry->label_),
    interface_(entry->interface_), sip_(entry->sip_), dip_(entry->dip_),
    sport_(entry->sport_), dport_(entry->dport_), smac_(entry->smac_),
    dmac_(entry->dmac_), rewrite_dmac_(entry->rewrite_dmac_),
    valid_(entry->valid_), policy_(entry->policy_),
    is_mcast_nh_(entry->is_mcast_nh_),
    defer_(entry->defer_), component_nh_list_(entry->component_nh_list_),
    nh_(entry->nh_), vlan_tag_(entry->vlan_tag_),
    is_local_ecmp_nh_(entry->is_local_ecmp_nh_),
    is_bridge_(entry->is_bridge_),
    is_vxlan_routing_(entry->is_vxlan_routing_),
    comp_type_(entry->comp_type_),
    validate_mcast_src_(entry->validate_mcast_src_),
    tunnel_type_(entry->tunnel_type_), prefix_len_(entry->prefix_len_),
    nh_id_(entry->nh_id()),
    component_nh_key_list_(entry->component_nh_key_list_),
    bridge_nh_(entry->bridge_nh_),
    flood_unknown_unicast_(entry->flood_unknown_unicast_),
    ecmp_hash_fieds_(entry->ecmp_hash_fieds_.HashFieldsToUse()),
    pbb_child_nh_(entry->pbb_child_nh_), isid_(entry->isid_),
    pbb_label_(entry->pbb_label_), learning_enabled_(entry->learning_enabled_),
    need_pbb_tunnel_(entry->need_pbb_tunnel_), etree_leaf_(entry->etree_leaf_),
    layer2_control_word_(entry->layer2_control_word_),
    crypt_(entry->crypt_), crypt_path_available_(entry->crypt_path_available_),
    crypt_interface_(entry->crypt_interface_),
    transport_tunnel_type_(entry->transport_tunnel_type_) {
    }

NHKSyncEntry::NHKSyncEntry(NHKSyncObject *obj, const NextHop *nh) :
    KSyncNetlinkDBEntry(kInvalidIndex), ksync_obj_(obj), type_(nh->GetType()),
    vrf_id_(0), interface_(NULL), dmac_(), valid_(nh->IsValid()),
    policy_(nh->PolicyEnabled()), is_mcast_nh_(false), nh_(nh),
    vlan_tag_(VmInterface::kInvalidVlanId), is_bridge_(false),
    is_vxlan_routing_(false),
    tunnel_type_(TunnelType::INVALID), prefix_len_(32), nh_id_(nh->id()),
    bridge_nh_(false), flood_unknown_unicast_(false),
    learning_enabled_(nh->learning_enabled()), need_pbb_tunnel_(false),
    etree_leaf_ (false), layer2_control_word_(false),
    crypt_(false), crypt_path_available_(false), crypt_interface_(NULL) {

    switch (type_) {
    case NextHop::ARP: {
        const ArpNH *arp = static_cast<const ArpNH *>(nh);
        vrf_id_ = arp->vrf_id();
        sip_ = *(arp->GetIp());
        break;
    }

    case NextHop::INTERFACE: {
        InterfaceKSyncObject *interface_object =
            ksync_obj_->ksync()->interface_ksync_obj();
        const InterfaceNH *if_nh = static_cast<const InterfaceNH *>(nh);
        InterfaceKSyncEntry if_ksync(interface_object, if_nh->GetInterface());
        interface_ = interface_object->GetReference(&if_ksync);
        assert(interface_);
        is_mcast_nh_ = if_nh->is_multicastNH();
        is_bridge_ = if_nh->IsBridge();
        is_vxlan_routing_ = if_nh->IsVxlanRouting();
        const VrfEntry * vrf = if_nh->GetVrf();
        vrf_id_ = (vrf != NULL) ? vrf->vrf_id() : VrfEntry::kInvalidIndex;
        dmac_ = if_nh->GetDMac();
        // VmInterface can potentially have vlan-tags. Get tag in such case
        if (if_nh->GetInterface()->type() == Interface::VM_INTERFACE) {
            vlan_tag_ = (static_cast<const VmInterface *>
                         (if_nh->GetInterface()))->tx_vlan_id();
        }
        break;
    }

    case NextHop::VLAN: {
        InterfaceKSyncObject *interface_object =
            ksync_obj_->ksync()->interface_ksync_obj();
        const VlanNH *vlan_nh = static_cast<const VlanNH *>(nh);
        InterfaceKSyncEntry if_ksync(interface_object, vlan_nh->GetInterface());
        interface_ = interface_object->GetReference(&if_ksync);
        assert(interface_);
        vlan_tag_ = vlan_nh->GetVlanTag();
        vrf_id_ = vlan_nh->GetVrf()->vrf_id();
        break;
    }

    case NextHop::TUNNEL: {
        const TunnelNH *tunnel = static_cast<const TunnelNH *>(nh);
        vrf_id_ = tunnel->vrf_id();
        sip_ = *(tunnel->GetSip());
        dip_ = *(tunnel->GetDip());
        tunnel_type_ = tunnel->GetTunnelType();
        crypt_ = tunnel->GetCrypt();
        crypt_path_available_ = tunnel->GetCryptTunnelAvailable();

        if (tunnel->GetCryptInterface()) {
            InterfaceKSyncObject *crypt_interface_object =
                    ksync_obj_->ksync()->interface_ksync_obj();
            InterfaceKSyncEntry if_ksync(crypt_interface_object, tunnel->GetCryptInterface());
            crypt_interface_ = crypt_interface_object->GetReference(&if_ksync);
        }
        rewrite_dmac_ = tunnel->rewrite_dmac();
        if (tunnel_type_.GetType() == TunnelType::MPLS_OVER_MPLS) {
            const LabelledTunnelNH *labelled_tunnel =
                    static_cast<const LabelledTunnelNH *>(nh);
            label_ = labelled_tunnel->GetTransportLabel();
            transport_tunnel_type_ =
                    labelled_tunnel->GetTransportTunnelType();
        }
        break;
    }

    case NextHop::DISCARD: {
        break;
    }

    case NextHop::L2_RECEIVE: {
        break;
    }

    case NextHop::RESOLVE: {
        InterfaceKSyncObject *interface_object =
            ksync_obj_->ksync()->interface_ksync_obj();
        const ResolveNH *rsl_nh = static_cast<const ResolveNH *>(nh);
        InterfaceKSyncEntry if_ksync(interface_object, rsl_nh->get_interface());
        interface_ = interface_object->GetReference(&if_ksync);
        vrf_id_ = rsl_nh->get_interface()->vrf_id();
        if (rsl_nh->get_interface()->type() == Interface::VM_INTERFACE) {
            const VmInterface *vm_intf =
                static_cast<const VmInterface *>(rsl_nh->get_interface());
            if (vm_intf->vrf()->forwarding_vrf()) {
                vrf_id_ = vm_intf->vrf()->forwarding_vrf()->vrf_id();
            }
        }
        break;
    }

    case NextHop::VRF: {
        const VrfNH *vrf_nh = static_cast<const VrfNH *>(nh);
        vrf_id_ = vrf_nh->GetVrf()->vrf_id();
        bridge_nh_ = vrf_nh->bridge_nh();
        break;
    }

    case NextHop::RECEIVE: {
        InterfaceKSyncObject *interface_object =
            ksync_obj_->ksync()->interface_ksync_obj();
        const ReceiveNH *rcv_nh = static_cast<const ReceiveNH *>(nh);
        InterfaceKSyncEntry if_ksync(interface_object, rcv_nh->GetInterface());
        interface_ = interface_object->GetReference(&if_ksync);
        vrf_id_ = rcv_nh->GetInterface()->vrf_id();
        assert(interface_);
        break;
    }

    case NextHop::MIRROR: {
        const MirrorNH *mirror_nh = static_cast<const MirrorNH *>(nh);
        vrf_id_ = mirror_nh->vrf_id();
        sip_ = *(mirror_nh->GetSip());
        sport_ = mirror_nh->GetSPort();
        dip_ = *(mirror_nh->GetDip());
        dport_ = mirror_nh->GetDPort();
        break;
    }

    case NextHop::PBB: {
        const PBBNH *pbb_nh = static_cast<const PBBNH *>(nh);
        vrf_id_ = pbb_nh->vrf_id();
        dmac_ = pbb_nh->dest_bmac();
        isid_ = pbb_nh->isid();
        break;
    }

    case NextHop::COMPOSITE: {
        const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
        component_nh_list_.clear();
        vrf_id_ = comp_nh->vrf()->vrf_id();
        comp_type_ = comp_nh->composite_nh_type();
        validate_mcast_src_ = comp_nh->validate_mcast_src();
        ecmp_hash_fieds_ =
            const_cast<CompositeNH *>(comp_nh)->CompEcmpHashFields().HashFieldsToUse();
        component_nh_key_list_ = comp_nh->component_nh_key_list();
        ComponentNHList::const_iterator component_nh_it =
            comp_nh->begin();
        while (component_nh_it != comp_nh->end()) {
            const NextHop *component_nh = NULL;
            uint32_t label = 0;
            if (*component_nh_it) {
                component_nh =  (*component_nh_it)->nh();
                label = (*component_nh_it)->label();
            }
            KSyncEntry *ksync_nh = NULL;
            if (component_nh != NULL) {
                NHKSyncObject *nh_object =
                    ksync_obj_->ksync()->nh_ksync_obj();
                NHKSyncEntry nhksync(nh_object, component_nh);
                ksync_nh = nh_object->GetReference(&nhksync);
            }
            KSyncComponentNH ksync_component_nh(label, ksync_nh);
            component_nh_list_.push_back(ksync_component_nh);
            component_nh_it++;
        }
        break;
    }

    default:
        assert(0);
        break;
    }
}

NHKSyncEntry::~NHKSyncEntry() {
}

KSyncDBObject *NHKSyncEntry::GetObject() const {
    return ksync_obj_;
}

bool NHKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const NHKSyncEntry &entry = static_cast<const NHKSyncEntry &>(rhs);

    if (type_ != entry.type_) {
        return type_ < entry.type_;
    }

    if (type_ == NextHop::DISCARD) {
        return false;
    }

    if (type_ == NextHop::L2_RECEIVE) {
        return false;
    }

    if (type_ == NextHop::ARP) {
        //Policy is ignored for ARP NH
        if (vrf_id_ != entry.vrf_id_) {
            return vrf_id_ < entry.vrf_id_;
        }
        return sip_ < entry.sip_;
    }

    if (policy_ != entry.policy_) {
        return policy_ < entry.policy_;
    }

    if (type_ == NextHop::VRF) {
        if ( vrf_id_ != entry.vrf_id_) {
            return vrf_id_ < entry.vrf_id_;
        }
        return bridge_nh_ < entry.bridge_nh_;
    }

    if (type_ == NextHop::INTERFACE) {
        if (is_mcast_nh_ != entry.is_mcast_nh_) {
            return is_mcast_nh_ < entry.is_mcast_nh_;
        }
        if(is_bridge_ != entry.is_bridge_) {
            return is_bridge_ < entry.is_bridge_;
        }

        if(is_vxlan_routing_ != entry.is_vxlan_routing_) {
            return is_vxlan_routing_ < entry.is_vxlan_routing_;
        }

        if (dmac_ != entry.dmac_) {
            return dmac_ < entry.dmac_;
        }

        return interface() < entry.interface();
    }

    if (type_ == NextHop::RECEIVE || type_ == NextHop::RESOLVE) {
        return interface() < entry.interface();
    }


    if (type_ == NextHop::TUNNEL) {
        if (vrf_id_ != entry.vrf_id_) {
            return vrf_id_ < entry.vrf_id_;
        }

        if (sip_ != entry.sip_) {
            return sip_ < entry.sip_;
        }

        if (dip_ != entry.dip_) {
            return dip_ < entry.dip_;
        }

        if (crypt_ != entry.crypt_) {
            return crypt_ < entry.crypt_;
        }

        if (crypt_path_available_ != entry.crypt_path_available_) {
            return crypt_path_available_ < entry.crypt_path_available_;
        }

        if (crypt_interface() != entry.crypt_interface()) {
            return crypt_interface() < entry.crypt_interface();
        }

        if (rewrite_dmac_ != entry.rewrite_dmac_) {
            return rewrite_dmac_ < entry.rewrite_dmac_;
        }
        if (tunnel_type_.Compare(entry.tunnel_type_) == false) {
            return tunnel_type_.IsLess(entry.tunnel_type_);
        }
        if (tunnel_type_.GetType() == TunnelType::MPLS_OVER_MPLS) {
            return label_ < entry.label_;
        }
    }

    if (type_ == NextHop::MIRROR) {
        if (vrf_id_ != entry.vrf_id_) {
            return vrf_id_ < entry.vrf_id_;
        }

        if (dip_ != entry.dip_) {
            return dip_ < entry.dip_;
        }

        return dport_ < entry.dport_;
    }

    if (type_ == NextHop::PBB) {
        if (vrf_id_ != entry.vrf_id_) {
            return vrf_id_ < entry.vrf_id_;
        }

        if (dmac_ != entry.dmac_) {
            return dmac_ < entry.dmac_;
        }

        return isid_ < entry.isid_;
    }

    if (type_ == NextHop::COMPOSITE) {
        if (comp_type_ != entry.comp_type_) {
            return comp_type_ < entry.comp_type_;
        }

        if (vrf_id_ != entry.vrf_id_) {
            return vrf_id_ < entry.vrf_id_;
        }

        ComponentNHKeyList::const_iterator it =
            component_nh_key_list_.begin();
        ComponentNHKeyList::const_iterator entry_it =
            entry.component_nh_key_list_.begin();
        for (;it != component_nh_key_list_.end() &&
             entry_it != entry.component_nh_key_list_.end(); it++, entry_it++) {
            if (*it == NULL &&
                    *entry_it == NULL) {
                continue;
            }
            //One of the component NH is NULL
            if ((*it) == NULL ||
                    (*entry_it) == NULL) {
                return (*it) < (*entry_it);
            }

            //Check if the label is different
            if ((*it)->label() !=
                    (*entry_it)->label()) {
                return (*it)->label() <
                    (*entry_it)->label();
            }

            //Check if the nexthop key is different
            //Ideally we could find the nexthop and compare pointer alone
            //it wont work because this is called from Find context itself,
            //and it would result in deadlock
            //Hence compare nexthop key alone
            const NextHopKey *left_nh = (*it)->nh_key();
            const NextHopKey *right_nh = (*entry_it)->nh_key();

            if (left_nh->IsEqual(*right_nh) == false) {
                if (left_nh->GetType() != right_nh->GetType()) {
                    return left_nh->GetType() < right_nh->GetType();
                }
                return left_nh->IsLess(*right_nh);
            }

        }

        if (it == component_nh_key_list_.end() &&
            entry_it == entry.component_nh_key_list_.end()) {
            return false;
        }

        if (it == component_nh_key_list_.end()) {
            return true;
        }
        return false;
    }

    if (type_ == NextHop::VLAN) {
        if (interface() != entry.interface()) {
            return interface() < entry.interface();

        }

        return vlan_tag_ < entry.vlan_tag_;
    }

    return false;

    assert(0);
}

std::string NHKSyncEntry::ToString() const {
    std::stringstream s;
    s << "NextHop Index: " << nh_id() << " Type: ";
    switch(type_) {
    case NextHop::DISCARD: {
        s << "Discard";
        break;
    }
    case NextHop::L2_RECEIVE: {
        s << "L2-Receive ";
        break;
    }
    case NextHop::RECEIVE: {
        s << "Receive ";
        break;
    }
    case NextHop::RESOLVE: {
        s << "Resolve ";
        break;
    }
    case NextHop::ARP: {
        s << "ARP ";
        break;
    }
    case NextHop::VRF: {
        s << "VRF assign to ";
        const VrfEntry* vrf =
            ksync_obj_->ksync()->agent()->vrf_table()->
            FindVrfFromId(vrf_id_);
        if (vrf) {
            s << vrf->GetName() << " ";
        } else {
            s << "Invalid ";
        }
        break;
    }
    case NextHop::INTERFACE: {
        s << "Interface ";
        break;
    }
    case NextHop::TUNNEL: {
        s << "Tunnel to ";
        s << dip_.to_string();
        s << "rewrite_dmac";
        s << rewrite_dmac_.ToString();
        break;
    }
    case NextHop::MIRROR: {
        s << "Mirror to ";
        s << dip_.to_string() << ": " << dport_;
        break;
    }
    case NextHop::VLAN: {
        s << "VLAN interface ";
        break;
    }
    case NextHop::COMPOSITE: {
        s << "Composite Child members: ";
        if (component_nh_list_.size() == 0) {
            s << "Empty ";
        }
        for (KSyncComponentNHList::const_iterator it = component_nh_list_.begin();
             it != component_nh_list_.end(); it++) {
            KSyncComponentNH component_nh = *it;
            if (component_nh.nh()) {
                s << component_nh.nh()->ToString();
            }
        }
        break;
    }
    case NextHop::INVALID: {
        s << "Invalid ";
    }

    case NextHop::PBB: {
        s << "PBB";
        break;
    }
    }

    if (interface_) {
        s << "<" << interface_->ToString() << ">";
    }
    return s.str();
}

bool NHKSyncEntry::Sync(DBEntry *e) {
    bool ret = false;
    const NextHop *nh = static_cast<NextHop *>(e);

    if (valid_ != nh->IsValid()) {
        valid_ = nh->IsValid();
        ret = true;
    }

    if (learning_enabled_ != nh->learning_enabled()) {
        learning_enabled_ = nh->learning_enabled();
        ret = true;
    }

    if (etree_leaf_ != nh->etree_leaf()) {
        etree_leaf_ = nh->etree_leaf();
        ret = true;
    }

    switch (nh->GetType()) {
    case NextHop::ARP: {
        InterfaceKSyncObject *interface_object =
            ksync_obj_->ksync()->interface_ksync_obj();
        ArpNH *arp_nh = static_cast<ArpNH *>(e);
        if (dmac_ != arp_nh->GetMac()) {
            dmac_ = arp_nh->GetMac();
            ret = true;
        }

        if (valid_) {
            InterfaceKSyncEntry if_ksync(interface_object,
                                         arp_nh->GetInterface());
            if (interface_ != interface_object->GetReference(&if_ksync)) {
                interface_ = interface_object->GetReference(&if_ksync);
                ret = true;
            }
        } else {
            if (interface_ != NULL) {
                interface_ = NULL;
                dmac_.Zero();
                ret = true;
            }
        }
        break;
    }

    case NextHop::INTERFACE: {
        InterfaceNH *intf_nh = static_cast<InterfaceNH *>(e);
        dmac_ = intf_nh->GetDMac();
        uint16_t vlan_tag = VmInterface::kInvalidVlanId;
        if (intf_nh->GetInterface()->type() == Interface::VM_INTERFACE) {
            vlan_tag = vlan_tag_;
            vlan_tag_ = (static_cast<const VmInterface *>
                         (intf_nh->GetInterface()))->tx_vlan_id();
            if (vlan_tag != vlan_tag_) {
                ret = true;
            }
        }
        if (layer2_control_word_ != intf_nh->layer2_control_word()) {
            layer2_control_word_ = intf_nh->layer2_control_word();
            ret = true;
        }

        const VrfEntry * vrf = intf_nh->GetVrf();
        uint32_t vrf_id = (vrf != NULL)? vrf->vrf_id() :VrfEntry::kInvalidIndex;
        if (vrf_id_ != vrf_id) {
            vrf_id_ = vrf_id;
            ret = true;
        }
        break;
    }

    case NextHop::DISCARD: {
        ret = false;
        break;
    }

    case NextHop::L2_RECEIVE: {
        ret = false;
        break;
    }

    case NextHop::TUNNEL: {
        ret = true;
        // Invalid nexthop, no valid interface or mac info
        // present, just return
        if (valid_ == false) {
            if (interface_ != NULL) {
                interface_ = NULL;
                dmac_.Zero();
                ret = true;
            }
            break;
        }

        KSyncEntryPtr interface = NULL;
        MacAddress dmac;
        const TunnelNH *tun_nh = static_cast<TunnelNH *>(e);
        const NextHop *active_nh = tun_nh->GetRt()->GetActiveNextHop();
        // active nexthop can be NULL as when arp rt is delete marked and
        //  is enqueued for notify.
        if (active_nh == NULL) {
            if (interface_ != NULL) {
                interface_ = NULL;
                dmac_.Zero();
                ret = true;
            }
            break;
        }

        if (active_nh->GetType() == NextHop::ARP) {
            const ArpNH *arp_nh = static_cast<const ArpNH *>(active_nh);
            InterfaceKSyncObject *interface_object =
                ksync_obj_->ksync()->interface_ksync_obj();
            InterfaceKSyncEntry if_ksync(interface_object, arp_nh->GetInterface());
            interface = interface_object->GetReference(&if_ksync);
            dmac = arp_nh->GetMac();
        } else if (active_nh->GetType() == NextHop::INTERFACE) {
            const InterfaceNH *intf_nh =
                static_cast<const InterfaceNH *>(active_nh);
            const Interface *oper_intf = intf_nh->GetInterface();
            InterfaceKSyncObject *interface_object =
                ksync_obj_->ksync()->interface_ksync_obj();
            InterfaceKSyncEntry if_ksync(interface_object, oper_intf);
            interface = interface_object->GetReference(&if_ksync);
            dmac = oper_intf->mac();
        }

        bool crypt = tun_nh->GetCrypt();
        if (crypt != crypt_) {
            crypt_ = crypt;
            ret = true;
        }

        bool crypt_path_available = tun_nh->GetCryptTunnelAvailable();
        if (crypt_path_available != crypt_path_available_) {
            crypt_path_available_ = crypt_path_available;
            ret = true;
        }

        const Interface *oper_crypt_intf = tun_nh->GetCryptInterface();

        if (tun_nh->GetCryptInterface()) {
            InterfaceKSyncObject *crypt_interface_object =
                    ksync_obj_->ksync()->interface_ksync_obj();
            InterfaceKSyncEntry if_ksync(crypt_interface_object, oper_crypt_intf);
            KSyncEntryPtr crypt_interface = crypt_interface_object->GetReference(&if_ksync);

            if (crypt_interface != crypt_interface_) {
                crypt_interface_ = crypt_interface;
                ret = true;
            }
        }
        if (tunnel_type_.GetType() == TunnelType::MPLS_OVER_MPLS) {
            const LabelledTunnelNH *labelled_tunnel =
                    static_cast<const LabelledTunnelNH *>(nh);
            if (label_ != labelled_tunnel->GetTransportLabel()) {
                label_ = labelled_tunnel->GetTransportLabel();
                ret = true;
            }
            if (transport_tunnel_type_ !=
                    labelled_tunnel->GetTransportTunnelType()) {
                transport_tunnel_type_ =
                    labelled_tunnel->GetTransportTunnelType();
                ret = true;
            }
        }

        if (dmac != dmac_) {
            dmac_ = dmac;
            ret = true;
        }
        if (interface_ != interface) {
            interface_ = interface;
            ret = true;
        }
        break;
    }

    case NextHop::MIRROR: {
        ret = true;
        // Invalid nexthop, no valid interface or mac info
        // present, just return
        if (valid_ == false || (vrf_id_ == (uint32_t)-1)) {
            if (interface_ != NULL) {
                interface_ = NULL;
                dmac_.Zero();
                ret = true;
            }
            break;
        }

        KSyncEntryPtr interface = NULL;
        MacAddress dmac;
        bool valid = valid_;

        const MirrorNH *mirror_nh = static_cast<MirrorNH *>(e);
        const NextHop *active_nh = mirror_nh->GetRt()->GetActiveNextHop();
        if (active_nh->GetType() == NextHop::ARP) {
            const ArpNH *arp_nh = static_cast<const ArpNH *>(active_nh);
            InterfaceKSyncObject *interface_object =
                ksync_obj_->ksync()->interface_ksync_obj();
            InterfaceKSyncEntry if_ksync(interface_object,
                                         arp_nh->GetInterface());
            interface = interface_object->GetReference(&if_ksync);
            dmac = arp_nh->GetMac();
        } else if (active_nh->GetType() == NextHop::RECEIVE) {
            const ReceiveNH *rcv_nh = static_cast<const ReceiveNH *>(active_nh);
            InterfaceKSyncObject *interface_object =
                ksync_obj_->ksync()->interface_ksync_obj();
            InterfaceKSyncEntry if_ksync(interface_object,
                                         rcv_nh->GetInterface());
            interface = interface_object->GetReference(&if_ksync);
            Agent *agent = ksync_obj_->ksync()->agent();
            dmac = agent->vhost_interface()->mac();
        } else if (active_nh->GetType() == NextHop::DISCARD) {
            valid = false;
            interface = NULL;
        }

        if (valid_ != valid) {
            valid_ = valid;
            ret = true;
        }
        if (dmac_ != dmac) {
            dmac_ = dmac;
            ret = true;
        }
        if (interface_ != interface) {
            interface_ = interface;
            ret = true;
        }
        break;
    }

    case NextHop::PBB: {
        PBBNH *pbb_nh = static_cast<PBBNH *>(e);
        if (pbb_label_ != pbb_nh->label()) {
            pbb_label_ = pbb_nh->label();
            ret = true;
        }

        NHKSyncObject *nh_object =
            ksync_obj_->ksync()->nh_ksync_obj();
        NHKSyncEntry nhksync(nh_object, pbb_nh->child_nh());
        KSyncEntry *ksync_nh = nh_object->GetReference(&nhksync);
        if (ksync_nh != pbb_child_nh_) {
            pbb_child_nh_ = ksync_nh;
            ret = true;
        }
        break;
    }

    case NextHop::COMPOSITE: {
        ret = true;
        CompositeNH *comp_nh = static_cast<CompositeNH *>(e);
        component_nh_list_.clear();
        ComponentNHList::const_iterator component_nh_it =
            comp_nh->begin();
        while (component_nh_it != comp_nh->end()) {
            const NextHop *component_nh = NULL;
            uint32_t label = 0;
            if (*component_nh_it) {
                component_nh =  (*component_nh_it)->nh();
                label = (*component_nh_it)->label();
            }
            KSyncEntry *ksync_nh = NULL;
            if (component_nh != NULL) {
                NHKSyncObject *nh_object =
                    ksync_obj_->ksync()->nh_ksync_obj();
                NHKSyncEntry nhksync(nh_object, component_nh);
                ksync_nh = nh_object->GetReference(&nhksync);
            }
            KSyncComponentNH ksync_component_nh(label, ksync_nh);
            component_nh_list_.push_back(ksync_component_nh);
            component_nh_it++;
        }

        if (comp_nh->EcmpHashFieldInUse() != ecmp_hash_fieds_.HashFieldsToUse()) {
            ecmp_hash_fieds_ = comp_nh->CompEcmpHashFields().HashFieldsToUse();
            ret = true;
        }

        if (need_pbb_tunnel_ != comp_nh->pbb_nh()) {
            need_pbb_tunnel_ = comp_nh->pbb_nh();
            ret = true;
        }

        if (layer2_control_word_ != comp_nh->layer2_control_word()) {
            layer2_control_word_ = comp_nh->layer2_control_word();
            ret = true;
        }
        break;
    }

    case NextHop::VLAN: {
        VlanNH *vlan_nh = static_cast<VlanNH *>(e);
        smac_ = vlan_nh->GetSMac();
        dmac_ = vlan_nh->GetDMac();
        ret = false;
        break;
    }

    case NextHop::VRF: {
        VrfNH *vrf_nh = static_cast<VrfNH *>(e);
        vrf_id_ = vrf_nh->GetVrf()->vrf_id();
        ret = false;
        if (bridge_nh_ != vrf_nh->bridge_nh()) {
            bridge_nh_ = vrf_nh->bridge_nh();
            ret = true;
        }

        if (flood_unknown_unicast_ != vrf_nh->flood_unknown_unicast()) {
            flood_unknown_unicast_ = vrf_nh->flood_unknown_unicast();
            ret = true;
        }

        if (layer2_control_word_ != vrf_nh->layer2_control_word()) {
            layer2_control_word_ = vrf_nh->layer2_control_word();
            ret = true;
        }
        break;
    }
    case NextHop::RECEIVE:
    case NextHop::RESOLVE: {
        valid_ = true;
        ret = true;
        break;
    }

    default:
        assert(0);
    }

    return ret;
};
int NHKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_nexthop_req encoder;
    int encode_len;
    uint32_t crypt_intf_id = kInvalidIndex;
    uint32_t intf_id = kInvalidIndex;
    std::vector<int8_t> encap;
    InterfaceKSyncEntry *if_ksync = NULL;
    InterfaceKSyncEntry *crypt_if_ksync = NULL;
    Agent *agent = ksync_obj_->ksync()->agent();

    encoder.set_h_op(op);
    encoder.set_nhr_id(nh_id());
    if (op == sandesh_op::DEL) {
        /* For delete only NH-index is required by vrouter */
        int error = 0;
        encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
        assert(error == 0);
        assert(encode_len <= buf_len);
        return encode_len;
    }
    encoder.set_nhr_rid(0);
    encoder.set_nhr_vrf(vrf_id_);
    encoder.set_nhr_family(AF_INET);
    uint32_t flags = 0;
    if (valid_) {
        flags |= NH_FLAG_VALID;
    }

    if (policy_) {
        flags |= NH_FLAG_POLICY_ENABLED;
    }

    if (etree_leaf_ == false) {
        flags |= NH_FLAG_ETREE_ROOT;
    }

    if (learning_enabled_) {
        flags |= NH_FLAG_MAC_LEARN;
    }

    if (layer2_control_word_) {
        flags |= NH_FLAG_L2_CONTROL_DATA;
    }

    if (crypt_) {
        flags |= NH_FLAG_CRYPT_TRAFFIC;
    }

    if_ksync = interface();
    crypt_if_ksync = crypt_interface();

    switch (type_) {
        case NextHop::VLAN:
        case NextHop::ARP:
        case NextHop::INTERFACE :
            encoder.set_nhr_type(NH_ENCAP);
            if (if_ksync) {
                intf_id = if_ksync->interface_id();
            }
            encoder.set_nhr_encap_oif_id(intf_id);
            encoder.set_nhr_encap_family(ETHERTYPE_ARP);

            SetEncap(if_ksync, encap);
            encoder.set_nhr_encap(encap);
            encoder.set_nhr_tun_sip(0);
            encoder.set_nhr_tun_dip(0);
            if (is_vxlan_routing_) {
                //Set vxlan routing flag
                flags |= NH_FLAG_L3_VXLAN;
            }
            if (is_bridge_) {
                encoder.set_nhr_family(AF_BRIDGE);
            }
            if (is_mcast_nh_) {
                flags |= NH_FLAG_MCAST;
            }
            encoder.set_nhr_flags(flags);
            break;

        case NextHop::TUNNEL : {
            encoder.set_nhr_type(NH_TUNNEL);
            encoder.set_nhr_tun_sip(htonl(sip_.to_v4().to_ulong()));
            encoder.set_nhr_tun_dip(htonl(dip_.to_v4().to_ulong()));
            encoder.set_nhr_encap_family(ETHERTYPE_ARP);

            if (if_ksync) {
                intf_id = if_ksync->interface_id();
            }
            encoder.set_nhr_encap_oif_id(intf_id);
            encoder.set_nhr_crypt_traffic(crypt_);
            encoder.set_nhr_crypt_path_available(crypt_path_available_);
            if (crypt_if_ksync && crypt_path_available_) {
                crypt_intf_id = crypt_if_ksync->interface_id();
            }
            encoder.set_nhr_encap_crypt_oif_id(crypt_intf_id);
            SetEncap(if_ksync,encap);
            encoder.set_nhr_encap(encap);
            if (tunnel_type_.GetType() == TunnelType::MPLS_UDP) {
                flags |= NH_FLAG_TUNNEL_UDP_MPLS;
            } else if (tunnel_type_.GetType() == TunnelType::MPLS_GRE) {
                flags |= NH_FLAG_TUNNEL_GRE;
            } else if (tunnel_type_.GetType() == TunnelType::NATIVE) {
                //Ideally we should have created a new type of
                //indirect nexthop to handle NATIVE encap
                //reusing tunnel NH as it provides most of
                //functionality now
                encoder.set_nhr_type(NH_ENCAP);
                encoder.set_nhr_encap_oif_id(intf_id);
                encoder.set_nhr_encap_family(ETHERTYPE_ARP);
                encoder.set_nhr_tun_sip(0);
                encoder.set_nhr_tun_dip(0);
            } else if (tunnel_type_.GetType() == TunnelType::MPLS_OVER_MPLS) {
                flags |= NH_FLAG_TUNNEL_MPLS_O_MPLS;
                if (transport_tunnel_type_ == TunnelType::MPLS_UDP) {
                    flags |= NH_FLAG_TUNNEL_UDP_MPLS;
                } else {
                    flags |= NH_FLAG_TUNNEL_GRE;
                }

                encoder.set_nhr_transport_label(label_);
            } else {
                flags |= NH_FLAG_TUNNEL_VXLAN;
            }
            std::vector<int8_t> rewrite_dmac;
            for (size_t i = 0 ; i < rewrite_dmac_.size(); i++) {
                rewrite_dmac.push_back(rewrite_dmac_[i]);
            }
            encoder.set_nhr_rw_dst_mac(rewrite_dmac);
            //TODO remove set_nhr_pbb_mac
            encoder.set_nhr_pbb_mac(rewrite_dmac);
            if (rewrite_dmac_.IsZero() == false) {
                flags |= NH_FLAG_L3_VXLAN;
            }
            break;
        }
        case NextHop::MIRROR :
            encoder.set_nhr_type(NH_TUNNEL);
            if (sip_.is_v4() && dip_.is_v4()) {
                encoder.set_nhr_tun_sip(htonl(sip_.to_v4().to_ulong()));
                encoder.set_nhr_tun_dip(htonl(dip_.to_v4().to_ulong()));
            } else if (sip_.is_v6() && dip_.is_v6()) {
                encoder.set_nhr_family(AF_INET6);
                Ip6Address::bytes_type bytes = sip_.to_v6().to_bytes();
                std::vector<int8_t> sip_vector(bytes.begin(), bytes.end());
                bytes = dip_.to_v6().to_bytes();
                std::vector<int8_t> dip_vector(bytes.begin(), bytes.end());
                encoder.set_nhr_tun_sip6(sip_vector);
                encoder.set_nhr_tun_dip6(dip_vector);
            }
            encoder.set_nhr_tun_sport(htons(sport_));
            encoder.set_nhr_tun_dport(htons(dport_));
            encoder.set_nhr_encap_family(ETHERTYPE_ARP);

            if (if_ksync) {
                intf_id = if_ksync->interface_id();
            }
            encoder.set_nhr_encap_oif_id(intf_id);
            SetEncap(NULL,encap);
            encoder.set_nhr_encap(encap);
            flags |= NH_FLAG_TUNNEL_UDP;
            if (vrf_id_ != agent->fabric_vrf()->vrf_id()) {
                flags |= NH_FLAG_TUNNEL_SIP_COPY;
            }
            break;

        case NextHop::L2_RECEIVE:
            encoder.set_nhr_type(NH_L2_RCV);
            break;

        case NextHop::DISCARD:
            encoder.set_nhr_type(NH_DISCARD);
            break;

        case NextHop::RECEIVE:
            if (!policy_) {
                flags |= NH_FLAG_RELAXED_POLICY;
            }
            intf_id = if_ksync->interface_id();
            encoder.set_nhr_encap_oif_id(intf_id);
            encoder.set_nhr_type(NH_RCV);
            break;

        case NextHop::RESOLVE:
            if (policy_) {
                //Policy bit is used in agent to copy over the
                //field to ARP nexthop that gets created from
                //resolve NH in case of gateway interface,
                //but the same is not needed in vrouter.
                //If policy bit is enabled then first packet
                //resulting flow with key NH of resolve NH
                //followed by next packet with ARP NH as key
                //resulting in flow drops
                flags &= ~NH_FLAG_POLICY_ENABLED;
            }
            encoder.set_nhr_type(NH_RESOLVE);
            break;

        case NextHop::VRF:
            encoder.set_nhr_type(NH_VXLAN_VRF);
            if (bridge_nh_ == true) {
                encoder.set_nhr_family(AF_BRIDGE);
            }
            if (flood_unknown_unicast_) {
                flags |= NH_FLAG_UNKNOWN_UC_FLOOD;
            }
            break;

        case NextHop::PBB: {
            encoder.set_nhr_family(AF_BRIDGE);
            encoder.set_nhr_type(NH_TUNNEL);
            flags |= (NH_FLAG_TUNNEL_PBB | NH_FLAG_INDIRECT);
            std::vector<int8_t> bmac;
            for (size_t i = 0 ; i < dmac_.size(); i++) {
                bmac.push_back(dmac_[i]);
            }
            encoder.set_nhr_pbb_mac(bmac);
            if (pbb_child_nh_.get()) {
                std::vector<int> sub_nh_list;
                NHKSyncEntry *child_nh =
                    static_cast<NHKSyncEntry *>(pbb_child_nh_.get());
                sub_nh_list.push_back(child_nh->nh_id());
                encoder.set_nhr_nh_list(sub_nh_list);

                std::vector<int> sub_label_list;
                sub_label_list.push_back(pbb_label_);
                encoder.set_nhr_label_list(sub_label_list);
            }
            break;
        }

        case NextHop::COMPOSITE: {
            std::vector<int> sub_nh_id;
            std::vector<int> sub_label_list;
            std::vector<int> sub_flag_list;
            encoder.set_nhr_type(NH_COMPOSITE);
            assert(sip_.is_v4());
            assert(dip_.is_v4());
            /* TODO encoding */
            encoder.set_nhr_tun_sip(htonl(sip_.to_v4().to_ulong()));
            encoder.set_nhr_tun_dip(htonl(dip_.to_v4().to_ulong()));
            encoder.set_nhr_encap_family(ETHERTYPE_ARP);
            encoder.set_nhr_ecmp_config_hash(SetEcmpFieldsToUse());
            /* Proto encode in Network byte order */
            switch (comp_type_) {
            case Composite::L2INTERFACE: {
                flags |= NH_FLAG_COMPOSITE_ENCAP;
                encoder.set_nhr_family(AF_BRIDGE);
                break;
            }
            case Composite::L3INTERFACE: {
                flags |= NH_FLAG_COMPOSITE_ENCAP;
                break;
            }
            case Composite::EVPN: {
                flags |= NH_FLAG_COMPOSITE_EVPN;
                break;
            }
            case Composite::TOR: {
                flags |= NH_FLAG_COMPOSITE_TOR;
                break;
            }
            case Composite::FABRIC: {
                encoder.set_nhr_family(AF_BRIDGE);
                flags |= NH_FLAG_COMPOSITE_FABRIC;
                break;
            }
            case Composite::L3FABRIC: {
                flags |= NH_FLAG_COMPOSITE_FABRIC;
                break;
            }
            case Composite::L2COMP: {
                encoder.set_nhr_family(AF_BRIDGE);
                flags |= NH_FLAG_MCAST;
                if (validate_mcast_src_) {
                    flags |= NH_FLAG_VALIDATE_MCAST_SRC;
                }
                break;
            }
            case Composite::L3COMP: {
                flags |= NH_FLAG_MCAST;
                if (validate_mcast_src_) {
                    flags |= NH_FLAG_VALIDATE_MCAST_SRC;
                }
                break;
            }
            case Composite::MULTIPROTO: {
                encoder.set_nhr_family(AF_UNSPEC);
                break;
            }
            case Composite::ECMP:
            case Composite::LOCAL_ECMP: {
                flags |= NH_FLAG_COMPOSITE_ECMP;
                break;
            }
            case Composite::LU_ECMP: {
                flags |= NH_FLAG_COMPOSITE_ECMP;
                flags |= NH_FLAG_COMPOSITE_LU_ECMP;
                break;
            }
            case Composite::INVALID: {
                assert(0);
            }
            }
            if (need_pbb_tunnel_) {
                flags |= NH_FLAG_TUNNEL_PBB;
            }

            encoder.set_nhr_flags(flags);
            for (KSyncComponentNHList::iterator it = component_nh_list_.begin();
                    it != component_nh_list_.end(); it++) {
                KSyncComponentNH component_nh = *it;
                if (component_nh.nh()) {
                    sub_nh_id.push_back(component_nh.nh()->nh_id());
                    sub_label_list.push_back(component_nh.label());
                } else {
                    sub_nh_id.push_back(CompositeNH::kInvalidComponentNHIdx);
                    sub_label_list.push_back(MplsTable::kInvalidLabel);
                }
            }
            encoder.set_nhr_nh_list(sub_nh_id);
            encoder.set_nhr_label_list(sub_label_list);
            break;
        }
        default:
            assert(0);
    }
    encoder.set_nhr_flags(flags);
    int error = 0;
    encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    assert(error == 0);
    assert(encode_len <= buf_len);
    return encode_len;
}

void NHKSyncEntry::FillObjectLog(sandesh_op::type op, KSyncNhInfo &info)
    const {
    info.set_index(nh_id());

    if (op == sandesh_op::ADD) {
        info.set_operation("ADD/CHANGE");
    } else {
        info.set_operation("DELETE");
    }

    switch(type_) {
    case NextHop::L2_RECEIVE: {
        info.set_type("L2-RECEIVE");
        break;
    }
    case NextHop::DISCARD: {
        info.set_type("DISCARD");
        break;
    }

    case NextHop::RECEIVE: {
        info.set_type("RECEIVE");
        break;
    }

    case NextHop::RESOLVE: {
        info.set_type("RESOLVE");
        break;
    }

    case NextHop::ARP: {
        info.set_type("ARP");
        //Fill dmac and smac???
        break;
    }

    case NextHop::VRF: {
        info.set_type("VRF");
        info.set_vrf(vrf_id_);
        break;
    }

    case NextHop::INTERFACE: {
        info.set_type("INTERFACE");
        break;
    }

    case NextHop::TUNNEL: {
        info.set_type("TUNNEL");
        info.set_sip(sip_.to_string());
        info.set_dip(dip_.to_string());
        info.set_crypt_path_available(crypt_);

        if (crypt_interface()) {
            info.set_crypt_intf_name(crypt_interface()->interface_name());
            info.set_crypt_out_if_index(crypt_interface()->interface_id());
        } else {
            info.set_crypt_intf_name("NULL");
            info.set_crypt_out_if_index(kInvalidIndex);
        }
        info.set_rewrite_dmac(rewrite_dmac_.ToString());
        break;
    }

    case NextHop::MIRROR: {
        info.set_type("MIRROR");
        info.set_vrf(vrf_id_);
        info.set_sip(sip_.to_string());
        info.set_dip(dip_.to_string());
        info.set_sport(sport_);
        info.set_dport(dport_);
        break;
    }

    case NextHop::VLAN: {
        info.set_type("VLAN");
        info.set_vlan_tag(vlan_tag_);
        break;
    }
    case NextHop::COMPOSITE: {
        info.set_type("Composite");
        info.set_sub_type(comp_type_);
        info.set_dip(dip_.to_string());
        info.set_vrf(vrf_id_);
        std::vector<KSyncComponentNHLog> sub_nh_list;
        for (KSyncComponentNHList::const_iterator it =
                component_nh_list_.begin();
             it != component_nh_list_.end(); it++) {
            KSyncComponentNH component_nh = *it;
            if (component_nh.nh()) {
                KSyncComponentNHLog sub_nh;
                sub_nh.set_nh_idx(component_nh.nh()->nh_id());
                sub_nh.set_label(component_nh.label());
                sub_nh_list.push_back(sub_nh);
            }
        }
        info.set_sub_nh_list(sub_nh_list);
        break;
    }
    default: {
        return;
    }
    }

    info.set_policy(policy_);
    info.set_valid(valid_);
    if (interface()) {
        info.set_intf_name(interface()->interface_name());
        info.set_out_if_index(interface()->interface_id());
    } else {
        info.set_intf_name("NULL");
        info.set_out_if_index(kInvalidIndex);
    }
}

int NHKSyncEntry::AddMsg(char *buf, int buf_len) {
    KSyncNhInfo info;
    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(NH, GetObject(), info);

    return Encode(sandesh_op::ADD, buf, buf_len);
}

int NHKSyncEntry::ChangeMsg(char *buf, int buf_len){
    KSyncNhInfo info;
    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(NH, GetObject(), info);

    return Encode(sandesh_op::ADD, buf, buf_len);
}

int NHKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    KSyncNhInfo info;
    FillObjectLog(sandesh_op::DEL, info);
    KSYNC_TRACE(NH, GetObject(), info);

    return Encode(sandesh_op::DEL, buf, buf_len);
}

KSyncEntry *NHKSyncEntry::UnresolvedReference() {
    KSyncEntry *entry = NULL;
    InterfaceKSyncEntry *if_ksync = interface();

    if (valid_ == false) {
        //Invalid nexthop has no reference dependency
        return NULL;
    }

    switch (type_) {
    case NextHop::ARP: {
        assert(if_ksync);
        if (!if_ksync->IsResolved()) {
            entry = if_ksync;
        }
        break;
    }

    case NextHop::VLAN:
    case NextHop::INTERFACE: {
        assert(if_ksync);
        if (!if_ksync->IsResolved()) {
            entry = if_ksync;
        }
        break;
    }

    case NextHop::L2_RECEIVE: {
        assert(if_ksync == NULL);
        break;
    }

    case NextHop::DISCARD: {
        assert(if_ksync == NULL);
        break;
    }

    case NextHop::TUNNEL: {
        if (crypt_path_available_) {
            InterfaceKSyncEntry *crypt_if_ksync = crypt_interface();
            assert(crypt_if_ksync);
            if (!crypt_if_ksync->IsResolved()) {
                entry = crypt_if_ksync;
            }
        }
        break;
    }

    case NextHop::MIRROR: {
        break;
    }

    case NextHop::RECEIVE: {
        assert(if_ksync);
        if (!if_ksync->IsResolved()) {
            entry = if_ksync;
        }
        break;
    }

    case NextHop::VRF: {
        break;
    }

    case NextHop::RESOLVE: {
        break;
    }

    case NextHop::PBB: {
        if (pbb_child_nh_ && pbb_child_nh_->IsResolved() == false) {
             entry = pbb_child_nh_.get();
        }
        break;
    }

    case NextHop::COMPOSITE: {
        for (KSyncComponentNHList::const_iterator it =
                                component_nh_list_.begin();
             it != component_nh_list_.end(); it++) {
            KSyncComponentNH component_nh = *it;
            if (component_nh.nh() && !(component_nh.nh()->IsResolved())) {
                entry = component_nh.nh();
                break;
            }
        }
        break;
    }

    default:
        assert(0);
        break;
    }
    return entry;
}

static bool NeedRewrite(NHKSyncEntry *entry, InterfaceKSyncEntry *if_ksync) {

    // If interface has RAW-IP encapsulation, it doesnt need any rewrite
    // information
    if (if_ksync && if_ksync->encap_type() == PhysicalInterface::RAW_IP) {
        return false;
    }

    // For bridge nexthops, only INTERFACE nexthop has rewrite NH
    if (entry->is_bridge() == true) {
        if (entry->type() == NextHop::INTERFACE)
            return true;
        return false;
    }

    return true;
}

void NHKSyncEntry::SetEncap(InterfaceKSyncEntry *if_ksync,
                            std::vector<int8_t> &encap) {

    if (NeedRewrite(this, if_ksync) == false)
        return;

    const MacAddress *smac = &MacAddress::ZeroMac();
    /* DMAC encode */
    for (size_t i = 0 ; i < dmac_.size(); i++) {
        encap.push_back(dmac_[i]);
    }
    /* SMAC encode */
    if (type_ == NextHop::VLAN) {
        smac = &smac_;
    } else if ((type_ == NextHop::INTERFACE || type_ == NextHop::ARP)
                    && if_ksync) {
        smac = &if_ksync->mac();

    } else {
        Agent *agent = ksync_obj_->ksync()->agent();
        smac = &agent->vhost_interface()->mac();
    }
    for (size_t i = 0 ; i < smac->size(); i++) {
        encap.push_back((*smac)[i]);
    }

    // Add 802.1q header if
    //  - Nexthop is of type VLAN
    //  - Nexthop is of type INTERFACE and VLAN configured for it
    if (type_ == NextHop::VLAN ||
            (type_ == NextHop::INTERFACE &&
             vlan_tag_ != VmInterface::kInvalidVlanId)) {
        encap.push_back(0x81);
        encap.push_back(0x00);
        encap.push_back((vlan_tag_ & 0xFF00) >> 8);
        encap.push_back(vlan_tag_ & 0xFF);
    }
    /* Proto encode in Network byte order */
    encap.push_back(0x08);
    encap.push_back(0x00);
}

uint8_t NHKSyncEntry::SetEcmpFieldsToUse() {
    uint8_t ecmp_hash_fields_in_use = VR_FLOW_KEY_NONE;
    uint8_t fields_in_byte = ecmp_hash_fieds_.HashFieldsToUse();
    if (fields_in_byte & 1 << EcmpLoadBalance::SOURCE_IP) {
        ecmp_hash_fields_in_use |= VR_FLOW_KEY_SRC_IP;
    }
    if (fields_in_byte & 1 << EcmpLoadBalance::DESTINATION_IP) {
        ecmp_hash_fields_in_use |= VR_FLOW_KEY_DST_IP;
    }
    if (fields_in_byte & 1 << EcmpLoadBalance::IP_PROTOCOL) {
        ecmp_hash_fields_in_use |= VR_FLOW_KEY_PROTO;
    }
    if (fields_in_byte & 1 << EcmpLoadBalance::SOURCE_PORT) {
        ecmp_hash_fields_in_use |= VR_FLOW_KEY_SRC_PORT;
    }
    if (fields_in_byte & 1 << EcmpLoadBalance::DESTINATION_PORT) {
        ecmp_hash_fields_in_use |= VR_FLOW_KEY_DST_PORT;
    }

    return ecmp_hash_fields_in_use;

}

NHKSyncObject::NHKSyncObject(KSync *ksync) :
    KSyncDBObject("KSync Nexthop"), ksync_(ksync) {
}

NHKSyncObject::~NHKSyncObject() {
}

void NHKSyncObject::RegisterDBClients() {
    RegisterDb(ksync_->agent()->nexthop_table());
}

KSyncEntry *NHKSyncObject::Alloc(const KSyncEntry *entry, uint32_t index) {
    const NHKSyncEntry *nh = static_cast<const NHKSyncEntry *>(entry);
    NHKSyncEntry *ksync = new NHKSyncEntry(this, nh, index);
    return static_cast<KSyncEntry *>(ksync);
}

KSyncEntry *NHKSyncObject::DBToKSyncEntry(const DBEntry *e) {
    const NextHop *nh = static_cast<const NextHop *>(e);
    NHKSyncEntry *key = new NHKSyncEntry(this, nh);
    return static_cast<KSyncEntry *>(key);
}

void vr_nexthop_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->NHMsgHandler(this);
}

void NHKSyncEntry::SetKSyncNhListSandeshData(KSyncNhListSandeshData *data) const {
    data->set_vrf_id(vrf_id_);
    data->set_label(label_);
    if (valid_) {
        data->set_valid("Enable");
    } else {
        data->set_valid("Disable");
    }

    if (policy_) {
        data->set_policy("Enable");
    } else {
        data->set_policy("Disable");
    }

    if (is_mcast_nh_) {
        data->set_is_mcast_nh("True");
    } else {
        data->set_is_mcast_nh("False");
    }

    if (defer_) {
        data->set_defer("Enable");
    } else {
        data->set_defer("Disable");
    }

    data->set_vlan_tag(vlan_tag_);

    if (is_local_ecmp_nh_) {
        data->set_is_local_ecmp_nh("True");
    } else {
        data->set_is_local_ecmp_nh("False");
    }

    if (is_bridge_) {
        data->set_is_bridge("True");
    } else {
        data->set_is_bridge("False");
    }

    if (bridge_nh_) {
        data->set_bridge_nh("Enable");
    } else {
        data->set_bridge_nh("Disable");
    }

    if (flood_unknown_unicast_) {
        data->set_flood_unknown_unicast("Enable");
    } else {
        data->set_flood_unknown_unicast("Disable");
    }

    data->set_nh_id(nh_id_);
    data->set_isid(isid_);
    return;
}

bool NHKSyncEntry::KSyncEntrySandesh(Sandesh *sresp) {
    KSyncNhListResp *resp = static_cast<KSyncNhListResp *> (sresp);

    KSyncNhListSandeshData data;
    SetKSyncNhListSandeshData(&data);
    std::vector<KSyncNhListSandeshData> &list =
            const_cast<std::vector<KSyncNhListSandeshData>&>(resp->get_KSyncNhList_list());
    list.push_back(data);

    return true;
}

void KSyncNhListReq::HandleRequest() const {
    AgentKsyncSandeshPtr sand(new AgentKsyncNhListSandesh(context()));

    sand->DoKsyncSandesh(sand);

}
