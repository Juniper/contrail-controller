/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_inet_table_h
#define ctrlplane_inet_table_h

#include "bgp/bgp_table.h"
#include "bgp/inet/inet_route.h"
#include "net/address.h"
#include "route/table.h"

class Ip4Prefix;
class BgpServer;

class InetTable : public BgpTable {
public:
    struct RequestKey : BgpTable::RequestKey {
        RequestKey(const Ip4Prefix &prefix, const IPeer *ipeer)
            : prefix(prefix), peer(ipeer) {
        }
        Ip4Prefix prefix;
        const IPeer *peer;
        virtual const IPeer *GetPeer() const {
            return peer;
        }
    };

    InetTable(DB *db, const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;
    virtual std::auto_ptr<DBEntry> AllocEntryStr(const std::string &key) const;

    virtual Address::Family family() const { return Address::INET; }

    virtual size_t Hash(const DBEntry *entry) const;
    virtual size_t Hash(const DBRequestKey *key) const;

    virtual bool Export(RibOut *ribout, Route *route,
                        const RibPeerSet &peerset,
                        UpdateInfoSList &info_slist);

    static size_t HashFunction(const Ip4Prefix &addr);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    BgpRoute *RouteReplicate(BgpServer *server, BgpTable *src_tbl, 
                             BgpRoute *src_rt, const BgpPath *path,
                             ExtCommunityPtr ptr);

private:
    virtual BgpRoute *TableFind(DBTablePartition *rtp, 
                                const DBRequestKey *prefix);

    DISALLOW_COPY_AND_ASSIGN(InetTable);
};

#endif
