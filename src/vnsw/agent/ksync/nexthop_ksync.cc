/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <netinet/ether.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <base/logging.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_sock.h>

#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/mirror_table.h"
#include "oper/tunnel_nh.h"
#include "ksync/interface_ksync.h"
#include "ksync/nexthop_ksync.h"

#include "vr_nexthop.h"

#include "ksync_init.h"
#include "vr_types.h"

static uint8_t nil_mac[6] = {0, 0, 0, 0, 0, 0};

void vr_nexthop_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->NHMsgHandler(this);
}

NHKSyncObject *NHKSyncObject::singleton_;

KSyncDBObject *NHKSyncEntry::GetObject() { 
    return NHKSyncObject::GetKSyncObject();
}

NHKSyncEntry::NHKSyncEntry(const NextHop *nh) :
    KSyncNetlinkDBEntry(kInvalidIndex), type_(nh->GetType()), vrf_id_(0),
    interface_(NULL), valid_(nh->IsValid()), policy_(nh->PolicyEnabled()),
    is_mcast_nh_(false), nh_(nh), vlan_tag_(0), is_layer2_(false),
    tunnel_type_(TunnelType::INVALID)  {

    sip_.s_addr = 0;
    memset(&dmac_, 0, sizeof(dmac_));
    switch (type_) {
    case NextHop::ARP: {
        const ArpNH *arp = static_cast<const ArpNH *>(nh);
        vrf_id_ = arp->GetVrfId();
        sip_.s_addr = arp->GetIp()->to_ulong();
        break;
    }

    case NextHop::INTERFACE: {
        IntfKSyncObject *interface_object = IntfKSyncObject::GetKSyncObject();
        const InterfaceNH *if_nh = static_cast<const InterfaceNH *>(nh);
        IntfKSyncEntry interface(if_nh->GetInterface());
        interface_ = interface_object->GetReference(&interface);
        assert(interface_);
        is_mcast_nh_ = if_nh->IsMulticastNH();
        is_layer2_ = if_nh->IsLayer2();
        vrf_id_ = if_nh->GetVrf()->GetVrfId();
        // VmInterface can potentially have vlan-tags. Get tag in such case
        if (if_nh->GetInterface()->type() == Interface::VM_INTERFACE) {
            vlan_tag_ = (static_cast<const VmInterface *>
                         (if_nh->GetInterface()))->vlan_id();
        }
        break;
    }

    case NextHop::VLAN: {
        IntfKSyncObject *interface_object = IntfKSyncObject::GetKSyncObject();
        const VlanNH *vlan_nh = static_cast<const VlanNH *>(nh);
        IntfKSyncEntry interface(vlan_nh->GetInterface());
        interface_ = interface_object->GetReference(&interface);
        assert(interface_);
        vlan_tag_ = vlan_nh->GetVlanTag();
        vrf_id_ = vlan_nh->GetVrf()->GetVrfId();
        break;
    }

    case NextHop::TUNNEL: {
        const TunnelNH *tunnel = static_cast<const TunnelNH *>(nh);
        vrf_id_ = tunnel->GetVrfId();
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
        vrf_id_ = vrf_nh->GetVrf()->GetVrfId();
        break;
    }

    case NextHop::RECEIVE: {
        IntfKSyncObject *interface_object = IntfKSyncObject::GetKSyncObject();
        const ReceiveNH *rcv_nh = static_cast<const ReceiveNH *>(nh);
        IntfKSyncEntry interface(rcv_nh->GetInterface());
        interface_ = interface_object->GetReference(&interface);
        vrf_id_ = rcv_nh->GetInterface()->GetVrfId();
        assert(interface_);
        break;
    }

    case NextHop::MIRROR: {
        const MirrorNH *mirror_nh = static_cast<const MirrorNH *>(nh);
        vrf_id_ = mirror_nh->GetVrfId();
        sip_.s_addr = mirror_nh->GetSip()->to_ulong();
        sport_ = mirror_nh->GetSPort();
        dip_.s_addr = mirror_nh->GetDip()->to_ulong();
        dport_ = mirror_nh->GetDPort();
        break;
    }
 
    case NextHop::COMPOSITE: {
        const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
        //vrf_id_ = comp_nh->GetVrfId();
        vrf_id_ = (Agent::GetInstance()->GetVrfTable()->
                   FindVrfFromName(comp_nh->GetVrfName()))->GetVrfId();
        sip_.s_addr = comp_nh->GetSrcAddr().to_ulong();
        dip_.s_addr = comp_nh->GetGrpAddr().to_ulong();
        is_mcast_nh_ = comp_nh->IsMcastNH();
        is_local_ecmp_nh_ = comp_nh->IsLocal();
        comp_type_ = comp_nh->CompositeType();
        component_nh_list_.clear();
        break;
    }

    default:
        assert(0);
        break;
    }
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
        return GetIntf() < entry.GetIntf();
    }

    if (type_ == NextHop::RECEIVE) {
        return GetIntf() < entry.GetIntf();
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
        if (vrf_id_ != entry.vrf_id_) {
            return vrf_id_ < entry.vrf_id_;
        }

        if (is_local_ecmp_nh_ != entry.is_local_ecmp_nh_) {
            return is_local_ecmp_nh_ < entry.is_local_ecmp_nh_;
        }

        if (comp_type_ != entry.comp_type_) {
            return comp_type_ < entry.comp_type_;
        }

        if (sip_.s_addr != entry.sip_.s_addr) {
            return sip_.s_addr < entry.sip_.s_addr;
        }

        return dip_.s_addr < entry.dip_.s_addr;
    }

    if (type_ == NextHop::VLAN) {
        if (GetIntf() != entry.GetIntf()) {
            return GetIntf() < entry.GetIntf();

        }

        return vlan_tag_ < entry.vlan_tag_;
    }

    return false;

    assert(0);
}

