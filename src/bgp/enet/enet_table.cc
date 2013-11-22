/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/enet/enet_table.h"

#include <boost/functional/hash.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_route.h"
#include "bgp/ipeer.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/enet/enet_route.h"
#include "bgp/evpn/evpn_route.h"
#include "bgp/routing-instance/routing_instance.h"
#include "db/db_table_partition.h"

using namespace std;

size_t EnetTable::HashFunction(const EnetPrefix &prefix) {
    const uint8_t *data = prefix.mac_addr().GetData();
    uint32_t value = get_value(data + 2, 4);
    return boost::hash_value(value);
}

EnetTable::EnetTable(DB *db, const std::string &name) : BgpTable(db, name) {
}

std::auto_ptr<DBEntry> EnetTable::AllocEntry(
        const DBRequestKey *key) const {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(key);
    return std::auto_ptr<DBEntry> (new EnetRoute(pfxkey->prefix));
}

std::auto_ptr<DBEntry> EnetTable::AllocEntryStr(
        const string &key_str) const {
    EnetPrefix prefix = EnetPrefix::FromString(key_str);
    return std::auto_ptr<DBEntry> (new EnetRoute(prefix));
}

size_t EnetTable::Hash(const DBRequestKey *key) const {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    size_t value = HashFunction(rkey->prefix);
    return value % DB::PartitionCount();
}

size_t EnetTable::Hash(const DBEntry *entry) const {
    const EnetRoute *rt_entry = static_cast<const EnetRoute *>(entry);
    size_t value = HashFunction(rt_entry->GetPrefix());
    return value % DB::PartitionCount();
}

BgpRoute *EnetTable::TableFind(DBTablePartition *rtp,
        const DBRequestKey *prefix) {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(prefix);
    EnetRoute rt_key(pfxkey->prefix);
    return static_cast<BgpRoute *>(rtp->Find(&rt_key));
}

DBTableBase *EnetTable::CreateTable(DB *db, const std::string &name) {
    EnetTable *table = new EnetTable(db, name);
    table->Init();
    return table;
}

BgpRoute *EnetTable::RouteReplicate(BgpServer *server,
        BgpTable *src_table, BgpRoute *src_rt, const BgpPath *src_path,
        ExtCommunityPtr community) {
    if (src_table->family() != Address::EVPN)
        return NULL;

    EnetRoute *enet= dynamic_cast<EnetRoute *>(src_rt);
    boost::scoped_ptr<EnetPrefix> enet_prefix;

    if (enet) {
        enet_prefix.reset(new EnetPrefix(enet->GetPrefix().mac_addr(),
            enet->GetPrefix().ip_prefix()));
    } else {
        EvpnRoute *evpn = dynamic_cast<EvpnRoute *>(src_rt);
        assert(evpn);
        enet_prefix.reset(new EnetPrefix(evpn->GetPrefix().mac_addr(),
            evpn->GetPrefix().ip_prefix()));
    }

    EnetRoute rt_key(*enet_prefix);
    DBTablePartition *rtp =
        static_cast<DBTablePartition *>(GetTablePartition(&rt_key));
    BgpRoute *dest_route = static_cast<BgpRoute *>(rtp->Find(&rt_key));
    if (dest_route == NULL) {
        dest_route = new EnetRoute(rt_key.GetPrefix());
        rtp->Add(dest_route);
    } else {
        dest_route->ClearDelete();
    }

    // Replace the extended community with the one provided.
    BgpAttrPtr new_attr = server->attr_db()->ReplaceExtCommunityAndLocate(
        src_path->GetAttr(), community);

    // Check whether peer already has a path.
    BgpPath *dest_path =
        dest_route->FindSecondaryPath(src_rt, src_path->GetSource(),
                                      src_path->GetPeer(), 
                                      src_path->GetPathId());
    if (dest_path != NULL) {
        if ((new_attr != dest_path->GetAttr()) ||
            (src_path->GetLabel() != dest_path->GetLabel())) {
            assert(dest_route->RemoveSecondaryPath(src_rt,
                       src_path->GetSource(), src_path->GetPeer(),
                       src_path->GetPathId()));
        } else {
            return dest_route;
        }
    }

    BgpSecondaryPath *replicated_path =
        new BgpSecondaryPath(src_path->GetPeer(), src_path->GetPathId(),
                             src_path->GetSource(), new_attr,
                             src_path->GetFlags(), src_path->GetLabel());
    replicated_path->SetReplicateInfo(src_table, src_rt);
    dest_route->InsertPath(replicated_path);

    // Notify the route even if the best path may not have changed. For XMPP
    // peers, we support sending multiple ECMP next-hops for a single route.
    rtp->Notify(dest_route);

    return dest_route;
}

bool EnetTable::Export(RibOut *ribout, Route *route,
        const RibPeerSet &peerset, UpdateInfoSList &uinfo_slist) {
    BgpRoute *bgp_route = static_cast<BgpRoute *> (route);
    UpdateInfo *uinfo = GetUpdateInfo(ribout, bgp_route, peerset);
    if (!uinfo)
        return false;
    uinfo_slist->push_front(*uinfo);

    return true;
}

static void RegisterFactory() {
    DB::RegisterFactory("enet.0", &EnetTable::CreateTable);
}

MODULE_INITIALIZER(RegisterFactory);
