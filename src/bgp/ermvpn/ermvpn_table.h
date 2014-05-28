/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ermvpn_table_h
#define ctrlplane_ermvpn_table_h

#include "bgp/bgp_attr.h"
#include "bgp/bgp_table.h"
#include "bgp/ermvpn/ermvpn_route.h"

class BgpServer;
class BgpRoute;
class McastTreeManager;

class ErmVpnTable : public BgpTable {
public:
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
    virtual bool IsVpnTable() const { return IsDefault(); }

    virtual size_t Hash(const DBEntry *entry) const;
    virtual size_t Hash(const DBRequestKey *key) const;

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
    virtual void set_routing_instance(RoutingInstance *rtinstance);

    bool IsDefault() const;

private:
    friend class BgpMulticastTest;

    virtual BgpRoute *TableFind(DBTablePartition *rtp, 
                                const DBRequestKey *prefix);
    McastTreeManager *tree_manager_;

    DISALLOW_COPY_AND_ASSIGN(ErmVpnTable);
};

#endif
