/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_inetmcast_table_h
#define ctrlplane_inetmcast_table_h

#include "bgp/bgp_table.h"
#include "bgp/inetmcast/inetmcast_route.h"
#include "route/table.h"

class McastTreeManager;

class InetMcastTable : public BgpTable {
public:
    struct RequestKey : BgpTable::RequestKey {
        RequestKey(const InetMcastPrefix &prefix, const IPeer *ipeer)
            : prefix(prefix), peer(ipeer) {
        }
        InetMcastPrefix prefix;
        const IPeer *peer;
        virtual const IPeer *GetPeer() const { return peer; }
    };

    InetMcastTable(DB *db, const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;
    virtual std::auto_ptr<DBEntry> AllocEntryStr(const std::string &key) const;

    virtual Address::Family family() const { return Address::INETMCAST; }

    virtual size_t Hash(const DBEntry *entry) const;
    virtual size_t Hash(const DBRequestKey *key) const;

    virtual BgpRoute *RouteReplicate(BgpServer *server, BgpTable *src_table,
                                     BgpRoute *src_rt, const BgpPath *path, 
                                     ExtCommunityPtr ptr);


    virtual bool Export(RibOut *ribout, Route *route,
                        const RibPeerSet &peerset,
                        UpdateInfoSList &info_slist);

    void CreateTreeManager();
    void DestroyTreeManager();
    McastTreeManager *GetTreeManager();

    static size_t HashFunction(const InetMcastPrefix &prefix);
    static DBTableBase *CreateTable(DB *db, const std::string &name);

    virtual void set_routing_instance(RoutingInstance *rtinstance);

private:
    friend class BgpMulticastTest;

    virtual BgpRoute *TableFind(DBTablePartition *rtp,
                                const DBRequestKey *prefix);

    McastTreeManager *tree_manager_;

    DISALLOW_COPY_AND_ASSIGN(InetMcastTable);
};

#endif
