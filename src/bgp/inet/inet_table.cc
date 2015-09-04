/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet/inet_table.h"

#include <boost/functional/hash.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/util.h"
#include "bgp/bgp_path.h"
#include "bgp/inet/inet_route.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/routing-instance/routing_instance.h"
#include "db/db_table_partition.h"

using std::auto_ptr;
using std::string;

size_t InetTable::HashFunction(const Ip4Prefix &prefix) {
    return boost::hash_value(prefix.ip4_addr().to_ulong());
}

InetTable::InetTable(DB *db, const string &name) : BgpTable(db, name) {
}

auto_ptr<DBEntry> InetTable::AllocEntry(const DBRequestKey *key) const {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(key);
    return auto_ptr<DBEntry> (new InetRoute(pfxkey->prefix));
}

auto_ptr<DBEntry> InetTable::AllocEntryStr(const string &key_str) const {
    Ip4Prefix prefix = Ip4Prefix::FromString(key_str);
    return auto_ptr<DBEntry> (new InetRoute(prefix));
}

size_t InetTable::Hash(const DBEntry *entry) const {
    const InetRoute *rt_entry = static_cast<const InetRoute *>(entry);
    size_t value = HashFunction(rt_entry->GetPrefix());
    return value % DB::PartitionCount();
}

size_t InetTable::Hash(const DBRequestKey *key) const {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    size_t value = HashFunction(rkey->prefix);
    return value % DB::PartitionCount();
}

BgpRoute *InetTable::TableFind(DBTablePartition *rtp,
        const DBRequestKey *prefix) {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(prefix);
    InetRoute rt_key(pfxkey->prefix);
    return static_cast<BgpRoute *>(rtp->Find(&rt_key));
}

DBTableBase *InetTable::CreateTable(DB *db, const string &name) {
    InetTable *table = new InetTable(db, name);
    table->Init();
    return table;
}

BgpRoute *InetTable::RouteReplicate(BgpServer *server,
        BgpTable *src_table, BgpRoute *src_rt, const BgpPath *path,
        ExtCommunityPtr community) {
    InetRoute *inet= dynamic_cast<InetRoute *> (src_rt);

    boost::scoped_ptr<Ip4Prefix> inet_prefix;

    if (inet) {
        inet_prefix.reset(new Ip4Prefix(inet->GetPrefix().ip4_addr(),
                                      inet->GetPrefix().prefixlen()));
    } else {
        InetVpnRoute *inetvpn = dynamic_cast<InetVpnRoute *> (src_rt);
        assert(inetvpn);
        inet_prefix.reset(new Ip4Prefix(inetvpn->GetPrefix().addr(),
                                  inetvpn->GetPrefix().prefixlen()));
    }

    InetRoute rt_key(*inet_prefix);
    DBTablePartition *rtp =
        static_cast<DBTablePartition *>(GetTablePartition(&rt_key));
    BgpRoute *dest_route = static_cast<BgpRoute *>(rtp->Find(&rt_key));
    if (dest_route == NULL) {
        dest_route = new InetRoute(rt_key.GetPrefix());
        rtp->Add(dest_route);
    } else {
        dest_route->ClearDelete();
    }

    // Replace the extended community with the one provided.
    BgpAttrPtr new_attr = server->attr_db()->ReplaceExtCommunityAndLocate(
        path->GetAttr(), community);

    // Check whether there's already a path with the given peer and path id.
    BgpPath *dest_path = dest_route->FindSecondaryPath(src_rt,
                                          path->GetSource(), path->GetPeer(),
                                          path->GetPathId());
    if (dest_path != NULL) {
        if ((new_attr != dest_path->GetAttr()) ||
            (path->GetLabel() != dest_path->GetLabel())) {
            // Update Attributes and notify (if needed)
            assert(dest_route->RemoveSecondaryPath(src_rt, path->GetSource(),
                        path->GetPeer(), path->GetPathId()));
        } else {
            return dest_route;
        }
    }

    BgpSecondaryPath *replicated_path =
        new BgpSecondaryPath(path->GetPeer(), path->GetPathId(),
            path->GetSource(), new_attr, path->GetFlags(), path->GetLabel());
    replicated_path->SetReplicateInfo(src_table, src_rt);
    dest_route->InsertPath(replicated_path);

    // Notify the route even if the best path may not have changed. For XMPP
    // peers, we support sending multiple ECMP next-hops for a single route.
    //
    // TODO(ananth): Can be optimized for changes that does not result in
    // any change to ECMP list.
    rtp->Notify(dest_route);

    return dest_route;
}

bool InetTable::Export(RibOut *ribout, Route *route, const RibPeerSet &peerset,
        UpdateInfoSList &uinfo_slist) {
    BgpRoute *bgp_route = static_cast<BgpRoute *>(route);

    UpdateInfo *uinfo = GetUpdateInfo(ribout, bgp_route, peerset);
    if (!uinfo)
        return false;

    // Strip BGP extended communities out by default.
    if (ribout->ExportPolicy().encoding == RibExportPolicy::BGP) {
        if (uinfo->roattr.attr()->ext_community() != NULL) {
            BgpAttrDB *attr_db = routing_instance()->server()->attr_db();
            BgpAttrPtr new_attr =
                attr_db->ReplaceExtCommunityAndLocate(
                    uinfo->roattr.attr(), NULL);
            uinfo->roattr.set_attr(new_attr);
        }
    }
    uinfo_slist->push_front(*uinfo);

    return true;
}

static void RegisterFactory() {
    DB::RegisterFactory("inet.0", &InetTable::CreateTable);
}

MODULE_INITIALIZER(RegisterFactory);
