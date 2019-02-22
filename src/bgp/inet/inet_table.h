/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_INET_INET_TABLE_H_
#define SRC_BGP_INET_INET_TABLE_H_

#include <string>

#include "bgp/bgp_table.h"
#include "bgp/inet/inet_route.h"
#include "base/address.h"

class BgpServer;
class InetVpnPrefix;
class Ip4Prefix;
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

    virtual Address::Family family() const { return family_; }

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
    virtual bool IsRouteAggregationSupported() const {
        return((family_ == Address::INETMPLS) ? false : true); }
    virtual BgpAttrPtr GetAttributes(BgpRoute *rt, BgpAttrPtr attrp,
                                     const IPeer *peer);
    void UpdateRoute(const InetVpnPrefix &inetvpn_prefix, const IPeer *peer,
                     BgpAttrPtr inetvpn_attrp);

private:
    BgpAttrPtr GetFabricAttributes(BgpRoute *rt, BgpAttrPtr attrp,
                                   const IPeer *peer);
    BgpAttrPtr GetMvpnAttributes(BgpAttrPtr attrp);
    BgpAttrPtr UpdateAttributes(const BgpAttrPtr inetvpn_attrp,
                                const BgpAttrPtr inet_attrp);
    void UpdateExtendedCommunity(RibOutAttr *roattr);
    virtual BgpRoute *TableFind(DBTablePartition *rtp,
                                const DBRequestKey *prefix);

    Address::Family family_;
    DISALLOW_COPY_AND_ASSIGN(InetTable);
};

#endif  // SRC_BGP_INET_INET_TABLE_H_
