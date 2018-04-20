/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/mvpn/mvpn_table.h"

#include <utility>
#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/extended-community/source_as.h"
#include "bgp/ipeer.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_multicast.h"
#include "bgp/bgp_mvpn.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update.h"
#include "bgp/inet/inet_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/path_resolver.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/routing_instance_analytics_types.h"
#include "bgp/routing-instance/routing_instance_log.h"

using std::auto_ptr;
using std::pair;
using std::string;
using std::set;

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
    : BgpTable(db, name), manager_(NULL) {
}

PathResolver *MvpnTable::CreatePathResolver() {
    if (routing_instance()->IsMasterRoutingInstance())
        return NULL;
    PathResolver * path_resolver = new PathResolver(this);
    path_resolver->set_nexthop_longest_match(true);
    return path_resolver;
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

void MvpnTable::CreateManager() {
    if (manager_)
        return;

    // Don't create the MvpnManager if ProjectManager is not present.
    MvpnProjectManager *pm = GetProjectManager();
    if (!pm)
        return;
    manager_ = BgpObjectFactory::Create<MvpnManager>(this, pm->table());
    manager_->Initialize();

    // Notify all routes in the table for further evaluation.
    NotifyAllEntries();

    // TODO(Ananth): Should we also notify routes in the bgp.mvpn.0 table ?
}

void MvpnTable::DestroyManager() {
    if (!manager_)
        return;
    if (IsDeleted())
        DeleteMvpnManager();
    manager_->Terminate();
    delete manager_;
    manager_ = NULL;
}

void MvpnTable::CreateMvpnManagers() {
    if (!server()->mvpn_ipv4_enable())
        return;
    RoutingInstance *rtinstance = routing_instance();
    tbb::mutex::scoped_lock lock(rtinstance->manager()->mutex());

    // Don't create the MvpnManager for the VPN table.
    if (!rtinstance->IsMasterRoutingInstance() &&
            !rtinstance->mvpn_project_manager_network().empty()) {
        pair<MvpnProjectManagerNetworks::iterator, bool> ret =
            rtinstance->manager()->mvpn_project_managers().insert(make_pair(
                rtinstance->mvpn_project_manager_network(), set<string>()));
        ret.first->second.insert(rtinstance->name());

        // Initialize MVPN Manager.
        CreateManager();
    }

    MvpnProjectManagerNetworks::iterator iter =
        rtinstance->manager()->mvpn_project_managers().find(
            rtinstance->name());
    if (iter == rtinstance->manager()->mvpn_project_managers().end())
        return;

    BOOST_FOREACH(const string &mvpn_network, iter->second) {
        RoutingInstance *rti =
            rtinstance->manager()->GetRoutingInstance(mvpn_network);
        if (!rti || rti->deleted())
            continue;
        MvpnTable *table =
            dynamic_cast<MvpnTable *>(rti->GetTable(Address::MVPN));
        if (!table || table->IsDeleted())
            continue;
        table->CreateManager();
    }
}

void MvpnTable::DeleteMvpnManager() {
    if (!server()->mvpn_ipv4_enable())
        return;
    if (routing_instance()->mvpn_project_manager_network().empty())
        return;
    tbb::mutex::scoped_lock lock(routing_instance()->manager()->mutex());
    MvpnProjectManagerNetworks::iterator iter =
        routing_instance()->manager()->mvpn_project_managers().find(
            routing_instance()->mvpn_project_manager_network());
    if (iter != routing_instance()->manager()->mvpn_project_managers().end()) {
        iter->second.erase(routing_instance()->name());
        if (iter->second.empty())
            routing_instance()->manager()->mvpn_project_managers().erase(iter);
    }
}

// Call the const version to avoid code duplication.
MvpnProjectManager *MvpnTable::GetProjectManager() {
    return const_cast<MvpnProjectManager *>(
        static_cast<const MvpnTable *>(this)->GetProjectManager());
}

// Get MvpnProjectManager object for this Mvpn. Each MVPN network is associated
// with a parent project maanger network via configuration. MvpnProjectManager
// is retrieved from this parent network RoutingInstance's ErmVpnTable.
const MvpnProjectManager *MvpnTable::GetProjectManager() const {
    std::string pm_network = routing_instance()->mvpn_project_manager_network();
    if (pm_network.empty())
        return NULL;
    const RoutingInstance *rtinstance =
        routing_instance()->manager()->GetRoutingInstance(pm_network);
    if (!rtinstance)
        return NULL;
    const ErmVpnTable *table = dynamic_cast<const ErmVpnTable *>(
        rtinstance->GetTable(Address::ERMVPN));
    if (!table)
        return NULL;
    return table->mvpn_project_manager();
}

bool MvpnTable::IsProjectManagerUsable() const {
    std::string pm_network = routing_instance()->mvpn_project_manager_network();
    if (pm_network.empty())
        return false;
    const RoutingInstance *rtinstance =
        routing_instance()->manager()->GetRoutingInstance(pm_network);
    if (!rtinstance || rtinstance->deleted())
        return false;
    const ErmVpnTable *table = dynamic_cast<const ErmVpnTable *>(
        rtinstance->GetTable(Address::ERMVPN));
    if (!table || table->IsDeleted())
        return false;

    if (!table->mvpn_project_manager() ||
            table->mvpn_project_manager()->deleter()->IsDeleted()) {
        return false;
    }
    return true;
}

// Return the MvpnProjectManagerPartition for this route using the same DB
// partition index as of the route.
const MvpnProjectManagerPartition *MvpnTable::GetProjectManagerPartition(
        BgpRoute *route) const {
    const MvpnProjectManager *manager = GetProjectManager();
    if (!manager)
        return NULL;
    int part_id = route->get_table_partition()->index();
    return manager->GetPartition(part_id);
}

// Override virtual method to retrive target table for MVPN routes. For now,
// only Type-4 LeafAD routes require special treatment, as they always come
// with the same route target <router-id>:0. Hence, if normal rtf selection
// mode is used, every table with MVPN enalbled would have to be notified for
// replication. Instead, find the table based on the correspondong S-PMSI route.
// This route can be retrieved from the MVPN state of the <S-G> maintained in
// the MvpnProjectManagerPartition object.
void MvpnTable::UpdateSecondaryTablesForReplication(BgpRoute *rt,
        TableSet *secondary_tables) {
    MvpnRoute *mvpn_rt = dynamic_cast<MvpnRoute *>(rt);
    assert(mvpn_rt);

    // Special table lookup is required only for the Type4 LeafAD routes.
    if (mvpn_rt->GetPrefix().type() != MvpnPrefix::LeafADRoute)
        return;

    // Find Type-3 S-PMSI route from the Type-4 prefix route.
    MvpnPrefix spmsi_prefix;
    spmsi_prefix.SetSPMSIPrefixFromLeafADPrefix(mvpn_rt->GetPrefix());
    const MvpnRoute *spmsi_rt = FindRoute(spmsi_prefix);
    if (!spmsi_rt || !spmsi_rt->IsUsable())
        return;
    if (!spmsi_rt->BestPath()->IsReplicated())
        return;

    const BgpTable *table = dynamic_cast<const BgpSecondaryPath *>(
                                spmsi_rt->BestPath())->src_table();
    const MvpnTable *mvpn_table = dynamic_cast<const MvpnTable *>(table);
    if (!mvpn_table || mvpn_table->IsMaster() || !mvpn_table->manager())
        return;
    mvpn_table->manager()->UpdateSecondaryTablesForReplication(
                              mvpn_rt, secondary_tables);
}

// Find or create the route.
MvpnRoute *MvpnTable::FindRoute(const MvpnPrefix &prefix) {
    MvpnRoute rt_key(prefix);
    DBTablePartition *rtp = static_cast<DBTablePartition *>(
        GetTablePartition(&rt_key));
    return dynamic_cast<MvpnRoute *>(rtp->Find(&rt_key));
}

const MvpnRoute *MvpnTable::FindRoute(const MvpnPrefix &prefix) const {
    MvpnRoute rt_key(prefix);
    const DBTablePartition *rtp = static_cast<const DBTablePartition *>(
        GetTablePartition(&rt_key));
    return dynamic_cast<const MvpnRoute *>(rtp->Find(&rt_key));
}

// Find or create the route.
MvpnRoute *MvpnTable::LocateRoute(const MvpnPrefix &prefix) {
    MvpnRoute rt_key(prefix);
    DBTablePartition *rtp = static_cast<DBTablePartition *>(
        GetTablePartition(&rt_key));
    MvpnRoute *dest_route = dynamic_cast<MvpnRoute *>(rtp->Find(&rt_key));
    if (dest_route == NULL) {
        dest_route = new MvpnRoute(prefix);
        rtp->Add(dest_route);
    } else {
        dest_route->ClearDelete();
    }
    return dest_route;
}

MvpnPrefix MvpnTable::CreateType4LeafADRoutePrefix(const MvpnRoute *type3_rt) {
    assert(type3_rt->GetPrefix().type() == MvpnPrefix::SPMSIADRoute);
    const Ip4Address originator_ip(server()->bgp_identifier());
    MvpnPrefix prefix(MvpnPrefix::LeafADRoute, originator_ip);
    prefix.SetLeafADPrefixFromSPMSIPrefix(type3_rt->GetPrefix());
    return prefix;
}

MvpnRoute *MvpnTable::LocateType4LeafADRoute(const MvpnRoute *type3_spmsi_rt) {
    MvpnPrefix prefix = CreateType4LeafADRoutePrefix(type3_spmsi_rt);
    return LocateRoute(prefix);
}

MvpnPrefix MvpnTable::CreateType3SPMSIRoutePrefix(const MvpnRoute *type7_rt) {
    assert(type7_rt->GetPrefix().type() == MvpnPrefix::SourceTreeJoinRoute);
    const RouteDistinguisher rd = type7_rt->GetPrefix().route_distinguisher();
    Ip4Address source = type7_rt->GetPrefix().source();
    Ip4Address group = type7_rt->GetPrefix().group();
    const Ip4Address originator_ip(server()->bgp_identifier());
    MvpnPrefix prefix(MvpnPrefix::SPMSIADRoute, rd, originator_ip,
            group, source);
    return prefix;
}

MvpnPrefix MvpnTable::CreateLocalType7Prefix(MvpnRoute *rt) const {
    const RouteDistinguisher rd =  RouteDistinguisher::kZeroRd;
    Ip4Address source = rt->GetPrefix().source();
    Ip4Address group = rt->GetPrefix().group();
    MvpnPrefix prefix(MvpnPrefix::SourceTreeJoinRoute, rd,
                      0, group, source);
    return prefix;
}

MvpnPrefix MvpnTable::CreateType7SourceTreeJoinRoutePrefix(
        MvpnRoute *rt) const {
    // get the source-rd from attributes as we store type-5 route with zero-rd
    const BgpAttr *attr = rt->BestPath()->GetAttr();
    assert(attr);
    assert(!attr->source_rd().IsZero());
    const RouteDistinguisher rd =  attr->source_rd();
    Ip4Address source = rt->GetPrefix().source();
    Ip4Address group = rt->GetPrefix().group();
    MvpnPrefix prefix(MvpnPrefix::SourceTreeJoinRoute, rd,
                      server()->autonomous_system(), group, source);
    return prefix;
}

MvpnRoute *MvpnTable::LocateType3SPMSIRoute(const MvpnRoute *type7_rt) {
    MvpnPrefix prefix = CreateType3SPMSIRoutePrefix(type7_rt);
    return LocateRoute(prefix);
}

MvpnPrefix MvpnTable::CreateType2ADRoutePrefix() {
    const RouteDistinguisher *rd = routing_instance()->GetRD();
    MvpnPrefix prefix(MvpnPrefix::InterASPMSIADRoute, *rd,
            server()->autonomous_system());
    return prefix;
}

MvpnPrefix MvpnTable::CreateType1ADRoutePrefix(
        const Ip4Address &originator_ip) {
    const RouteDistinguisher rd(originator_ip.to_ulong(),
                                routing_instance()->index());
    MvpnPrefix prefix(MvpnPrefix::IntraASPMSIADRoute, rd, originator_ip);
    return prefix;
}

MvpnPrefix MvpnTable::CreateType1ADRoutePrefix() {
    return CreateType1ADRoutePrefix(Ip4Address(server()->bgp_identifier()));
}

MvpnRoute *MvpnTable::LocateType1ADRoute() {
    MvpnPrefix prefix = CreateType1ADRoutePrefix();
    return LocateRoute(prefix);
}

MvpnRoute *MvpnTable::FindType1ADRoute(const Ip4Address &originator_ip) {
    MvpnPrefix prefix = CreateType1ADRoutePrefix(originator_ip);
    return FindRoute(prefix);
}

MvpnRoute *MvpnTable::FindType1ADRoute() {
    Ip4Address originator_ip(server()->bgp_identifier());
    return FindType1ADRoute(Ip4Address(server()->bgp_identifier()));
}

const MvpnRoute *MvpnTable::FindType7SourceTreeJoinRoute(MvpnRoute *rt) const {
    MvpnPrefix prefix;
    if (rt->GetPrefix().type() == MvpnPrefix::SourceActiveADRoute)
        prefix = CreateType7SourceTreeJoinRoutePrefix(rt);
    if (rt->GetPrefix().type() == MvpnPrefix::SPMSIADRoute) {
        prefix = CreateLocalType7Prefix(rt);
    }
    return FindRoute(prefix);
}

BgpRoute *MvpnTable::RouteReplicate(BgpServer *server, BgpTable *stable,
        BgpRoute *rt, const BgpPath *src_path, ExtCommunityPtr community) {
    MvpnTable *src_table = dynamic_cast<MvpnTable *>(stable);
    assert(src_table);
    MvpnRoute *src_rt = dynamic_cast<MvpnRoute *>(rt);
    assert(src_rt);

    if (!server->mvpn_ipv4_enable()) {
        return ReplicatePath(server, src_rt->GetPrefix(), src_table, src_rt,
                             src_path, community);
    }

    // Replicate Type7 C-Join route.
    if (src_rt->GetPrefix().type() == MvpnPrefix::SourceTreeJoinRoute) {
        return ReplicateType7SourceTreeJoin(server, src_table, src_rt,
                                            src_path, community);
    }

    if (!IsMaster()) {
        // For type-4 paths, only replicate if there is a type-3 primary path
        // present in the table.
        if (src_rt->GetPrefix().type() == MvpnPrefix::LeafADRoute) {
            MvpnProjectManager *pm = GetProjectManager();
            if (!pm)
                return NULL;
            MvpnStatePtr mvpn_state = pm->GetState(src_rt);
            if (!mvpn_state || !mvpn_state->spmsi_rt() ||
                    !mvpn_state->spmsi_rt()->IsUsable()) {
                return NULL;
            }
            if (mvpn_state->spmsi_rt()->table() != this)
                return NULL;
        }
    }

    // Replicate all other types.
    return ReplicatePath(server, src_rt->GetPrefix(), src_table, src_rt,
                         src_path, community);
}

BgpRoute *MvpnTable::ReplicateType7SourceTreeJoin(BgpServer *server,
    MvpnTable *src_table, MvpnRoute *src_rt, const BgpPath *src_path,
    ExtCommunityPtr ext_community) {

    // Only replicate if route has a target that matches this table's auto
    // created route target (vit).
    if (!IsMaster()) {
        RouteTarget vit(Ip4Address(server->bgp_identifier()),
                                   routing_instance()->index());
        bool vit_found = false;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_community->communities()) {
            if (ExtCommunity::is_route_target(comm)) {
                RouteTarget rtarget(comm);
                if (rtarget == vit) {
                    vit_found = true;
                    break;
                }
            }
        }

        if (!vit_found) {
            MVPN_RT_LOG(src_rt, "Route was not replicated as rt-import "
                        "extended-community was not found");
            return NULL;
        }
    }

    // If replicating from Master table, no special checks are required.
    if (src_table->IsMaster()) {
        return ReplicatePath(server, src_rt->GetPrefix(), src_table, src_rt,
                             src_path, ext_community);
    }

    // This is the case when routes are replicated either to Master or to other
    // vrf.mvpn.0 as identified the route targets. In either case, basic idea
    // is to target the replicated path directly to vrf where sender resides.
    //
    // Route-target of the target vrf is derived from the Vrf Import Target of
    // the route the source resolves to. Resolver code would have already
    // computed this and encoded inside source-rd. Also source-as to encode in
    // the RD is also encoded as part of the SourceAS extended community.
    const BgpAttr *attr = src_path->GetAttr();
    if (!attr)
        return NULL;

    // Do not resplicate if the source is not resolvable.
    if (attr->source_rd().IsZero()) {
        MVPN_RT_LOG(src_rt, "Route was not replicated as source_rd is zero");
        return NULL;
    }

    // Find source-as extended-community. If not present, do not replicate
    bool source_as_found = false;
    SourceAs source_as;
    BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &value,
                  attr->ext_community()->communities()) {
        if (ExtCommunity::is_source_as(value)) {
            source_as_found = true;
            source_as = SourceAs(value);
            break;
        }
    }

    if (!source_as_found) {
        MVPN_RT_LOG(src_rt, "Route was not replicated as source_as is zero");
        return NULL;
    }

    // No need to send SourceAS with this mvpn route. This is only sent along
    // with the unicast routes.
    ext_community =
        server->extcomm_db()->RemoveSourceASAndLocate(ext_community.get());

    // Replicate path using source route's<C-S,G>, source_rd and asn as encoded
    // in the source-as attribute.
    MvpnPrefix prefix(MvpnPrefix::SourceTreeJoinRoute, attr->source_rd(),
                      source_as.GetAsn(), src_rt->GetPrefix().group(),
                      src_rt->GetPrefix().source());

    // Replicate the path with the computed prefix and attributes.
    return ReplicatePath(server, prefix, src_table, src_rt, src_path,
                         ext_community);
}