std::string NHKSyncEntry::ToString() const {
    std::stringstream s;
    s << "NH : " << GetIndex() << " Type :" << type_;
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
        IntfKSyncObject *interface_object = IntfKSyncObject::GetKSyncObject();
        ArpNH *arp_nh = static_cast<ArpNH *>(e);
        dmac_ = *(arp_nh->GetMac());
        if (valid_) {
            IntfKSyncEntry interface(arp_nh->GetInterface());
            interface_ = interface_object->GetReference(&interface);
        } else {
            interface_ = NULL;
            memset(&dmac_, 0, sizeof(dmac_));
        }
        ret = true;
        break;
    }

    case NextHop::INTERFACE: {
        InterfaceNH *intf_nh = static_cast<InterfaceNH *>(e);
        dmac_ = intf_nh->GetDMac();
        uint16_t vlan_tag = VmInterface::kInvalidVlanId;
        if (intf_nh->GetInterface()->type() == Interface::VM_INTERFACE) {
            vlan_tag_ = (static_cast<const VmInterface *>
                         (intf_nh->GetInterface()))->vlan_id();
        }

        ret = vlan_tag != vlan_tag_;
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
        IntfKSyncObject *interface_object = IntfKSyncObject::GetKSyncObject();
        IntfKSyncEntry interface(arp_nh->GetInterface());
        interface_ = interface_object->GetReference(&interface);
        dmac_ = *(arp_nh->GetMac());
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
            IntfKSyncObject *interface_object = IntfKSyncObject::GetKSyncObject();
            IntfKSyncEntry interface(arp_nh->GetInterface());
            interface_ = interface_object->GetReference(&interface);
            dmac_ = *(arp_nh->GetMac());
        } else if (active_nh->GetType() == NextHop::RECEIVE) {
            const ReceiveNH *rcv_nh = static_cast<const ReceiveNH *>(active_nh);
            IntfKSyncObject *interface_object = IntfKSyncObject::GetKSyncObject();
            IntfKSyncEntry interface(rcv_nh->GetInterface());
            interface_ = interface_object->GetReference(&interface);
            memcpy(&dmac_, 
                   IntfKSyncObject::GetKSyncObject()->PhysicalIntfMac(),
                   sizeof(dmac_));
        } else if (active_nh->GetType() == NextHop::DISCARD) {
            valid_ = false;
            interface_ = NULL;
            memset(&dmac_, 0, sizeof(dmac_));
        }
        break;
    }

    case NextHop::COMPOSITE: {
        const CompositeNH *composite_nh = static_cast<const CompositeNH *>(e);
        ret = true;
        valid_ = true;

        is_mcast_nh_ = composite_nh->IsMcastNH();

        //Iterate thru all sub nh to fill component NH list
        component_nh_list_.clear();
        CompositeNH::ComponentNHList::const_iterator component_nh_it = 
            composite_nh->begin();

        while (component_nh_it != composite_nh->end()) {
            const NextHop *component_nh = NULL;
            uint32_t label = 0;
            if (*component_nh_it) {
               component_nh =  (*component_nh_it)->GetNH();
               label = (*component_nh_it)->GetLabel();
            }
            KSyncEntry *ksync_nh = NULL;
            if (component_nh != NULL) {
                NHKSyncEntry nhksync(component_nh);
                NHKSyncObject *nh_object = NHKSyncObject::GetKSyncObject();
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
        vrf_id_ = vrf_nh->GetVrf()->GetVrfId();
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
    const uint8_t *smac = nil_mac;
    IntfKSyncEntry *interface = NULL;

    encoder.set_h_op(op);
    encoder.set_nhr_id(GetIndex());
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
    interface = GetIntf();

    switch (type_) {
        case NextHop::VLAN:
        case NextHop::ARP: 
        case NextHop::INTERFACE : 
            encoder.set_nhr_type(NH_ENCAP);
            if (interface) {
                intf_id = interface->id();
            }
            encoder.set_nhr_encap_oif_id(intf_id);
            encoder.set_nhr_encap_family(ETH_P_ARP);

            /* DMAC encode */
            for (int i = 0 ; i < ETHER_ADDR_LEN; i++) {
                encap.push_back(dmac_.ether_addr_octet[i]);
            }
            /* SMAC encode */
            if (type_ == NextHop::VLAN) {
                smac = smac_.ether_addr_octet;
            } else {
                smac = (const uint8_t *)
                    IntfKSyncObject::GetKSyncObject()->PhysicalIntfMac();
            }
            for (int i = 0 ; i < ETHER_ADDR_LEN; i++) {
                encap.push_back(smac[i]);
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
            encoder.set_nhr_encap_family(ETH_P_ARP);

            if (interface) {
                intf_id = interface->id();
            }
            encoder.set_nhr_encap_oif_id(intf_id);

            /* DMAC encode */
            for (int i = 0 ; i < ETHER_ADDR_LEN; i++) {
                encap.push_back(dmac_.ether_addr_octet[i]);
            }
            /* SMAC encode */
            smac = (const uint8_t *)
                IntfKSyncObject::GetKSyncObject()->PhysicalIntfMac();
            for (int i = 0 ; i < ETHER_ADDR_LEN; i++) {
                encap.push_back(smac[i]);
            }
            /* Proto encode in Network byte order */
            encap.push_back(0x08);
            encap.push_back(0x00);
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
            encoder.set_nhr_encap_family(ETH_P_ARP);

            if (interface) {
                intf_id = interface->id();
            }
            encoder.set_nhr_encap_oif_id(intf_id);

            /* DMAC encode */
            for (int i = 0 ; i < ETHER_ADDR_LEN; i++) {
                encap.push_back(dmac_.ether_addr_octet[i]);
            }
            /* SMAC encode */
            smac = (const uint8_t *)
                IntfKSyncObject::GetKSyncObject()->PhysicalIntfMac();
            for (int i = 0 ; i < ETHER_ADDR_LEN; i++) {
                encap.push_back(smac[i]);
            }
            /* Proto encode in Network byte order */
            encap.push_back(0x08);
            encap.push_back(0x00);
            encoder.set_nhr_encap(encap);
            flags |= NH_FLAG_TUNNEL_UDP;
            break;

        case NextHop::DISCARD:
            encoder.set_nhr_type(NH_DISCARD);
            break;

        case NextHop::RECEIVE:
            intf_id = interface->id();
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
            encoder.set_nhr_encap_family(ETH_P_ARP);
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
            case Composite::ECMP: {
                flags |= NH_FLAG_COMPOSITE_ECMP;
                break;
            }
            }
            encoder.set_nhr_flags(flags);
            for (KSyncComponentNHList::iterator it = component_nh_list_.begin();
                 it != component_nh_list_.end(); it++) {
                KSyncComponentNH component_nh = *it;
                if (component_nh.GetNH()) {
                    sub_nh_id.push_back(component_nh.GetNH()->GetIndex());
                    sub_label_list.push_back(component_nh.GetLabel());
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

void NHKSyncEntry::FillObjectLog(sandesh_op::type op, KSyncNhInfo &info) {
    info.set_index(GetIndex());
    
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
        for (KSyncComponentNHList::iterator it = component_nh_list_.begin();
             it != component_nh_list_.end(); it++) {
            KSyncComponentNH component_nh = *it;
            if (component_nh.GetNH()) {
                KSyncComponentNHLog sub_nh;
                sub_nh.set_nh_idx(component_nh.GetNH()->GetIndex());
                sub_nh.set_label(component_nh.GetLabel());
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
    if (GetIntf()) {
        info.set_intf_name(GetIntf()->GetName());
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
    IntfKSyncEntry *interface = GetIntf();

    if (valid_ == false) {
        //Invalid nexthop has no reference dependency
        return NULL;
    }

    switch (type_) {
    case NextHop::ARP: {
        assert(interface);
        if (!interface->IsResolved()) {
            entry = interface;
        }
        break;
    }

    case NextHop::VLAN:
    case NextHop::INTERFACE: {
        assert(interface);
        if (!interface->IsResolved()) {
            entry = interface;
        }
        break;
    }

    case NextHop::DISCARD: {
        assert(interface == NULL);
        break;
    }

    case NextHop::TUNNEL: {
        break;
    }

    case NextHop::MIRROR: {
        break;
    }

    case NextHop::RECEIVE: {
        assert(interface);
        if (!interface->IsResolved()) {
            entry = interface;
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
        for (KSyncComponentNHList::const_iterator it = component_nh_list_.begin();
             it != component_nh_list_.end(); it++) {
            KSyncComponentNH component_nh = *it;
            if (component_nh.GetNH() && !(component_nh.GetNH()->IsResolved())) {
                entry = component_nh.GetNH();
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
