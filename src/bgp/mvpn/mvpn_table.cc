/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/mvpn/mvpn_table.h"

#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/extended-community/source_as.h"
#include "bgp/ipeer.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_multicast.h"
#include "bgp/bgp_mvpn.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update.h"
#include "bgp/inet/inet_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/path_resolver.h"
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
    : BgpTable(db, name), manager_(NULL) {
}

PathResolver *MvpnTable::CreatePathResolver() {
    if (routing_instance()->IsMasterRoutingInstance())
        return NULL;
    return (new PathResolver(this));
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
    // Don't create the MvpnManager for the VPN table.
    if (IsMaster())
        return;
    assert(!manager_);
    manager_ = BgpObjectFactory::Create<MvpnManager>(this);
    manager_->Initialize();
}

void MvpnTable::DestroyManager() {
    assert(manager_);
    manager_->Terminate();
    delete manager_;
    manager_ = NULL;
}

// Call the const version to avoid code duplication.
MvpnProjectManager *MvpnTable::GetProjectManager() {
    return const_cast<MvpnProjectManager *>(
        static_cast<const MvpnTable *>(this)->GetProjectManager());
}

// Call the const version to avoid code duplication.
MvpnProjectManagerPartition *MvpnTable::GetProjectManagerPartition(
        BgpRoute *rt) {
    return const_cast<MvpnProjectManagerPartition *>(
        static_cast<const MvpnTable *>(this)->GetProjectManagerPartition(rt));
}

// Get MvpnProjectManager object for this Mvpn. Each MVPN network is associated
// with a parent project maanger network via configuration. MvpnProjectManager
// is retrieved from this parent network RoutingInstance's ErmVpnTable.
const MvpnProjectManager *MvpnTable::GetProjectManager() const {
    std::string pm_network =
        routing_instance()->mvpn_project_manager_network();
    if (pm_network.empty())
        return NULL;
    const RoutingInstance *rtinstance =
        routing_instance()->manager()->GetRoutingInstance(pm_network);
    if (!rtinstance || rtinstance->deleted())
        return NULL;
    const ErmVpnTable *table = dynamic_cast<const ErmVpnTable *>(
        rtinstance->GetTable(Address::ERMVPN));
    if (!table || table->IsDeleted())
        return NULL;
    return table->mvpn_project_manager();
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
    if (IsMaster())
        return;
    MvpnRoute *mvpn_rt = dynamic_cast<MvpnRoute *>(rt);
    assert(mvpn_rt);

    // Special table lookup is required only for the Type4 LeafAD routes.
    if (mvpn_rt->GetPrefix().type() != MvpnPrefix::LeafADRoute)
        return;

    manager()->UpdateSecondaryTablesForReplication(mvpn_rt, secondary_tables);
}

// Find or create the route.
MvpnRoute *MvpnTable::FindRoute(MvpnPrefix &prefix) {
    MvpnRoute rt_key(prefix);
    DBTablePartition *rtp = static_cast<DBTablePartition *>(
        GetTablePartition(&rt_key));
    return static_cast<MvpnRoute *>(rtp->Find(&rt_key));
}

const MvpnRoute *MvpnTable::FindRoute(MvpnPrefix &prefix) const {
    return const_cast<MvpnRoute *>(
        static_cast<const MvpnTable *>(this)->FindRoute(prefix));
}

// Find or create the route.
MvpnRoute *MvpnTable::LocateRoute(MvpnPrefix &prefix) {
    MvpnRoute rt_key(prefix);
    DBTablePartition *rtp = static_cast<DBTablePartition *>(
        GetTablePartition(&rt_key));
    MvpnRoute *dest_route = static_cast<MvpnRoute *>(rtp->Find(&rt_key));
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
    prefix.SetRtKeyFromSPMSIADRoute(type3_rt->GetPrefix());
    return prefix;
}

MvpnRoute *MvpnTable::LocateType4LeafADRoute(const MvpnRoute *type3_spmsi_rt) {
    MvpnPrefix prefix = CreateType4LeafADRoutePrefix(type3_spmsi_rt);
    return LocateRoute(prefix);
}

MvpnPrefix MvpnTable::CreateType3SPMSIRoutePrefix(MvpnRoute *type7_rt) {
    assert(type7_rt->GetPrefix().type() == MvpnPrefix::SourceTreeJoinRoute);
    const RouteDistinguisher *rd = routing_instance()->GetRD();
    Ip4Address source = type7_rt->GetPrefix().source();
    Ip4Address group = type7_rt->GetPrefix().group();
    const Ip4Address originator_ip(server()->bgp_identifier());
    MvpnPrefix prefix(MvpnPrefix::SPMSIADRoute, *rd, originator_ip,
            group, source);
    return prefix;
}

