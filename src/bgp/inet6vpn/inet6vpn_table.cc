/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet6vpn/inet6vpn_table.h"

#include "base/util.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "bgp/inet6/inet6_route.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/inet6vpn/inet6vpn_route.h"
#include "bgp/routing-instance/routing_instance.h"
#include "db/db_table_partition.h"

Inet6VpnTable::Inet6VpnTable(DB *db, const std::string &name)
    : BgpTable(db, name) {
}

std::auto_ptr<DBEntry>
Inet6VpnTable::AllocEntry(const DBRequestKey *key) const {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(key);
    return std::auto_ptr<DBEntry>(new Inet6VpnRoute(pfxkey->prefix));
}

std::auto_ptr<DBEntry>
Inet6VpnTable::AllocEntryStr(const std::string &key_str) const {
    Inet6VpnPrefix vpn_prefix = Inet6VpnPrefix::FromString(key_str);
    return std::auto_ptr<DBEntry> (new Inet6VpnRoute(vpn_prefix));
}

size_t Inet6VpnTable::Hash(const DBEntry *entry) const {
    const Inet6VpnRoute *vpn_route = static_cast<const Inet6VpnRoute *>(entry);
    const Inet6VpnPrefix &vpn_prefix = vpn_route->GetPrefix();
    Inet6Prefix prefix(vpn_prefix.addr(), vpn_prefix.prefixlen());
    size_t value = Inet6Table::HashFunction(prefix);
    return value % DB::PartitionCount();
}

size_t Inet6VpnTable::Hash(const DBRequestKey *key) const {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    Inet6Prefix prefix(rkey->prefix.addr(), rkey->prefix.prefixlen());
    size_t value = Inet6Table::HashFunction(prefix);
    return value % DB::PartitionCount();
}

BgpRoute *Inet6VpnTable::TableFind(DBTablePartition *rtp,
                                   const DBRequestKey *prefix) {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(prefix);
    Inet6VpnRoute vpn_route(pfxkey->prefix);
    return static_cast<BgpRoute *>(rtp->Find(&vpn_route));
}

DBTableBase *Inet6VpnTable::CreateTable(DB *db, const std::string &name) {
    Inet6VpnTable *table = new Inet6VpnTable(db, name);
    table->Init();
    return table;
}

BgpRoute *Inet6VpnTable::RouteReplicate(BgpServer *server, BgpTable *src_table,
        BgpRoute *source_rt, const BgpPath *src_path,
        ExtCommunityPtr community) {
    assert(src_table->family()  == Address::INET6);

    Inet6Route *src_rt = dynamic_cast<Inet6Route *>(source_rt);
    assert(src_rt);

    const RouteDistinguisher &rd = src_path->GetAttr()->source_rd();
    Inet6VpnPrefix vpn_prefix(rd, src_rt->GetPrefix().ip6_addr(),
                              src_rt->GetPrefix().prefixlen());

    Inet6VpnRoute vpn_route(vpn_prefix);
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(&vpn_route));
    BgpRoute *dest_route = static_cast<BgpRoute *>(partition->Find(&vpn_route));
    if (dest_route == NULL) {
        dest_route = new Inet6VpnRoute(vpn_prefix);
        partition->Add(dest_route);
    } else {
        dest_route->ClearDelete();
    }

    BgpAttrPtr new_attr =
        server->attr_db()->ReplaceExtCommunityAndLocate(src_path->GetAttr(),
                                                        community);

    // Check whether there's already a path with the given peer and path id.
    BgpPath *dest_path =
        dest_route->FindSecondaryPath(source_rt, src_path->GetSource(),
                                      src_path->GetPeer(),
                                      src_path->GetPathId());
    if (dest_path != NULL) {
        if ((new_attr != dest_path->GetAttr()) ||
            (src_path->GetLabel() != dest_path->GetLabel())) {
            // Update Attributes and notify (if needed)
            assert(dest_route->RemoveSecondaryPath(source_rt,
                                src_path->GetSource(), src_path->GetPeer(),
                                src_path->GetPathId()));
        } else {
            return dest_route;
        }
    }

    // Create replicated path and insert it on the route
    BgpSecondaryPath *replicated_path =
        new BgpSecondaryPath(src_path->GetPeer(), src_path->GetPathId(),
                             src_path->GetSource(), new_attr,
                             src_path->GetFlags(), src_path->GetLabel());
    replicated_path->SetReplicateInfo(src_table, source_rt);
    dest_route->InsertPath(replicated_path);

    // Trigger notification only if the inserted path is selected
    if (replicated_path == dest_route->front()) {
        partition->Notify(dest_route);
    }

    return dest_route;
}

bool Inet6VpnTable::Export(RibOut *ribout, Route *route,
        const RibPeerSet &peerset, UpdateInfoSList &uinfo_slist) {
    BgpRoute *bgp_route = static_cast<BgpRoute *> (route);
    UpdateInfo *uinfo = GetUpdateInfo(ribout, bgp_route, peerset);
    if (!uinfo) {
        return false;
    }
    uinfo_slist->push_front(*uinfo);

    return true;
}

static void RegisterFactory() {
    DB::RegisterFactory("bgp.l3vpn-inet6.0", &Inet6VpnTable::CreateTable);
}
MODULE_INITIALIZER(RegisterFactory);
