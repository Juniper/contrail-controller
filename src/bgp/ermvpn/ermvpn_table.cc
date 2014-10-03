/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/ermvpn/ermvpn_table.h"

#include "base/util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_multicast.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "bgp/ermvpn/ermvpn_route.h"
#include "bgp/inet/inet_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routing_instance.h"
#include "db/db_table_partition.h"

using namespace std;

size_t ErmVpnTable::HashFunction(const ErmVpnPrefix &prefix) const {
    return boost::hash_value(prefix.group().to_ulong());
}

ErmVpnTable::ErmVpnTable(DB *db, const string &name)
    : BgpTable(db, name), tree_manager_(NULL) {
}

std::auto_ptr<DBEntry> ErmVpnTable::AllocEntry(
    const DBRequestKey *key) const {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(key);
    return std::auto_ptr<DBEntry> (new ErmVpnRoute(pfxkey->prefix));
}


std::auto_ptr<DBEntry> ErmVpnTable::AllocEntryStr(
    const string &key_str) const {
    ErmVpnPrefix prefix = ErmVpnPrefix::FromString(key_str);
    return std::auto_ptr<DBEntry> (new ErmVpnRoute(prefix));
}

size_t ErmVpnTable::Hash(const DBEntry *entry) const {
    const ErmVpnRoute *rt_entry = static_cast<const ErmVpnRoute *>(entry);
    const ErmVpnPrefix &ermvpnprefix = rt_entry->GetPrefix();
    size_t value = ErmVpnTable::HashFunction(ermvpnprefix);
    return value % DB::PartitionCount();
}

size_t ErmVpnTable::Hash(const DBRequestKey *key) const {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    Ip4Prefix prefix(rkey->prefix.group(), 32);
    size_t value = InetTable::HashFunction(prefix);
    return value % DB::PartitionCount();
}

BgpRoute *ErmVpnTable::TableFind(DBTablePartition *rtp,
    const DBRequestKey *prefix) {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(prefix);
    ErmVpnRoute rt_key(pfxkey->prefix);
    return static_cast<BgpRoute *>(rtp->Find(&rt_key));
}

DBTableBase *ErmVpnTable::CreateTable(DB *db, const std::string &name) {
    ErmVpnTable *table = new ErmVpnTable(db, name);
    table->Init();
    return table;
}

BgpRoute *ErmVpnTable::RouteReplicate(BgpServer *server,
        BgpTable *src_table, BgpRoute *src_rt, const BgpPath *src_path,
        ExtCommunityPtr community) {
    assert(src_table->family() == Address::ERMVPN);

    ErmVpnRoute *mroute = dynamic_cast<ErmVpnRoute *>(src_rt);
    assert(mroute);

    // Native routes are not replicated to other VRFs or to the VPN table.
    if (mroute->GetPrefix().type() == ErmVpnPrefix::NativeRoute)
        return NULL;

    if (!IsDefault()) {

        // Don't replicate to a VRF from other VRF tables.
        ErmVpnTable *src_ermvpn_table = dynamic_cast<ErmVpnTable *>(src_table);
        if (!src_ermvpn_table->IsDefault())
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
    ErmVpnPrefix mprefix(mroute->GetPrefix());
    if (IsDefault()) {
        mprefix.set_route_distinguisher(src_path->GetAttr()->source_rd());
    } else {
        mprefix.set_route_distinguisher(RouteDistinguisher::kZeroRd);
    }
    ErmVpnRoute rt_key(mprefix);

    // Find or create the route.
    DBTablePartition *rtp =
        static_cast<DBTablePartition *>(GetTablePartition(&rt_key));
    BgpRoute *dest_route = static_cast<BgpRoute *>(rtp->Find(&rt_key));
    if (dest_route == NULL) {
        dest_route = new ErmVpnRoute(mprefix);
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
        if (new_attr != dest_path->GetAttr()) {
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

bool ErmVpnTable::Export(RibOut *ribout, Route *route,
    const RibPeerSet &peerset, UpdateInfoSList &uinfo_slist) {
    if (ribout->IsEncodingBgp()) {
        BgpRoute *bgp_route = static_cast<BgpRoute *> (route);
        UpdateInfo *uinfo = GetUpdateInfo(ribout, bgp_route, peerset);
        if (!uinfo)
            return false;
        uinfo_slist->push_front(*uinfo);
        return true;
    }

    ErmVpnRoute *ermvpn_route = dynamic_cast<ErmVpnRoute *>(route);
    if (ermvpn_route->GetPrefix().type() != ErmVpnPrefix::NativeRoute)
        return false;

    if (!tree_manager_ || tree_manager_->deleter()->IsDeleted())
        return false;

    const IPeer *peer = ermvpn_route->BestPath()->GetPeer();
    if (!peer || !ribout->IsRegistered(const_cast<IPeer *>(peer)))
        return false;

    size_t peerbit = ribout->GetPeerIndex(const_cast<IPeer *>(peer));
    if (!peerset.test(peerbit))
        return false;

    UpdateInfo *uinfo = tree_manager_->GetUpdateInfo(ermvpn_route);
    if (!uinfo)
        return false;

    uinfo->target.set(peerbit);
    uinfo_slist->push_front(*uinfo);
    return true;
}

void ErmVpnTable::CreateTreeManager() {
    // Don't create the McastTreeManager for the VPN table.
    if (IsDefault())
        return;
    assert(!tree_manager_);
    tree_manager_ = BgpObjectFactory::Create<McastTreeManager>(this);
    tree_manager_->Initialize();
}

void ErmVpnTable::DestroyTreeManager() {
    assert(tree_manager_);
    tree_manager_->Terminate();
    delete tree_manager_;
    tree_manager_ = NULL;
}

McastTreeManager *ErmVpnTable::GetTreeManager() {
    return tree_manager_;
}

void ErmVpnTable::set_routing_instance(RoutingInstance *rtinstance) {
    BgpTable::set_routing_instance(rtinstance);
    CreateTreeManager();
}

bool ErmVpnTable::IsDefault() const {
    return routing_instance()->IsDefaultRoutingInstance();
}

static void RegisterFactory() {
    DB::RegisterFactory("ermvpn.0", &ErmVpnTable::CreateTable);
}

MODULE_INITIALIZER(RegisterFactory);
