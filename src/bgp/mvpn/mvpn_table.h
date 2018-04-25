/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_MVPN_MVPN_TABLE_H_
#define SRC_BGP_MVPN_MVPN_TABLE_H_

#include <map>
#include <set>
#include <string>

#include "bgp/bgp_attr.h"
#include "bgp/bgp_table.h"
#include "bgp/mvpn/mvpn_route.h"
#include "bgp/routing-instance/path_resolver.h"

class BgpPath;
class BgpRoute;
class BgpServer;
class MvpnManager;
class MvpnProjectManager;
class MvpnProjectManagerPartition;

class MvpnTable : public BgpTable {
public:
    static const int kPartitionCount = 1;

    struct RequestKey : BgpTable::RequestKey {
        RequestKey(const MvpnPrefix &prefix, const IPeer *ipeer)
            : prefix(prefix), peer(ipeer) {
        }
        MvpnPrefix prefix;
        const IPeer *peer;
        virtual const IPeer *GetPeer() const { return peer; }
    };

    MvpnTable(DB *db, const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;
    virtual std::auto_ptr<DBEntry> AllocEntryStr(const std::string &key) const;
    void CreateManager();
    void DestroyManager();
    virtual Address::Family family() const { return Address::MVPN; }
    bool IsMaster() const;
    virtual bool IsVpnTable() const { return IsMaster(); }

    virtual size_t Hash(const DBEntry *entry) const;
    virtual size_t Hash(const DBRequestKey *key) const;
    virtual int PartitionCount() const { return kPartitionCount; }
    MvpnRoute *FindSPMSIRoute(MvpnRoute *leaf_ad_rt) { return NULL; }
    const ExtCommunity::ExtCommunityValue GetAutoVrfImportRouteTarget() const {
        return ExtCommunity::ExtCommunityValue();
    }

    virtual BgpRoute *RouteReplicate(BgpServer *server, BgpTable *src_table,
                                     BgpRoute *src_rt, const BgpPath *path,
                                     ExtCommunityPtr ptr);
    virtual bool Export(RibOut *ribout, Route *route,
                        const RibPeerSet &peerset,
                        UpdateInfoSList &info_slist);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    size_t HashFunction(const MvpnPrefix &prefix) const;
    PathResolver *CreatePathResolver();
    const MvpnManager *manager() const { return manager_; }
    MvpnManager *manager() { return manager_; }

    bool RouteNotify(BgpServer *server, DBTablePartBase *root, DBEntryBase *e);
    const MvpnProjectManager *GetProjectManager() const;
    MvpnProjectManager *GetProjectManager();
    const MvpnProjectManagerPartition *GetProjectManagerPartition(
            BgpRoute *route) const;
    void UpdateSecondaryTablesForReplication(BgpRoute *rt,
        TableSet *secondary_tables);
    MvpnPrefix CreateType4LeafADRoutePrefix(const MvpnRoute *type3_rt);
    MvpnPrefix CreateType3SPMSIRoutePrefix(const MvpnRoute *type7_rt);
    MvpnPrefix CreateType2ADRoutePrefix();
    MvpnPrefix CreateType1ADRoutePrefix(const Ip4Address &originator_ip);
    MvpnPrefix CreateType1ADRoutePrefix();
    MvpnPrefix CreateType7SourceTreeJoinRoutePrefix(MvpnRoute *rt) const;
    MvpnPrefix CreateLocalType7Prefix(MvpnRoute *rt) const;
    MvpnRoute *FindType1ADRoute(const Ip4Address &originator_ip);
    MvpnRoute *FindType1ADRoute();
    const MvpnRoute *FindType7SourceTreeJoinRoute(MvpnRoute *rt) const;
    MvpnRoute *LocateType1ADRoute();
    MvpnRoute *LocateType3SPMSIRoute(const MvpnRoute *type7_join_rt);
    MvpnRoute *LocateType4LeafADRoute(const MvpnRoute *type3_spmsi_rt);
    MvpnRoute *FindRoute(const MvpnPrefix &prefix);
    const MvpnRoute *FindRoute(const MvpnPrefix &prefix) const;
    void CreateMvpnManagers();
    bool IsProjectManagerUsable() const;

private:
    friend class BgpMulticastTest;
    typedef std::set<std::string> MvpnManagerNetworks;
    typedef std::map<std::string, MvpnManagerNetworks>
        MvpnProjectManagerNetworks;

    void DeleteMvpnManager();
    virtual BgpRoute *TableFind(DBTablePartition *rtp,
                                const DBRequestKey *prefix);
    MvpnRoute *LocateRoute(const MvpnPrefix &prefix);
    UpdateInfo *GetMvpnUpdateInfo(RibOut *ribout, MvpnRoute *route,
                                  const RibPeerSet &peerset);
    void GetPeerSet(RibOut *ribout, MvpnRoute *route,
                    const RibPeerSet &peerset, RibPeerSet *new_peerset);
    BgpRoute *ReplicateType7SourceTreeJoin(BgpServer *server,
        MvpnTable *src_table, MvpnRoute *src_rt, const BgpPath *src_path,
        ExtCommunityPtr ext_community);
    BgpRoute *ReplicatePath(BgpServer *server, const MvpnPrefix &mprefix,
        MvpnTable *src_table, MvpnRoute *src_rt, const BgpPath *src_path,
        ExtCommunityPtr comm);
    const MvpnTable *table() const { return this; }

    MvpnManager *manager_;

    DISALLOW_COPY_AND_ASSIGN(MvpnTable);
};

#endif  // SRC_BGP_MVPN_MVPN_TABLE_H_
