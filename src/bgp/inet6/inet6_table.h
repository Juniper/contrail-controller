/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_INET6_INET6_TABLE_H_
#define SRC_BGP_INET6_INET6_TABLE_H_

#include <string>

#include "bgp/bgp_table.h"
#include "bgp/inet6/inet6_route.h"
#include "net/address.h"

class Inet6Prefix;
class BgpServer;
class PathResolver;

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

    Inet6Table(DB *db, const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;
    virtual std::auto_ptr<DBEntry> AllocEntryStr(const std::string &key) const;

    virtual Address::Family family() const { return Address::INET6; }

    virtual size_t Hash(const DBEntry *entry) const;
    virtual size_t Hash(const DBRequestKey *key) const;

    virtual bool Export(RibOut *ribout, Route *route, const RibPeerSet &peerset,
                        UpdateInfoSList &info_slist);
    virtual PathResolver *CreatePathResolver();

    static size_t HashFunction(const Inet6Prefix &addr);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    BgpRoute *RouteReplicate(BgpServer *server, BgpTable *src_tbl,
                             BgpRoute *src_rt, const BgpPath *path,
                             ExtCommunityPtr ptr);

    virtual bool IsRoutingPolicySupported() const { return true; }
    virtual bool IsRouteAggregationSupported() const { return true; }
private:
    virtual BgpRoute *TableFind(DBTablePartition *partition,
                                const DBRequestKey *rkey);

    DISALLOW_COPY_AND_ASSIGN(Inet6Table);
};

#endif  // SRC_BGP_INET6_INET6_TABLE_H_
