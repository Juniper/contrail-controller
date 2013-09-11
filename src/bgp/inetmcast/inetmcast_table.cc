/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inetmcast/inetmcast_table.h"

#include <boost/functional/hash.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_multicast.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_route.h"
#include "bgp/ipeer.h"
#include "bgp/inetmcast/inetmcast_route.h"
#include "bgp/routing-instance/routing_instance.h"
#include "db/db_table_partition.h"

using namespace std;

size_t InetMcastTable::HashFunction(const InetMcastPrefix &prefix) {
    return boost::hash_value(prefix.group().to_ulong());
}

InetMcastTable::InetMcastTable(DB *db, const std::string &name)
    : BgpTable(db, name), tree_manager_(NULL) {
}

std::auto_ptr<DBEntry> InetMcastTable::AllocEntry(
        const DBRequestKey *key) const {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(key);
    return std::auto_ptr<DBEntry> (new InetMcastRoute(pfxkey->prefix));
}

std::auto_ptr<DBEntry> InetMcastTable::AllocEntryStr(
        const string &key_str) const {
    InetMcastPrefix prefix = InetMcastPrefix::FromString(key_str);
    return std::auto_ptr<DBEntry> (new InetMcastRoute(prefix));
}

size_t InetMcastTable::Hash(const DBRequestKey *key) const {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    size_t value = HashFunction(rkey->prefix);
    return value % DB::PartitionCount();
}

size_t InetMcastTable::Hash(const DBEntry *entry) const {
    const InetMcastRoute *rt_entry = static_cast<const InetMcastRoute *>(entry);
    size_t value = HashFunction(rt_entry->GetPrefix());
    return value % DB::PartitionCount();
}

BgpRoute *InetMcastTable::TableFind(DBTablePartition *rtp,
        const DBRequestKey *prefix) {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(prefix);
    InetMcastRoute rt_key(pfxkey->prefix);
    return static_cast<BgpRoute *>(rtp->Find(&rt_key));
}

DBTableBase *InetMcastTable::CreateTable(DB *db, const std::string &name) {
    InetMcastTable *table = new InetMcastTable(db, name);
    table->Init();
    return table;
}

BgpRoute *InetMcastTable::RouteReplicate(BgpServer *server,
        BgpTable *src_table, BgpRoute *src_rt, const BgpPath *path,
        ExtCommunityPtr community) {
    return NULL;
}

bool InetMcastTable::Export(RibOut *ribout, Route *route,
        const RibPeerSet &peerset, UpdateInfoSList &uinfo_slist) {
    InetMcastRoute *mcast_route = dynamic_cast<InetMcastRoute *>(route);
    assert(mcast_route);

    if (!tree_manager_ || tree_manager_->deleter()->IsDeleted())
        return false;

    const IPeer *peer = mcast_route->BestPath()->GetPeer();
    if (!peer || !ribout->IsRegistered(const_cast<IPeer *>(peer)))
        return false;

    size_t peerbit = ribout->GetPeerIndex(const_cast<IPeer *>(peer));
    if (!peerset.test(peerbit))
        return false;

    UpdateInfo *uinfo = tree_manager_->GetUpdateInfo(mcast_route);
    if (!uinfo)
        return false;

    uinfo->target.set(peerbit);
    uinfo_slist->push_front(*uinfo);
    return true;
}

void InetMcastTable::CreateTreeManager() {
    assert(!tree_manager_);
    tree_manager_ = BgpObjectFactory::Create<McastTreeManager>(this);
    tree_manager_->Initialize();
}

void InetMcastTable::DestroyTreeManager() {
    tree_manager_->Terminate();
    delete tree_manager_;
    tree_manager_ = NULL;
}

McastTreeManager *InetMcastTable::GetTreeManager() {
    return tree_manager_;
}

void InetMcastTable::set_routing_instance(RoutingInstance *rtinstance) {
    BgpTable::set_routing_instance(rtinstance);
    CreateTreeManager();
}

static void RegisterFactory() {
    DB::RegisterFactory("inetmcast.0", &InetMcastTable::CreateTable);
}

MODULE_INITIALIZER(RegisterFactory);
