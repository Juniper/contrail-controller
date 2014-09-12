/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#if defined(__linux__)
#include <netinet/ether.h>
#elif defined(__FreeBSD__)
#include "vr_os.h"
#include <net/if.h>
#include "nl_util.h"
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
#include "ksync/interface_ksync.h"
#include "ksync/nexthop_ksync.h"
#include "ksync_init.h"
#include "vr_types.h"


NHKSyncEntry::NHKSyncEntry(NHKSyncObject *obj, const NHKSyncEntry *entry,
                           uint32_t index) :
    KSyncNetlinkDBEntry(index), ksync_obj_(obj), type_(entry->type_),
    vrf_id_(entry->vrf_id_), label_(entry->label_),
    interface_(entry->interface_), sip_(entry->sip_), dip_(entry->dip_),
    sport_(entry->sport_), dport_(entry->dport_), smac_(entry->smac_),
    dmac_(entry->dmac_), valid_(entry->valid_), policy_(entry->policy_),
    is_mcast_nh_(entry->is_mcast_nh_), defer_(entry->defer_),
    component_nh_list_(entry->component_nh_list_),
    nh_(entry->nh_), vlan_tag_(entry->vlan_tag_),
    is_local_ecmp_nh_(entry->is_local_ecmp_nh_),
    is_layer2_(entry->is_layer2_), comp_type_(entry->comp_type_),
    tunnel_type_(entry->tunnel_type_), prefix_len_(entry->prefix_len_),
    nh_id_(entry->nh_id()),
    component_nh_key_list_(entry->component_nh_key_list_) {
}

