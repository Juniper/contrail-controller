/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_INET_INET_TABLE_H_
#define SRC_BGP_INET_INET_TABLE_H_

#include <string>

#include "bgp/bgp_table.h"
#include "bgp/inet/inet_route.h"
#include "net/address.h"

class Ip4Prefix;
class BgpServer;
class PathResolver;

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
    virtual PathResolver *CreatePathResolver();

    static size_t HashFunction(const Ip4Prefix &addr);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    BgpRoute *RouteReplicate(BgpServer *server, BgpTable *src_tbl,
                             BgpRoute *src_rt, const BgpPath *path,
                             ExtCommunityPtr ptr);

    virtual bool IsRoutingPolicySupported() const { return true; }
    virtual bool IsRouteAggregationSupported() const { return true; }
private:
    virtual BgpRoute *TableFind(DBTablePartition *rtp,
                                const DBRequestKey *prefix);

    DISALLOW_COPY_AND_ASSIGN(InetTable);
};

#endif  // SRC_BGP_INET_INET_TABLE_H_
