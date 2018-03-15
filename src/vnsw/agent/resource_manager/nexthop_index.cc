/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent.h"
#include "cmn/agent_cmn.h"
#include "resource_manager/resource_manager.h"
#include "resource_manager/resource_table.h"
#include "resource_manager/resource_cmn.h"
#include "resource_manager/nexthop_index.h"
#include "resource_manager/resource_manager_types.h"
#include "resource_manager/resource_backup.h"
#include "base/time_util.h"
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>


// NextHop resource key
NHIndexResourceKey::NHIndexResourceKey(ResourceManager *rm, uint16_t nh_type,
                                       NextHopKey *nh_key):
     IndexResourceKey(rm, Resource::NEXTHOP_INDEX), nh_type_(nh_type) {
    nh_key_.reset(nh_key);
}

NHIndexResourceKey::NHIndexResourceKey(ResourceManager *rm,
                                       const uint16_t nh_type,
                                       const uint16_t comp_type,
                                       const std::vector<cnhid_label_map>
                                       &nh_ids_labels,
                                       const bool policy,
                                       const std::string &vrf_name):
    IndexResourceKey(rm, Resource::NEXTHOP_INDEX), nh_type_(nh_type),
    comp_type_(comp_type), nh_ids_labels_(nh_ids_labels), policy_(policy),
    vrf_name_(vrf_name) {
    nh_key_.reset();
}

NHIndexResourceKey::~NHIndexResourceKey() {
    nh_key_.reset();
}

bool NHIndexResourceKey::IsLess(const ResourceKey &rhs) const {
    const NHIndexResourceKey *nh_rkey = static_cast<const
        NHIndexResourceKey *>(&rhs);

    if (nh_rkey->nh_type() != nh_type())
        return nh_rkey->nh_type() < nh_type();

    if (nh_rkey->nh_type() == NextHop::COMPOSITE &&
        (nh_type() == NextHop::COMPOSITE)) {
        if (nh_rkey->comp_type() != comp_type())
            return nh_rkey->comp_type() < comp_type();
        if (nh_rkey->policy() != policy()) 
            return nh_rkey->policy() < policy();
    }

    if (nh_rkey->vrf_name() != vrf_name())
        return nh_rkey->vrf_name() < vrf_name();

    if (nh_rkey->nh_ids_labels_.size() != nh_ids_labels_.size())
        return nh_rkey->nh_ids_labels_.size() < nh_ids_labels_.size();

    std::vector<cnhid_label_map>::const_iterator left_it =
        nh_rkey->nh_ids_labels_.begin(); 
    std::vector<cnhid_label_map>::const_iterator right_it =
        nh_ids_labels_.begin();
    for (;left_it != nh_rkey->nh_ids_labels_.end() && right_it != nh_ids_labels_.end();
         left_it++, right_it++) {
        if ((*left_it).nh_id != (*right_it).nh_id )
            return (*left_it).nh_id < (*right_it).nh_id;
        if ((*left_it).label != (*right_it).label)
            return (*left_it).label < (*right_it).label;
    }
    const NextHopKey *nh_key1 = GetNhKey();
    const NextHopKey *nh_key2 = nh_rkey->GetNhKey();
    if (nh_key1 && nh_key2)
        return nh_key1->IsLess(*nh_key2);
    return false;
}

