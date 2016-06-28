/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ERMVPN_ERMVPN_TABLE_H_
#define SRC_BGP_ERMVPN_ERMVPN_TABLE_H_

#include <string>

#include "bgp/bgp_attr.h"
#include "bgp/bgp_table.h"
#include "bgp/ermvpn/ermvpn_route.h"

class BgpServer;
class BgpRoute;
class McastTreeManager;

class ErmVpnTable : public BgpTable {
public:
    static const int kPartitionCount = 1;

    struct RequestKey : BgpTable::RequestKey {
        RequestKey(const ErmVpnPrefix &prefix, const IPeer *ipeer)
            : prefix(prefix), peer(ipeer) {
        }
        ErmVpnPrefix prefix;
        const IPeer *peer;
        virtual const IPeer *GetPeer() const { return peer; }
    };

    ErmVpnTable(DB *db, const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;
    virtual std::auto_ptr<DBEntry> AllocEntryStr(const std::string &key) const;

    virtual Address::Family family() const { return Address::ERMVPN; }
    bool IsMaster() const;
    virtual bool IsVpnTable() const { return IsMaster(); }

    virtual size_t Hash(const DBEntry *entry) const;
    virtual size_t Hash(const DBRequestKey *key) const;
    virtual int PartitionCount() const { return kPartitionCount; }

    virtual BgpRoute *RouteReplicate(BgpServer *server, BgpTable *src_table,
                                     BgpRoute *src_rt, const BgpPath *path,
                                     ExtCommunityPtr ptr);

    virtual bool Export(RibOut *ribout, Route *route,
                        const RibPeerSet &peerset,
                        UpdateInfoSList &info_slist);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    size_t HashFunction(const ErmVpnPrefix &prefix) const;

    void CreateTreeManager();
    void DestroyTreeManager();
    McastTreeManager *GetTreeManager();
    const McastTreeManager *GetTreeManager() const;
    virtual void set_routing_instance(RoutingInstance *rtinstance);

private:
    friend class BgpMulticastTest;

    virtual BgpRoute *TableFind(DBTablePartition *rtp,
                                const DBRequestKey *prefix);
    McastTreeManager *tree_manager_;

    DISALLOW_COPY_AND_ASSIGN(ErmVpnTable);
};

#endif  // SRC_BGP_ERMVPN_ERMVPN_TABLE_H_
