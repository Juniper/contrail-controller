/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/evpn/evpn_table.h"

#include <boost/functional/hash.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_route.h"
#include "bgp/ipeer.h"
#include "bgp/enet/enet_route.h"
#include "bgp/enet/enet_table.h"
#include "bgp/evpn/evpn_route.h"
#include "bgp/routing-instance/routing_instance.h"
#include "db/db_table_partition.h"

using namespace std;

size_t EvpnTable::HashFunction(const EvpnPrefix &prefix) {
    const uint8_t *data = prefix.mac_addr().GetData();
    uint32_t value = get_value(data + 2, 4);
    return boost::hash_value(value);
}

EvpnTable::EvpnTable(DB *db, const std::string &name) : BgpTable(db, name) {
}

std::auto_ptr<DBEntry> EvpnTable::AllocEntry(
        const DBRequestKey *key) const {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(key);
    return std::auto_ptr<DBEntry> (new EvpnRoute(pfxkey->prefix));
}

std::auto_ptr<DBEntry> EvpnTable::AllocEntryStr(
        const string &key_str) const {
    EvpnPrefix prefix = EvpnPrefix::FromString(key_str);
    return std::auto_ptr<DBEntry> (new EvpnRoute(prefix));
}

size_t EvpnTable::Hash(const DBRequestKey *key) const {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    size_t value = HashFunction(rkey->prefix);
    return value % DB::PartitionCount();
}

size_t EvpnTable::Hash(const DBEntry *entry) const {
    const EvpnRoute *rt_entry = static_cast<const EvpnRoute *>(entry);
    size_t value = HashFunction(rt_entry->GetPrefix());
    return value % DB::PartitionCount();
}

BgpRoute *EvpnTable::TableFind(DBTablePartition *rtp,
        const DBRequestKey *prefix) {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(prefix);
    EvpnRoute rt_key(pfxkey->prefix);
    return static_cast<BgpRoute *>(rtp->Find(&rt_key));
}

DBTableBase *EvpnTable::CreateTable(DB *db, const std::string &name) {
    EvpnTable *table = new EvpnTable(db, name);
    table->Init();
    return table;
}

// TBD: Make this a method in BgpTable
static RouteDistinguisher GenerateDistinguisher(
        const BgpTable *src_table, const BgpPath *src_path) {
    RouteDistinguisher source_rd = src_path->GetAttr()->source_rd();
    if (!source_rd.IsNull())
        return source_rd;

    assert(!src_path->GetPeer() || !src_path->GetPeer()->IsXmppPeer());
    const RoutingInstance *src_instance = src_table->routing_instance();
    return *src_instance->GetRD();
}

BgpRoute *EvpnTable::RouteReplicate(BgpServer *server,
        BgpTable *src_table, BgpRoute *src_rt, const BgpPath *src_path,
        ExtCommunityPtr community, OriginVnPtr origin_vn) {
    assert(src_table->family() == Address::ENET);

    EnetRoute *enet = dynamic_cast<EnetRoute *>(src_rt);
    assert(enet);

    const RouteDistinguisher &rd = GenerateDistinguisher(src_table, src_path);

    EvpnPrefix vpn_prefix(
        rd, enet->GetPrefix().mac_addr(), enet->GetPrefix().ip_prefix());
    EvpnRoute rt_key(vpn_prefix);

    DBTablePartition *rtp =
        static_cast<DBTablePartition *>(GetTablePartition(&rt_key));
    BgpRoute *dest_route = static_cast<BgpRoute *>(rtp->Find(&rt_key));
    if (dest_route == NULL) {
        dest_route = new EvpnRoute(vpn_prefix);
        rtp->Add(dest_route);
    } else {
        dest_route->ClearDelete();
    }

    BgpAttrPtr new_attr =
        server->attr_db()->ReplaceExtCommunityAndLocate(src_path->GetAttr(),
                                                        community);

    // Check whether peer already has a path
    BgpPath *dest_path = dest_route->FindSecondaryPath(src_rt, 
                                   src_path->GetPeer(), src_path->GetSource());
    if (dest_path != NULL) {
        if ((new_attr != dest_path->GetAttr()) ||
            (src_path->GetLabel() != dest_path->GetLabel())) {
            // Update Attributes and notify (if needed)
            dest_route->RemoveSecondaryPath(src_rt, src_path->GetPeer(),
                                            src_path->GetSource());
        } else {
            return dest_route;
        }
    }

    // Create replicated path and insert it on the route
    BgpSecondaryPath *replicated_path =
        new BgpSecondaryPath(src_path->GetPeer(), src_path->GetSource(), 
                         new_attr, src_path->GetFlags(), src_path->GetLabel());
    replicated_path->SetReplicateInfo(src_table, src_rt);
    dest_route->InsertPath(replicated_path);

    // Trigger notification only if the inserted path is selected
    if (replicated_path == dest_route->front())
        rtp->Notify(dest_route);

    return dest_route;
}

bool EvpnTable::Export(RibOut *ribout, Route *route,
        const RibPeerSet &peerset, UpdateInfoSList &uinfo_slist) {
    BgpRoute *bgp_route = static_cast<BgpRoute *> (route);
    UpdateInfo *uinfo = GetUpdateInfo(ribout, bgp_route, peerset);
    if (!uinfo)
        return false;
    uinfo_slist->push_front(*uinfo);

    return true;
}

static void RegisterFactory() {
    DB::RegisterFactory("bgp.evpn.0", &EvpnTable::CreateTable);
}

MODULE_INITIALIZER(RegisterFactory);
