/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_inet6vpn_table_h
#define ctrlplane_inet6vpn_table_h

#include "bgp/bgp_attr.h"
#include "bgp/bgp_table.h"
#include "bgp/inet6vpn/inet6vpn_route.h"

class BgpServer;
class BgpRoute;

class Inet6VpnTable : public BgpTable {
public:
    struct RequestKey : BgpTable::RequestKey {
        RequestKey(const Inet6VpnPrefix &prefix, const IPeer *ipeer)
            : prefix(prefix), peer(ipeer) {
        }
        Inet6VpnPrefix prefix;
        const IPeer *peer;
        virtual const IPeer *GetPeer() const { return peer; }
    };

    Inet6VpnTable(DB *db, const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;
    virtual std::auto_ptr<DBEntry> AllocEntryStr(const std::string &key) const;

    virtual Address::Family family() const { return Address::INET6VPN; }
    virtual bool IsVpnTable() const { return true; }

    virtual size_t Hash(const DBEntry *entry) const;
    virtual size_t Hash(const DBRequestKey *key) const;

    virtual BgpRoute *RouteReplicate(BgpServer *server, BgpTable *src_table,
                                     BgpRoute *src_rt, const BgpPath *path,
                                     ExtCommunityPtr ptr);

    virtual bool Export(RibOut *ribout, Route *route, const RibPeerSet &peerset,
                        UpdateInfoSList &info_slist);
    static DBTableBase *CreateTable(DB *db, const std::string &name);

private:
    virtual BgpRoute *TableFind(DBTablePartition *rtp,
                                const DBRequestKey *prefix);

    DISALLOW_COPY_AND_ASSIGN(Inet6VpnTable);
};

#endif /* ctrlplane_inet6vpn_table_h */