BgpRoute *MvpnTable::ReplicatePath(BgpServer *server, const MvpnPrefix &mprefix,
        MvpnTable *src_table, MvpnRoute *src_rt, const BgpPath *src_path,
        ExtCommunityPtr comm) {
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
                                                        comm.get());
    // Replace Nexthop with controller address, MX rejects the route if
    // nexthop is same as neighbor address, needed for Type-7 routes
    new_attr = server->attr_db()->ReplaceNexthopAndLocate(new_attr.get(),
                                  Ip4Address(server->bgp_identifier()));
    // Need to strip off route targets other than sender-ip:0
    if (src_rt->GetPrefix().type() == MvpnPrefix::LeafADRoute) {
        ExtCommunity::ExtCommunityList rtarget;
        Ip4Address ip = src_rt->GetPrefix().GetType3OriginatorFromType4Route();
        RouteTarget leaf_ad_rtarget(ip, 0);
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &value,
                      comm->communities()) {
            if (ExtCommunity::is_route_target(value)) {
                if (leaf_ad_rtarget == RouteTarget(value)) {
                    rtarget.push_back(value);
                    break;
                }
            }
        }

        if (rtarget.size() == 1) {
            ExtCommunityPtr ext_community = server->extcomm_db()->
                ReplaceRTargetAndLocate(new_attr->ext_community(), rtarget);
            new_attr = server->attr_db()->ReplaceExtCommunityAndLocate(
                src_path->GetAttr(), ext_community.get());
        } else {
            MVPN_RT_LOG(src_rt,
                        "Could not find <originator>:0 route-target community");
        }
    }

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
    MVPN_RT_LOG(src_rt, "Route was successfully replicated");
    return dest_route;
}