//Backup the Nexthop Resource Data
//Based the Key type fill the Sandesh structure.
//Sandesh is flat structure for all the Key types
void NHIndexResourceKey::Backup(ResourceData *data, uint16_t op) {
    const NextHopKey *nh_key = GetNhKey();
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    NextHopResource backup_data;
    // For Composite Next hop we are not setting the key
    if (!nh_key) {
        BackupCompositeNH(data, op);
        return;
    }

    if (op == ResourceBackupReq::DEL) {
        rm()->backup_mgr()->
            sandesh_maps().DeleteNextHopResourceEntry(index_data->index());
        goto end;
    } 
    backup_data.set_time_stamp(UTCTimestampUsec());
    switch(nh_key->GetType()) {
    case NextHop::INTERFACE: {
        const InterfaceNHKey *itfnh_key = static_cast<const InterfaceNHKey *>
            (GetNhKey());
        backup_data.set_intf_type(itfnh_key->intf_type());
        backup_data.set_uuid(UuidToString(itfnh_key->GetUuid()));
        backup_data.set_name(itfnh_key->name());
        backup_data.set_intf_policy(itfnh_key->GetPolicy());
        backup_data.set_flags(itfnh_key->flags());
        backup_data.set_mac(itfnh_key->dmac().ToString());
        break;
    }
    case NextHop::VLAN: {
        const VlanNHKey *vlan_nh_key = static_cast<const VlanNHKey *>
            (GetNhKey());
        backup_data.set_uuid(UuidToString(vlan_nh_key->GetUuid()));
        backup_data.set_tag(vlan_nh_key->vlan_tag());
        break;
    }
    case NextHop::VRF: {
        const VrfNHKey *vrfnh_key = static_cast<const VrfNHKey *>(GetNhKey());
        backup_data.set_vrf_name(vrfnh_key->GetVrfName());
        backup_data.set_intf_policy(vrfnh_key->GetPolicy());
        backup_data.set_vxlan_nh(vrfnh_key->GetVxlanNh());
        break;
    }
    case NextHop::RECEIVE: {
        const ReceiveNHKey *receive_nh_key =
            static_cast<const ReceiveNHKey *>(GetNhKey());
        backup_data.set_policy(receive_nh_key->GetPolicy());
        const InterfaceKey *itf_key = receive_nh_key->intf_key();
        backup_data.set_intf_type(itf_key->type_);
        backup_data.set_uuid(UuidToString(itf_key->uuid_));
        backup_data.set_name(itf_key->name_);
        break;
    }
    case NextHop::RESOLVE: {
        const ResolveNHKey *resolve_nh_key =
            static_cast<const ResolveNHKey *>(GetNhKey());
        backup_data.set_policy(resolve_nh_key->GetPolicy());
        const InterfaceKey *itf_key = resolve_nh_key->intf_key();
        backup_data.set_intf_type(itf_key->type_);
        backup_data.set_uuid(UuidToString(itf_key->uuid_));
        backup_data.set_name(itf_key->name_);
        break;
    }
    case NextHop::ARP: {
        const ArpNHKey *arp_nh_key =
            static_cast<const ArpNHKey *>(GetNhKey());
        backup_data.set_policy(arp_nh_key->GetPolicy());
        backup_data.set_vrf_name(arp_nh_key->vrf_name());
        backup_data.set_dip(arp_nh_key->dip().to_ulong());
        break;
    }
    case NextHop::TUNNEL: {
        const TunnelNHKey *tunnel_nh_key =
            static_cast<const TunnelNHKey *>(GetNhKey());
        backup_data.set_policy(tunnel_nh_key->GetPolicy());
        backup_data.set_dip(tunnel_nh_key->dip().to_ulong());
        backup_data.set_sip(tunnel_nh_key->sip().to_ulong());
        backup_data.set_vrf_name(tunnel_nh_key->vrf_name());
        uint16_t tunnel_type = tunnel_nh_key->tunnel_type();
        backup_data.set_tunnel_type(tunnel_type);
        break;
    }
    case NextHop::PBB: {
        const PBBNHKey *pbb_nh_key =
            static_cast<const PBBNHKey *>(GetNhKey());
        backup_data.set_vrf_name(pbb_nh_key->vrf_name());
        backup_data.set_isid(pbb_nh_key->isid());
        backup_data.set_mac(pbb_nh_key->dest_bmac().ToString());
        break;
    }
    case NextHop::MIRROR: {
        const MirrorNHKey *mirror_nh_key = 
            static_cast<const MirrorNHKey *>(GetNhKey());
        backup_data.set_dip(mirror_nh_key->dip().to_v4().to_ulong());
        backup_data.set_sip(mirror_nh_key->sip().to_v4().to_ulong());
        backup_data.set_dip(mirror_nh_key->dport());
        backup_data.set_sip(mirror_nh_key->sport());
        backup_data.set_vrf_name(mirror_nh_key->vrf_name());
        break;
    }
    default:
        break;
    }
    rm()->backup_mgr()->sandesh_maps().AddNextHopResourceEntry
        (index_data->index(), backup_data);
    end:
    rm()->backup_mgr()->sandesh_maps().nexthop_index_table().TriggerBackup();
}

void NHIndexResourceKey::BackupCompositeNH(ResourceData *data, uint16_t op) {
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    CompositeNHIndexResource backup_data;
    if (op == ResourceBackupReq::DEL) {
        rm()->backup_mgr()->
            sandesh_maps().DeleteCompositeNHResourceEntry(index_data->index());
    } else {
        backup_data.set_type(comp_type_);
        backup_data.set_vrf_name(vrf_name_);
        backup_data.set_policy(policy_);
        std::vector<CompositeNHIdToLableMap> nhid_label_map;
        for(uint32_t i =0; i < nh_ids_labels_.size(); i++){
            CompositeNHIdToLableMap nhid_label;
            nhid_label.nh_id = nh_ids_labels_[i].nh_id;
            nhid_label.label = nh_ids_labels_[i].label;
            nhid_label_map.push_back(nhid_label);
        }
        // set list of nh_id's 
        backup_data.set_nhid_label_map(nhid_label_map);
        rm()->backup_mgr()->
            sandesh_maps().AddCompositeNHResourceEntry(index_data->index(),
                                                       backup_data);
    }
    rm()->backup_mgr()->sandesh_maps().compositenh_index_table().TriggerBackup();
}