NHKSyncEntry::NHKSyncEntry(NHKSyncObject *obj, const NextHop *nh) :
    KSyncNetlinkDBEntry(kInvalidIndex), ksync_obj_(obj), type_(nh->GetType()),
    vrf_id_(0), interface_(NULL), valid_(nh->IsValid()),
    policy_(nh->PolicyEnabled()), is_mcast_nh_(false), nh_(nh),
    vlan_tag_(VmInterface::kInvalidVlanId), is_layer2_(false),
    tunnel_type_(TunnelType::INVALID), prefix_len_(32), nh_id_(nh->id()) {

    sip_.s_addr = 0;
    memset(&dmac_, 0, sizeof(dmac_));
    switch (type_) {
    case NextHop::ARP: {
        const ArpNH *arp = static_cast<const ArpNH *>(nh);
        vrf_id_ = arp->vrf_id();
        sip_.s_addr = arp->GetIp()->to_ulong();
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
        is_layer2_ = if_nh->IsLayer2();
        vrf_id_ = if_nh->GetVrf()->vrf_id();
        // VmInterface can potentially have vlan-tags. Get tag in such case
        if (if_nh->GetInterface()->type() == Interface::VM_INTERFACE) {
            vlan_tag_ = (static_cast<const VmInterface *>
                         (if_nh->GetInterface()))->vlan_id();
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
        sip_.s_addr = tunnel->GetSip()->to_ulong();
        dip_.s_addr = tunnel->GetDip()->to_ulong();
        tunnel_type_ = tunnel->GetTunnelType();
        break;
    }

    case NextHop::DISCARD: {
        break;
    }

    case NextHop::RESOLVE: {
        break;
    }

    case NextHop::VRF: {
        const VrfNH *vrf_nh = static_cast<const VrfNH *>(nh);
        vrf_id_ = vrf_nh->GetVrf()->vrf_id();
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
        sip_.s_addr = mirror_nh->GetSip()->to_ulong();
        sport_ = mirror_nh->GetSPort();
        dip_.s_addr = mirror_nh->GetDip()->to_ulong();
        dport_ = mirror_nh->GetDPort();
        break;
    }
 
    case NextHop::COMPOSITE: {
        const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
        component_nh_list_.clear();
        vrf_id_ = comp_nh->vrf()->vrf_id();
        comp_type_ = comp_nh->composite_nh_type();
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

KSyncDBObject *NHKSyncEntry::GetObject() {
    return ksync_obj_;
}

bool NHKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const NHKSyncEntry &entry = static_cast<const NHKSyncEntry &>(rhs);

    if (type_ != entry.type_) {
        return type_ < entry.type_;
    }

    if (type_ == NextHop::DISCARD || type_ == NextHop::RESOLVE) {
        return false;
    }

    if (policy_ != entry.policy_) {
        return policy_ < entry.policy_;
    }

    if (type_ == NextHop::VRF) {
        return vrf_id_ < entry.vrf_id_;
    }

    if (type_ == NextHop::ARP) {
        if (vrf_id_ != entry.vrf_id_) {
            return vrf_id_ < entry.vrf_id_;
        }
        return sip_.s_addr < entry.sip_.s_addr;
    }

    if (type_ == NextHop::INTERFACE) {
        if (is_mcast_nh_ != entry.is_mcast_nh_) {
            return is_mcast_nh_ < entry.is_mcast_nh_;
        }
        if(is_layer2_ != entry.is_layer2_) {
            return is_layer2_ < entry.is_layer2_;
        }
        return interface() < entry.interface();
    }

    if (type_ == NextHop::RECEIVE) {
        return interface() < entry.interface();
    }

    if (type_ == NextHop::TUNNEL) {
        if (vrf_id_ != entry.vrf_id_) {
            return vrf_id_ < entry.vrf_id_;
        }

        if (sip_.s_addr != entry.sip_.s_addr) {
            return sip_.s_addr < entry.sip_.s_addr;
        }

        if (dip_.s_addr != entry.dip_.s_addr) {
            return dip_.s_addr < entry.dip_.s_addr;
        }

        return tunnel_type_.IsLess(entry.tunnel_type_);
    }

    if (type_ == NextHop::MIRROR) {
        if (vrf_id_ != entry.vrf_id_) {
            return vrf_id_ < entry.vrf_id_;
        }

        if (dip_.s_addr != entry.dip_.s_addr) {
            return dip_.s_addr < entry.dip_.s_addr;
        }

        return dport_ < entry.dport_;
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
        s << inet_ntoa(dip_);
        break;
    }
    case NextHop::MIRROR: {
        s << "Mirror to ";
        s << inet_ntoa(dip_) << ": " << dport_;
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

    switch (nh->GetType()) {
    case NextHop::ARP: {
        InterfaceKSyncObject *interface_object =
            ksync_obj_->ksync()->interface_ksync_obj();
        ArpNH *arp_nh = static_cast<ArpNH *>(e);
        dmac_ = arp_nh->GetMac();
        if (valid_) {
            InterfaceKSyncEntry if_ksync(interface_object,
                                         arp_nh->GetInterface());
            interface_ = interface_object->GetReference(&if_ksync);
        } else {
            interface_ = NULL;
            dmac_.Zero();
        }
        ret = true;
        break;
    }

    case NextHop::INTERFACE: {
        InterfaceNH *intf_nh = static_cast<InterfaceNH *>(e);
        dmac_ = intf_nh->GetDMac();
        uint16_t vlan_tag = VmInterface::kInvalidVlanId;
        if (intf_nh->GetInterface()->type() == Interface::VM_INTERFACE) {
            vlan_tag = vlan_tag_;
            vlan_tag_ = (static_cast<const VmInterface *>
                         (intf_nh->GetInterface()))->vlan_id();
            ret = vlan_tag != vlan_tag_;
        }
        break;
    }

    case NextHop::DISCARD: {
        ret = false;
        break;
    }

    case NextHop::TUNNEL: {
        ret = true;
        // Invalid nexthop, no valid interface or mac info
        // present, just return
        if (valid_ == false) {
            interface_ = NULL;
            memset(&dmac_, 0, sizeof(dmac_));
            break;
        }

        const TunnelNH *tun_nh = static_cast<TunnelNH *>(e);
        const NextHop *active_nh = tun_nh->GetRt()->GetActiveNextHop();
        if (active_nh->GetType() != NextHop::ARP) {
            valid_ = false;
            interface_ = NULL;
            memset(&dmac_, 0, sizeof(dmac_));
            break;
        }
        const ArpNH *arp_nh = static_cast<const ArpNH *>(active_nh);
        InterfaceKSyncObject *interface_object =
            ksync_obj_->ksync()->interface_ksync_obj();
        InterfaceKSyncEntry if_ksync(interface_object, arp_nh->GetInterface());
        interface_ = interface_object->GetReference(&if_ksync);
        dmac_ = arp_nh->GetMac();
        break;
    }

    case NextHop::MIRROR: {
        ret = true;
        // Invalid nexthop, no valid interface or mac info
        // present, just return
        if (valid_ == false || (vrf_id_ == (uint32_t)-1)) {
            interface_ = NULL;
            memset(&dmac_, 0, sizeof(dmac_));
            break;
        }

        const MirrorNH *mirror_nh = static_cast<MirrorNH *>(e);
        const NextHop *active_nh = mirror_nh->GetRt()->GetActiveNextHop();
        if (active_nh->GetType() == NextHop::ARP) {
            const ArpNH *arp_nh = static_cast<const ArpNH *>(active_nh);
            InterfaceKSyncObject *interface_object =
                ksync_obj_->ksync()->interface_ksync_obj();
            InterfaceKSyncEntry if_ksync(interface_object,
                                         arp_nh->GetInterface());
            interface_ = interface_object->GetReference(&if_ksync);
            //dmac_ = *(arp_nh->GetMac());
            dmac_ = arp_nh->GetMac();
        } else if (active_nh->GetType() == NextHop::RECEIVE) {
            const ReceiveNH *rcv_nh = static_cast<const ReceiveNH *>(active_nh);
            InterfaceKSyncObject *interface_object =
                ksync_obj_->ksync()->interface_ksync_obj();
            InterfaceKSyncEntry if_ksync(interface_object,
                                         rcv_nh->GetInterface());
            interface_ = interface_object->GetReference(&if_ksync);
            Agent *agent = ksync_obj_->ksync()->agent();
            memcpy(&dmac_, &agent->vhost_interface()->mac(), sizeof(dmac_));
        } else if (active_nh->GetType() == NextHop::DISCARD) {
            valid_ = false;
            interface_ = NULL;
            memset(&dmac_, 0, sizeof(dmac_));
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
    int encode_len, error;
    uint32_t intf_id = kInvalidIndex;
    std::vector<int8_t> encap;
    InterfaceKSyncEntry *if_ksync = NULL;

    encoder.set_h_op(op);
    encoder.set_nhr_id(nh_id());
    if (op == sandesh_op::DELETE) {
        /* For delete only NH-index is required by vrouter */
        encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
        return encode_len;
    }
    encoder.set_nhr_rid(0);
    encoder.set_nhr_vrf(vrf_id_);
    encoder.set_nhr_family(AF_INET);
    encoder.set_nhr_label(MplsTable::kInvalidLabel);
    uint16_t flags = 0;
    if (valid_) {
        flags |= NH_FLAG_VALID;
    }

    if (policy_) {
        flags |= NH_FLAG_POLICY_ENABLED;
    }
    if_ksync = interface();

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
            if (is_layer2_) {
                flags |= 0x0004;
                encoder.set_nhr_family(AF_BRIDGE);
            }
            if (is_mcast_nh_) {
                flags |= 0x0020;
                encoder.set_nhr_flags(flags);
            }
            break;

        case NextHop::TUNNEL :
            encoder.set_nhr_type(NH_TUNNEL);
            encoder.set_nhr_tun_sip(htonl(sip_.s_addr));
            encoder.set_nhr_tun_dip(htonl(dip_.s_addr));
            encoder.set_nhr_encap_family(ETHERTYPE_ARP);

            if (if_ksync) {
                intf_id = if_ksync->interface_id();
            }
            encoder.set_nhr_encap_oif_id(intf_id);

            SetEncap(NULL,encap);
            encoder.set_nhr_encap(encap);
            if (tunnel_type_.GetType() == TunnelType::MPLS_UDP) {
                flags |= NH_FLAG_TUNNEL_UDP_MPLS;
            } else if (tunnel_type_.GetType() == TunnelType::MPLS_GRE) {
                flags |= NH_FLAG_TUNNEL_GRE;
            } else {
                flags |= NH_FLAG_TUNNEL_VXLAN;
            }
            break;

        case NextHop::MIRROR :
            encoder.set_nhr_type(NH_TUNNEL);
            encoder.set_nhr_tun_sip(htonl(sip_.s_addr));
            encoder.set_nhr_tun_dip(htonl(dip_.s_addr));
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
            encoder.set_nhr_type(NH_RESOLVE);
            break;

        case NextHop::VRF:
            encoder.set_nhr_type(NH_VXLAN_VRF);
            break;

        case NextHop::COMPOSITE: {
            std::vector<int> sub_nh_id;
            std::vector<int> sub_label_list;
            encoder.set_nhr_type(NH_COMPOSITE);
            /* TODO encoding */
            encoder.set_nhr_tun_sip(htonl(sip_.s_addr));
            encoder.set_nhr_tun_dip(htonl(dip_.s_addr));
            encoder.set_nhr_encap_family(ETHERTYPE_ARP);

            /* Proto encode in Network byte order */
            switch (comp_type_) {
            case Composite::FABRIC: {
                flags |= NH_FLAG_COMPOSITE_FABRIC;
                break;
            }
            case Composite::L2COMP: {
                encoder.set_nhr_family(AF_BRIDGE);
                flags |= NH_FLAG_MCAST;
                flags |= NH_FLAG_COMPOSITE_L2;
                break;
            }
            case Composite::L3COMP: {
                flags |= NH_FLAG_MCAST;
                flags |= NH_FLAG_COMPOSITE_L3;
                break;
            }
            case Composite::MULTIPROTO: {
                encoder.set_nhr_family(AF_UNSPEC);
                flags |= NH_FLAG_COMPOSITE_MULTI_PROTO;
                break;
            }
            case Composite::ECMP:
            case Composite::LOCAL_ECMP: {
                flags |= NH_FLAG_COMPOSITE_ECMP;
                break;
            }
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
    encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
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
        info.set_sip(inet_ntoa(sip_));
        info.set_dip(inet_ntoa(dip_));
        break;
    }

    case NextHop::MIRROR: {
        info.set_type("MIRROR");
        info.set_vrf(vrf_id_);
        info.set_sip(inet_ntoa(sip_));
        info.set_dip(inet_ntoa(dip_));
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
        info.set_dip(inet_ntoa(dip_));
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
    KSYNC_TRACE(NH, info);
 
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int NHKSyncEntry::ChangeMsg(char *buf, int buf_len){
    KSyncNhInfo info;
    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(NH, info);

    return Encode(sandesh_op::ADD, buf, buf_len);
}

int NHKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    KSyncNhInfo info;
    FillObjectLog(sandesh_op::DELETE, info);
    KSYNC_TRACE(NH, info);

    return Encode(sandesh_op::DELETE, buf, buf_len);
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

    case NextHop::DISCARD: {
        assert(if_ksync == NULL);
        break;
    }

    case NextHop::TUNNEL: {
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

void NHKSyncEntry::SetEncap(InterfaceKSyncEntry *if_ksync,
                            std::vector<int8_t> &encap) {

    if (is_layer2_ == true) {
        return;
    }

    MacAddress smac;
    /* DMAC encode */
    for (size_t i = 0; i < smac.size(); i++) {
        encap.push_back((int8_t)smac[i]);
    }
    /* SMAC encode */
    if (type_ == NextHop::VLAN) {
        smac = smac_;
    } else if ((type_ == NextHop::INTERFACE || type_ == NextHop::ARP)
               && if_ksync) {
    smac = if_ksync->mac();

    } else {
        Agent *agent = ksync_obj_->ksync()->agent();
        smac = agent->vhost_interface()->mac();
    }
    for (size_t i = 0 ; i < smac.size(); i++) {
        encap.push_back((int8_t)smac[i]);
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

NHKSyncObject::NHKSyncObject(KSync *ksync) :
    KSyncDBObject(), ksync_(ksync) {
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

