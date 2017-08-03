/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/mvpn/mvpn_table.h"

#include "bgp/ipeer.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_multicast.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update.h"
#include "bgp/inet/inet_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routing_instance.h"

using std::auto_ptr;
using std::string;

size_t MvpnTable::HashFunction(const MvpnPrefix &prefix) const {
    if ((prefix.type() == MvpnPrefix::IntraASPMSIADRoute) ||
           (prefix.type() == MvpnPrefix::LeafADRoute)) {
        uint32_t data = prefix.originator().to_ulong();
        return boost::hash_value(data);
    }
    if (prefix.type() == MvpnPrefix::InterASPMSIADRoute) {
        uint32_t data = prefix.asn();
        return boost::hash_value(data);
    }
    return boost::hash_value(prefix.group().to_ulong());
}

MvpnTable::MvpnTable(DB *db, const string &name)
    : BgpTable(db, name) {
}

auto_ptr<DBEntry> MvpnTable::AllocEntry(
    const DBRequestKey *key) const {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(key);
    return auto_ptr<DBEntry> (new MvpnRoute(pfxkey->prefix));
}


auto_ptr<DBEntry> MvpnTable::AllocEntryStr(
    const string &key_str) const {
    MvpnPrefix prefix = MvpnPrefix::FromString(key_str);
    return auto_ptr<DBEntry> (new MvpnRoute(prefix));
}

size_t MvpnTable::Hash(const DBEntry *entry) const {
    const MvpnRoute *rt_entry = static_cast<const MvpnRoute *>(entry);
    const MvpnPrefix &mvpnprefix = rt_entry->GetPrefix();
    size_t value = MvpnTable::HashFunction(mvpnprefix);
    return value % kPartitionCount;
}

size_t MvpnTable::Hash(const DBRequestKey *key) const {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    Ip4Prefix prefix(rkey->prefix.group(), 32);
    size_t value = InetTable::HashFunction(prefix);
    return value % kPartitionCount;
}

BgpRoute *MvpnTable::TableFind(DBTablePartition *rtp,
    const DBRequestKey *prefix) {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(prefix);
    MvpnRoute rt_key(pfxkey->prefix);
    return static_cast<BgpRoute *>(rtp->Find(&rt_key));
}

DBTableBase *MvpnTable::CreateTable(DB *db, const string &name) {
    MvpnTable *table = new MvpnTable(db, name);
    table->Init();
    return table;
}

BgpRoute *MvpnTable::RouteReplicate(BgpServer *server,
        BgpTable *src_table, BgpRoute *src_rt, const BgpPath *src_path,
        ExtCommunityPtr community) {
    assert(src_table->family() == Address::MVPN);

    MvpnRoute *mroute = dynamic_cast<MvpnRoute *>(src_rt);
    assert(mroute);

    if (!IsMaster()) {
        // Don't replicate to a VRF from other VRF tables.
        MvpnTable *src_mvpn_table = dynamic_cast<MvpnTable *>(src_table);
        if (!src_mvpn_table->IsMaster())
            return NULL;

        // Don't replicate to VRF from the VPN table if OriginVn doesn't match.
        OriginVn origin_vn(server->autonomous_system(),
            routing_instance()->virtual_network_index());
        if (!community->ContainsOriginVn(origin_vn.GetExtCommunity()))
            return NULL;
    }

    // RD is always zero in the VRF.  When replicating to the VPN table, we
    // pick up the RD from the SourceRD attribute. The SourceRD is always set
    // for Local and Global routes that the multicast code adds to a VRF.
    MvpnPrefix mprefix(mroute->GetPrefix());
    if (IsMaster()) {
        mprefix.set_route_distinguisher(src_path->GetAttr()->source_rd());
    } else {
        mprefix.set_route_distinguisher(RouteDistinguisher::kZeroRd);
    }
    MvpnRoute rt_key(mprefix);

    // Find or create the route.
    DBTablePartition *rtp =
        static_cast<DBTablePartition *>(GetTablePartition(&rt_key));
    BgpRoute *dest_route = static_cast<BgpRoute *>(rtp->Find(&rt_key));
    if (dest_route == NULL) {
        dest_route = new MvpnRoute(mprefix);
        rtp->Add(dest_route);
    } else {
        dest_route->ClearDelete();
    }

    BgpAttrPtr new_attr =
        server->attr_db()->ReplaceExtCommunityAndLocate(src_path->GetAttr(),
                                                        community);

    // Check whether peer already has a path.
    BgpPath *dest_path = dest_route->FindSecondaryPath(src_rt,
            src_path->GetSource(), src_path->GetPeer(),
            src_path->GetPathId());
    if (dest_path != NULL) {
        if (new_attr != dest_path->GetOriginalAttr() ||
            src_path->GetFlags() != dest_path->GetFlags()) {
            bool success = dest_route->RemoveSecondaryPath(src_rt,
                src_path->GetSource(), src_path->GetPeer(),
                src_path->GetPathId());
            assert(success);
        } else {
            return dest_route;
        }
    }

    // Create replicated path and insert it on the route.
    BgpSecondaryPath *replicated_path =
        new BgpSecondaryPath(src_path->GetPeer(), src_path->GetPathId(),
                             src_path->GetSource(), new_attr,
                             src_path->GetFlags(), src_path->GetLabel());
    replicated_path->SetReplicateInfo(src_table, src_rt);
    dest_route->InsertPath(replicated_path);
    rtp->Notify(dest_route);

    return dest_route;
}

bool MvpnTable::Export(RibOut *ribout, Route *route,
    const RibPeerSet &peerset, UpdateInfoSList &uinfo_slist) {

    // in phase 1 source is outside so no need to send anything
    // to agent
    if (!ribout->IsEncodingBgp())
        return false;

    MvpnRoute *mvpn_route = dynamic_cast<MvpnRoute *>(route);
    uint8_t rt_type = mvpn_route->GetPrefix().type();

    if (ribout->peer_type() == BgpProto::EBGP &&
                rt_type == MvpnPrefix::IntraASPMSIADRoute) {
        return false;
    }
    if (ribout->peer_type() == BgpProto::IBGP &&
                rt_type == MvpnPrefix::InterASPMSIADRoute) {
        return false;
    }
    BgpRoute *bgp_route = static_cast<BgpRoute *> (route);

    UpdateInfo *uinfo = GetUpdateInfo(ribout, bgp_route, peerset);
    if (!uinfo)
        return false;
    uinfo_slist->push_front(*uinfo);
    return true;
}

void MvpnTable::set_routing_instance(RoutingInstance *rtinstance) {
    BgpTable::set_routing_instance(rtinstance);
}

bool MvpnTable::IsMaster() const {
    return routing_instance()->IsMasterRoutingInstance();
}

static void RegisterFactory() {
    DB::RegisterFactory("mvpn.0", &MvpnTable::CreateTable);
}

MODULE_INITIALIZER(RegisterFactory);