bool MvpnTable::Export(RibOut *ribout, Route *route,
    const RibPeerSet &peerset, UpdateInfoSList &uinfo_slist) {
    MvpnRoute *mvpn_route = dynamic_cast<MvpnRoute *>(route);

    if (ribout->IsEncodingXmpp()) {
        UpdateInfo *uinfo = GetMvpnUpdateInfo(ribout, mvpn_route, peerset);
        if (!uinfo)
            return false;
        uinfo_slist->push_front(*uinfo);
        return true;
    }
    BgpRoute *bgp_route = static_cast<BgpRoute *> (route);

    UpdateInfo *uinfo = GetUpdateInfo(ribout, bgp_route, peerset);
    if (!uinfo) {
        MVPN_RT_LOG(mvpn_route, "Route was exported as update_info could not "
                    "be computed");
        return false;
    }
    uinfo_slist->push_front(*uinfo);
    return true;
}

void MvpnTable::GetPeerSet(RibOut *ribout, MvpnRoute *route,
        const RibPeerSet &peerset, RibPeerSet *new_peerset) {
    RibOut::PeerIterator iter(ribout, peerset);
    while (iter.HasNext()) {
        int current_index = iter.index();
        IPeer *peer = dynamic_cast<IPeer *>(iter.Next());
        assert(peer);
        for (Route::PathList::const_iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if (path->IsFeasible() && peer == path->GetPeer()) {
                new_peerset->set(current_index);
                break;
            }
        }
    }
}

