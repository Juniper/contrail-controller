/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet/inet_table.h"

#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update.h"
#include "bgp/extended-community/source_as.h"
#include "bgp/extended-community/vrf_route_import.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/routing-instance/path_resolver.h"
#include "bgp/routing-instance/routing_instance.h"

using std::auto_ptr;
using std::string;

InetTable::InetTable(DB *db, const string &name)
    : BgpTable(db, name) {
    family_ = (name.at(name.length()-1) == '3') ?
        Address::INETMPLS : Address::INET;
}

size_t InetTable::HashFunction(const Ip4Prefix &prefix) {
    return boost::hash_value(prefix.ip4_addr().to_ulong());
}

auto_ptr<DBEntry> InetTable::AllocEntry(const DBRequestKey *key) const {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(key);
    return auto_ptr<DBEntry>(new InetRoute(pfxkey->prefix));
}

auto_ptr<DBEntry> InetTable::AllocEntryStr(const string &key_str) const {
    Ip4Prefix prefix = Ip4Prefix::FromString(key_str);
    return auto_ptr<DBEntry>(new InetRoute(prefix));
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
    RouteDistinguisher rd;

    if (inet) {
        inet_prefix.reset(new Ip4Prefix(inet->GetPrefix().ip4_addr(),
                                      inet->GetPrefix().prefixlen()));
    } else {
        InetVpnRoute *inetvpn = dynamic_cast<InetVpnRoute *> (src_rt);
        assert(inetvpn);
        rd = inetvpn->GetPrefix().route_distinguisher();
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

    BgpAttrDB *attr_db = server->attr_db();
    BgpAttrPtr new_attr = attr_db->ReplaceExtCommunityAndLocate(path->GetAttr(),
                                                                community);

    // Set the RD attr if route is replicated from vpn table
    if (!inet) {
        new_attr = attr_db->ReplaceSourceRdAndLocate(new_attr.get(), rd);
    }

    // Check whether there's already a path with the given peer and path id.
    BgpPath *dest_path = dest_route->FindSecondaryPath(src_rt,
                                          path->GetSource(), path->GetPeer(),
                                          path->GetPathId());
    uint32_t prev_flags = 0 , prev_label = 0;
    const BgpAttr *prev_attr = NULL;
    if (dest_path != NULL) {
        prev_flags = dest_path->GetFlags();
        prev_label = dest_path->GetLabel();
        prev_attr = dest_path->GetOriginalAttr();
        if ((new_attr != prev_attr) ||
            (path->GetFlags() != prev_flags) ||
            (path->GetLabel() != prev_label)) {
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

    // BgpRoute::InsertPath() triggers routing policies processing, which may
    // change the flags (namely, RoutingPolicyReject). So we check the flags
    // once again and only notify the partition if the new path is still
    // different after routing policies were applied.
    bool notify = true;
    if (dest_path != NULL) {
        if ((replicated_path->GetOriginalAttr() == prev_attr) &&
            (replicated_path->GetFlags() == prev_flags) &&
            (replicated_path->GetLabel() == prev_label))
            notify = false;
    }

    // Notify the route even if the best path may not have changed. For XMPP
    // peers, we support sending multiple ECMP next-hops for a single route.
    //
    // TODO(ananth): Can be optimized for changes that does not result in
    // any change to ECMP list.
    if (notify)
        rtp->Notify(dest_route);

    return dest_route;
}

bool InetTable::Export(RibOut *ribout, Route *route, const RibPeerSet &peerset,
        UpdateInfoSList &uinfo_slist) {
    BgpRoute *bgp_route = static_cast<BgpRoute *>(route);

    UpdateInfo *uinfo = GetUpdateInfo(ribout, bgp_route, peerset);
    if (!uinfo)
        return false;

    if (ribout->ExportPolicy().encoding == RibExportPolicy::BGP) {
        BgpAttrDB *attr_db = routing_instance()->server()->attr_db();
        UpdateExtendedCommunity(&uinfo->roattr);

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

// Strip all extended-communities except OriginVN.
void InetTable::UpdateExtendedCommunity(RibOutAttr *roattr) {
    ExtCommunityPtr ext_commp = roattr->attr()->ext_community();
    if (!ext_commp)
        return;

    // Retrieve any origin_vn already present.
    ExtCommunity::ExtCommunityValue const *origin_vnp = NULL;
    BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                  ext_commp->communities()) {
        if (!ExtCommunity::is_origin_vn(comm))
            continue;
        origin_vnp = &comm;
        break;
    }

    BgpAttrDB *attr_db = routing_instance()->server()->attr_db();

    // If there is no origin-vn, then remove all other extended communities.
    if (!origin_vnp) {
        BgpAttrPtr new_attr = attr_db->ReplaceExtCommunityAndLocate(
            roattr->attr(), NULL);
        roattr->set_attr(this, new_attr);
        return;
    }

    // Remove all communities other than OriginVN by replacing all of the
    // extended-community with just OriginVN.
    if (ext_commp->communities().size() > 1) {
        ExtCommunity::ExtCommunityList list;
        list.push_back(*origin_vnp);
        ExtCommunityDB *extcomm_db =
            routing_instance()->server()->extcomm_db();
        ext_commp = extcomm_db->AppendAndLocate(NULL, list);
        BgpAttrPtr new_attr = attr_db->ReplaceExtCommunityAndLocate(
            roattr->attr(), ext_commp);
        roattr->set_attr(this, new_attr);
    }
}

// Attach OriginVN extended-community from inetvpn path attribute if present
// into inet route path attribute.
BgpAttrPtr InetTable::UpdateAttributes(const BgpAttrPtr inetvpn_attrp,
                                       const BgpAttrPtr inet_attrp) {
    BgpServer *server = routing_instance()->server();

    // Check if origin-vn path attribute in inet.0 table path is identical to
    // what is in inetvpn table path.
    ExtCommunity::ExtCommunityValue const *inetvpn_rt_origin_vn = NULL;
    if (inetvpn_attrp && inetvpn_attrp->ext_community()) {
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                    inetvpn_attrp->ext_community()->communities()) {
            if (!ExtCommunity::is_origin_vn(comm))
                continue;
            inetvpn_rt_origin_vn = &comm;
            break;
        }
    }

    ExtCommunity::ExtCommunityValue const *inet_rt_origin_vn = NULL;
    if (inet_attrp && inet_attrp->ext_community()) {
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                    inet_attrp->ext_community()->communities()) {
            if (!ExtCommunity::is_origin_vn(comm))
                continue;
            inet_rt_origin_vn = &comm;
            break;
        }
    }

    // Ignore if there is no change.
    if (inetvpn_rt_origin_vn == inet_rt_origin_vn)
        return inet_attrp;

    // Update/Delete inet route attributes with updated OriginVn community.
    ExtCommunityPtr new_ext_community;
    if (!inetvpn_rt_origin_vn) {
        new_ext_community = server->extcomm_db()->RemoveOriginVnAndLocate(
            inet_attrp->ext_community());
    } else {
        new_ext_community = server->extcomm_db()->ReplaceOriginVnAndLocate(
            inet_attrp->ext_community(), *inetvpn_rt_origin_vn);
    }

    return server->attr_db()->ReplaceExtCommunityAndLocate(inet_attrp.get(),
                                                           new_ext_community);
}

BgpAttrPtr InetTable::GetAttributes(BgpRoute *rt,
                                    BgpAttrPtr inet_attrp, const IPeer *peer) {
    CHECK_CONCURRENCY("db::DBTable");

    BgpAttrPtr attrp = GetFabricAttributes(rt, inet_attrp, peer);
    return GetMvpnAttributes(attrp);
}

BgpAttrPtr InetTable::GetMvpnAttributes(BgpAttrPtr attrp) {
    BgpServer *server = routing_instance()->server();
    if (server->mvpn_ipv4_enable()) {
        VrfRouteImport vit(server->bgp_identifier(),
                           routing_instance()->index());
        ExtCommunityPtr ext =
            server->extcomm_db()->ReplaceVrfRouteImportAndLocate(
                attrp->ext_community(), vit.GetExtCommunity());
        SourceAs sas(server->autonomous_system(), 0);
        ext = server->extcomm_db()->ReplaceSourceASAndLocate(ext.get(),
                    sas.GetExtCommunity());
        BgpAttrPtr new_attr =
            server->attr_db()->ReplaceExtCommunityAndLocate(attrp.get(), ext);
        return new_attr;
    }
    return attrp;
}

// Given an inet prefix, update OriginVN with corresponding inetvpn route's
// path attribute.
BgpAttrPtr InetTable::GetFabricAttributes(BgpRoute *rt,
                                    BgpAttrPtr inet_attrp, const IPeer *peer) {

    if (!routing_instance()->IsMasterRoutingInstance())
        return inet_attrp;
    if (!inet_attrp || inet_attrp->source_rd().IsZero())
        return inet_attrp;

    const InetRoute *inet_rt = dynamic_cast<InetRoute *>(rt);
    const Ip4Prefix inet_prefix = inet_rt->GetPrefix();
    RequestKey inet_rt_key(inet_prefix, NULL);
    DBTablePartition *inet_partition = dynamic_cast<DBTablePartition *>(
        GetTablePartition(&inet_rt_key));

    InetVpnTable *inetvpn_table = dynamic_cast<InetVpnTable *>(
        routing_instance()->GetTable(Address::INETVPN));
    assert(inetvpn_table);
    InetVpnPrefix inetvpn_prefix(inet_attrp->source_rd(),
                                 inet_prefix.ip4_addr(),
                                 inet_prefix.prefixlen());
    InetVpnTable::RequestKey inetvpn_rt_key(inetvpn_prefix, NULL);
    DBTablePartition *inetvpn_partition = dynamic_cast<DBTablePartition *>(
        inetvpn_table->GetTablePartition(&inetvpn_rt_key));

    // Assert that the partition indicies are identical. This is a MUST
    // requirement as we need to peek into tables across different families.
    assert(inet_partition->index() == inetvpn_partition->index());
    InetVpnRoute *inetvpn_route = dynamic_cast<InetVpnRoute *>(
        inetvpn_table->TableFind(inetvpn_partition, &inetvpn_rt_key));
    if (!inetvpn_route)
        return inet_attrp;
    BgpPath *inetvpn_path = inetvpn_route->FindPath(peer, true);
    if (!inetvpn_path)
        return inet_attrp;
    return UpdateAttributes(inetvpn_path->GetAttr(), inet_attrp);
}

// Update inet route path attributes with OriginVN from corresponding inetvpn
// route path attribute.
void InetTable::UpdateRoute(const InetVpnPrefix &inetvpn_prefix,
                            const IPeer *peer, BgpAttrPtr inetvpn_attrp) {
    CHECK_CONCURRENCY("db::DBTable");
    assert(routing_instance()->IsMasterRoutingInstance());

    // Check if a route is present in inet.0 table for this prefix.
    Ip4Prefix inet_prefix(inetvpn_prefix.addr(), inetvpn_prefix.prefixlen());
    InetTable::RequestKey inet_rt_key(inet_prefix, NULL);
    DBTablePartition *inet_partition = dynamic_cast<DBTablePartition *>(
        GetTablePartition(&inet_rt_key));

    InetVpnTable *inetvpn_table = dynamic_cast<InetVpnTable *>(
        routing_instance()->GetTable(Address::INETVPN));
    assert(inetvpn_table);
    InetVpnTable::RequestKey inetvpn_rt_key(inetvpn_prefix, NULL);
    DBTablePartition *inetvpn_partition =
        static_cast<DBTablePartition *>(inetvpn_table->GetTablePartition(
            &inetvpn_rt_key));
    assert(inet_partition->index() == inetvpn_partition->index());

    InetRoute *inet_route = dynamic_cast<InetRoute *>(
        TableFind(inet_partition, &inet_rt_key));
    if (!inet_route)
        return;

    BgpPath *inet_path = inet_route->FindPath(peer);
    if (!inet_path)
        return;
    BgpAttrPtr inet_attrp = inet_path->GetAttr();
    if (!inet_attrp)
        return;

    // Bail if the RDs do not match.
    if (!(inet_attrp->source_rd() == inetvpn_prefix.route_distinguisher())) {
        return;
    }
    BgpAttrPtr new_inet_attrp = UpdateAttributes(inetvpn_attrp, inet_attrp);
    if (new_inet_attrp == inet_attrp)
        return;

    // Update route with OriginVN path attribute.
    inet_path->SetAttr(new_inet_attrp, inet_path->GetOriginalAttr());
    inet_route->Notify();
}

PathResolver *InetTable::CreatePathResolver() {
    if (routing_instance()->IsMasterRoutingInstance())
        return NULL;
    return (new PathResolver(this));
}

static void RegisterFactory() {
    DB::RegisterFactory("inet.0", &InetTable::CreateTable);
    DB::RegisterFactory("inet.3", &InetTable::CreateTable);
}

MODULE_INITIALIZER(RegisterFactory);
