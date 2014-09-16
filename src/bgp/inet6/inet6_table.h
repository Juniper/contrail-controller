/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_inet6_table_h
#define ctrlplane_inet6_table_h

#include "bgp/bgp_table.h"
#include "bgp/inet6/inet6_route.h"
#include "net/address.h"
#include "route/table.h"

class Inet6Prefix;
class BgpServer;

class Inet6Table : public BgpTable {
public:
    struct RequestKey : BgpTable::RequestKey {
        RequestKey(const Inet6Prefix &prefix, const IPeer *ipeer)
            : prefix(prefix), peer(ipeer) {
        }
        Inet6Prefix prefix;
        const IPeer *peer;
        virtual const IPeer *GetPeer() const {
            return peer;
        }
    };

    Inet6Table(DB *db, const std::string &name) : BgpTable(db, name) { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;
    virtual std::auto_ptr<DBEntry> AllocEntryStr(const std::string &key) const;

    virtual Address::Family family() const { return Address::INET6; }

    virtual size_t Hash(const DBEntry *entry) const;
    virtual size_t Hash(const DBRequestKey *key) const;

    virtual bool Export(RibOut *ribout, Route *route, const RibPeerSet &peerset,
                        UpdateInfoSList &info_slist);

    static size_t HashFunction(const Inet6Prefix &addr);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    BgpRoute *RouteReplicate(BgpServer *server, BgpTable *src_tbl,
                             BgpRoute *src_rt, const BgpPath *path,
                             ExtCommunityPtr ptr);

private:
    virtual BgpRoute *TableFind(DBTablePartition *partition,
                                const DBRequestKey *rkey);

    DISALLOW_COPY_AND_ASSIGN(Inet6Table);
};

#endif /* ctrlplane_inet6_table_h */
