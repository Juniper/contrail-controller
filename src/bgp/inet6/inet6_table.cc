/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet6/inet6_table.h"

#include "bgp/bgp_server.h"
#include "bgp/bgp_update.h"
#include "bgp/inet6vpn/inet6vpn_route.h"
#include "bgp/routing-instance/path_resolver.h"
#include "bgp/routing-instance/routing_instance.h"

Inet6Table::Inet6Table(DB *db, const std::string &name)
    : BgpTable(db, name) {
}

size_t Inet6Table::HashFunction(const Inet6Prefix &prefix) {
    const Ip6Address::bytes_type &addr_bytes = prefix.ToBytes();
    return boost::hash_range(addr_bytes.begin(), addr_bytes.end());
}

std::auto_ptr<DBEntry> Inet6Table::AllocEntry(const DBRequestKey *key) const {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    return std::auto_ptr<DBEntry> (new Inet6Route(rkey->prefix));
}

std::auto_ptr<DBEntry> Inet6Table::AllocEntryStr(
        const std::string &key_str) const {
    Inet6Prefix prefix = Inet6Prefix::FromString(key_str);
    return std::auto_ptr<DBEntry> (new Inet6Route(prefix));
}

size_t Inet6Table::Hash(const DBEntry *entry) const {
    const Inet6Route *route = static_cast<const Inet6Route *>(entry);
    size_t value = HashFunction(route->GetPrefix());
    return value % DB::PartitionCount();
}

size_t Inet6Table::Hash(const DBRequestKey *key) const {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    size_t value = HashFunction(rkey->prefix);
    return value % DB::PartitionCount();
}

BgpRoute *Inet6Table::TableFind(DBTablePartition *partition,
                                const DBRequestKey *key) {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    Inet6Route route(rkey->prefix);
    return static_cast<BgpRoute *>(partition->Find(&route));
}

DBTableBase *Inet6Table::CreateTable(DB *db, const std::string &name) {
    Inet6Table *table = new Inet6Table(db, name);
    table->Init();
    return table;
}

BgpRoute *Inet6Table::RouteReplicate(BgpServer *server, BgpTable *src_table,
        BgpRoute *src_rt, const BgpPath *path, ExtCommunityPtr community) {
    assert((src_table->family()  == Address::INET6) ||
           (src_table->family()  == Address::INET6VPN));

    Inet6Route *source = dynamic_cast<Inet6Route *>(src_rt);

    RouteDistinguisher rd;

    boost::scoped_ptr<Inet6Prefix> prefix;
    if (source) {
        prefix.reset(new Inet6Prefix(source->GetPrefix().ip6_addr(),
                                     source->GetPrefix().prefixlen()));
    } else {
        Inet6VpnRoute *vpn_route = dynamic_cast<Inet6VpnRoute *> (src_rt);
        assert(vpn_route);
        rd = vpn_route->GetPrefix().route_distinguisher();
        prefix.reset(new Inet6Prefix(vpn_route->GetPrefix().addr(),
                                     vpn_route->GetPrefix().prefixlen()));
    }

    Inet6Route route(*prefix);
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(&route));
    BgpRoute *dest_route = static_cast<BgpRoute *>(partition->Find(&route));
    if (dest_route == NULL) {
        dest_route = new Inet6Route(route.GetPrefix());
        partition->Add(dest_route);
    } else {
        dest_route->ClearDelete();
    }

    // Replace the extended community with the one provided.
    BgpAttrDB *attr_db = server->attr_db();
    BgpAttrPtr new_attr = attr_db->ReplaceExtCommunityAndLocate(path->GetAttr(),
                                                                community);

    if (!source) {
        new_attr = attr_db->ReplaceSourceRdAndLocate(new_attr.get(), rd);
    }

    // Check whether there's already a path with the given peer and path id.
    BgpPath *dest_path =
        dest_route->FindSecondaryPath(src_rt, path->GetSource(),
                                      path->GetPeer(), path->GetPathId());
    if (dest_path != NULL) {
        if ((new_attr != dest_path->GetOriginalAttr()) ||
            (path->GetFlags() != dest_path->GetFlags()) ||
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
    // any change to ECMP list
    partition->Notify(dest_route);

    return dest_route;
}

bool Inet6Table::Export(RibOut *ribout, Route *route, const RibPeerSet &peerset,
        UpdateInfoSList &uinfo_slist) {
    BgpRoute *bgp_route = static_cast<BgpRoute *> (route);
    UpdateInfo *uinfo = GetUpdateInfo(ribout, bgp_route, peerset);
    if (!uinfo) {
        return false;
    }

    if (ribout->ExportPolicy().encoding == RibExportPolicy::BGP) {
        BgpAttrDB *attr_db = routing_instance()->server()->attr_db();
        // Strip ExtCommunity.
        if (uinfo->roattr.attr()->ext_community()) {
            BgpAttrPtr new_attr = attr_db->ReplaceExtCommunityAndLocate(
                uinfo->roattr.attr(), NULL);
            uinfo->roattr.set_attr(this, new_attr);
        }

        // Strip OriginVnPath.
        if (uinfo->roattr.attr()->origin_vn_path()) {
            BgpAttrPtr new_attr = attr_db->ReplaceOriginVnPathAndLocate(
                uinfo->roattr.attr(), NULL);
            uinfo->roattr.set_attr(this, new_attr);
        }
    }
    uinfo_slist->push_front(*uinfo);

    return true;
}

PathResolver *Inet6Table::CreatePathResolver() {
    if (routing_instance()->IsMasterRoutingInstance())
        return NULL;
    return (new PathResolver(this));
}

static void RegisterFactory() {
    DB::RegisterFactory("inet6.0", &Inet6Table::CreateTable);
}
MODULE_INITIALIZER(RegisterFactory);