MvpnPrefix MvpnTable::CreateType5SourceActiveRoutePrefix(MvpnRoute *rt) const {
    const RouteDistinguisher *rd = routing_instance()->GetRD();
    Ip4Address source = rt->GetPrefix().source();
    Ip4Address group = rt->GetPrefix().group();
    const Ip4Address originator_ip(server()->bgp_identifier());
    MvpnPrefix prefix(MvpnPrefix::SourceActiveADRoute, *rd, group, source);
    return prefix;
}

MvpnRoute *MvpnTable::LocateType3SPMSIRoute(MvpnRoute *type7_rt) {
    MvpnPrefix prefix = CreateType3SPMSIRoutePrefix(type7_rt);
    return LocateRoute(prefix);
}

MvpnPrefix MvpnTable::CreateType2ADRoutePrefix() {
    const RouteDistinguisher *rd = routing_instance()->GetRD();
    MvpnPrefix prefix(MvpnPrefix::InterASPMSIADRoute, *rd,
            server()->autonomous_system());
    return prefix;
}

MvpnRoute *MvpnTable::LocateType2ADRoute() {
    MvpnPrefix prefix = CreateType2ADRoutePrefix();
    return LocateRoute(prefix);
}

MvpnPrefix MvpnTable::CreateType1ADRoutePrefix(
        const Ip4Address &originator_ip) {
    const RouteDistinguisher *rd = routing_instance()->GetRD();
    MvpnPrefix prefix(MvpnPrefix::IntraASPMSIADRoute, *rd, originator_ip);
    return prefix;
}

MvpnPrefix MvpnTable::CreateType1ADRoutePrefix() {
    return CreateType1ADRoutePrefix(Ip4Address(server()->bgp_identifier()));
}

MvpnRoute *MvpnTable::LocateType1ADRoute() {
    MvpnPrefix prefix = CreateType1ADRoutePrefix(
            Ip4Address(server()->bgp_identifier()));
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

MvpnRoute *MvpnTable::FindType2ADRoute() {
    MvpnPrefix prefix = CreateType2ADRoutePrefix();
    return FindRoute(prefix);
}

MvpnRoute *MvpnTable::FindType5SourceActiveADRoute(MvpnRoute *rt) {
    MvpnPrefix prefix = CreateType5SourceActiveRoutePrefix(rt);
    return FindRoute(prefix);
}

const MvpnRoute *MvpnTable::FindType5SourceActiveADRoute(MvpnRoute *rt) const {
    MvpnPrefix prefix = CreateType5SourceActiveRoutePrefix(rt);
    return FindRoute(prefix);
}

BgpRoute *MvpnTable::RouteReplicate(BgpServer *server,
        BgpTable *stable, BgpRoute *src_rt, const BgpPath *src_path,
        ExtCommunityPtr community) {
    MvpnTable *src_table = dynamic_cast<MvpnTable *>(stable);
    assert(src_table);
    assert(src_table->family() == Address::MVPN);

    MvpnRoute *mvpn_route = dynamic_cast<MvpnRoute *>(src_rt);
    assert(mvpn_route);

    if (!IsMaster()) {
        // For type-4 paths, only replicate if there is a type-3 primary path
        // present in the table.
        if (mvpn_route->GetPrefix().type() == MvpnPrefix::LeafADRoute) {
            MvpnProjectManager *pm = GetProjectManager();
            if (!pm)
                return NULL;
            MvpnStatePtr mvpn_state = pm->GetState(mvpn_route);
            if (!mvpn_state || !mvpn_state->spmsi_rt() ||
                    !mvpn_state->spmsi_rt()->IsUsable()) {
                return NULL;
            }
        }

        // For type-7 paths, only replicate if route has a target that matches
        // this table's auto created route target (vit).
    }

    MvpnPrefix mprefix(mvpn_route->GetPrefix());
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

bool MvpnTable::IsMaster() const {
    return routing_instance()->IsMasterRoutingInstance();
}

static void RegisterFactory() {
    DB::RegisterFactory("mvpn.0", &MvpnTable::CreateTable);
}

MODULE_INITIALIZER(RegisterFactory);
