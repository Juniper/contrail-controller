/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/rtarget/rtarget_table.h"

#include "base/util.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "bgp/rtarget/rtarget_route.h"
#include "bgp/routing-instance/routing_instance.h"
#include "db/db_table_partition.h"

using namespace std;

RTargetTable::RTargetTable(DB *db, const string &name)
        : BgpTable(db, name) {
}

std::auto_ptr<DBEntry> RTargetTable::AllocEntry(const DBRequestKey *key) const {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(key);
    return std::auto_ptr<DBEntry> (new RTargetRoute(pfxkey->prefix));
}


std::auto_ptr<DBEntry> RTargetTable::AllocEntryStr(const string &key_str) 
    const {
    RTargetPrefix prefix = RTargetPrefix::FromString(key_str);
    return std::auto_ptr<DBEntry> (new RTargetRoute(prefix));
}

size_t RTargetTable::Hash(const DBEntry *entry) const {
    return 0;
}

size_t RTargetTable::Hash(const DBRequestKey *key) const {
    return 0;
}

BgpRoute *RTargetTable::TableFind(DBTablePartition *rtp, 
                                  const DBRequestKey *prefix) {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(prefix);
    RTargetRoute rt_key(pfxkey->prefix);
    return static_cast<BgpRoute *>(rtp->Find(&rt_key));
}

DBTableBase *RTargetTable::CreateTable(DB *db, const std::string &name) {
    RTargetTable *table = new RTargetTable(db, name);
    table->Init();
    return table;
}

BgpRoute *RTargetTable::RouteReplicate(BgpServer *server,
        BgpTable *src_table, BgpRoute *src_rt, const BgpPath *src_path,
        ExtCommunityPtr community) {
    return NULL;
}

bool RTargetTable::Export(RibOut *ribout, Route *route,
        const RibPeerSet &peerset, UpdateInfoSList &uinfo_slist) {
    BgpRoute *bgp_route = static_cast<BgpRoute *> (route);
    UpdateInfo *uinfo = GetUpdateInfo(ribout, bgp_route, peerset);
    if (!uinfo) return false;
    uinfo_slist->push_front(*uinfo);

    return true;
}

static void RegisterFactory() {
    DB::RegisterFactory("bgp.rtarget.0", &RTargetTable::CreateTable);
}
MODULE_INITIALIZER(RegisterFactory);