UpdateInfo *MvpnTable::GetMvpnUpdateInfo(RibOut *ribout, MvpnRoute *route,
    const RibPeerSet &peerset) {
    if ((route->GetPrefix().type() != MvpnPrefix::SourceActiveADRoute) &&
        (route->GetPrefix().type() != MvpnPrefix::SourceTreeJoinRoute))
        return NULL;
    if (!route->IsUsable())
        return NULL;

    if (route->BestPath()->IsReplicated())
        return NULL;

    MvpnProjectManager *pm = GetProjectManager();
    if (!pm) {
        MVPN_RT_LOG(route, "Route was exported as ProjectManager was "
                    "not found");
        return NULL;
    }

    RibPeerSet new_peerset;
    GetPeerSet(ribout, route, peerset, &new_peerset);

    if (new_peerset.empty())
        return NULL;

    UpdateInfo *uinfo = pm->GetUpdateInfo(route);
    if (uinfo)
        uinfo->target = new_peerset;
    return uinfo;
}

bool MvpnTable::IsMaster() const {
    return routing_instance()->IsMasterRoutingInstance();
}

static void RegisterFactory() {
    DB::RegisterFactory("mvpn.0", &MvpnTable::CreateTable);
}

MODULE_INITIALIZER(RegisterFactory);
