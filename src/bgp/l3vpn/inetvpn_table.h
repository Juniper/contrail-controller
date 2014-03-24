/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_inetvpn_table_h
#define ctrlplane_inetvpn_table_h

#include "bgp/bgp_attr.h"
#include "bgp/bgp_table.h"
#include "bgp/l3vpn/inetvpn_address.h"
#include "bgp/l3vpn/inetvpn_route.h"

class BgpServer;
class BgpRoute;

class InetVpnTable : public BgpTable {
public:
    struct RequestKey : BgpTable::RequestKey {
        RequestKey(const InetVpnPrefix &prefix, const IPeer *ipeer)
            : prefix(prefix), peer(ipeer) {
        }
        InetVpnPrefix prefix;
        const IPeer *peer;
        virtual const IPeer *GetPeer() const { return peer; }
    };

    InetVpnTable(DB *db, const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;
    virtual std::auto_ptr<DBEntry> AllocEntryStr(const std::string &key) const;

    virtual Address::Family family() const { return Address::INETVPN; }
    virtual bool IsVpnTable() const { return true; }

    virtual size_t Hash(const DBEntry *entry) const;
    virtual size_t Hash(const DBRequestKey *key) const;

    virtual BgpRoute *RouteReplicate(BgpServer *server, BgpTable *src_table, 
                                     BgpRoute *src_rt, const BgpPath *path,
                                     ExtCommunityPtr ptr);

    virtual bool Export(RibOut *ribout, Route *route,
                        const RibPeerSet &peerset,
                        UpdateInfoSList &info_slist);
    static DBTableBase *CreateTable(DB *db, const std::string &name);

private:
    virtual BgpRoute *TableFind(DBTablePartition *rtp, 
                                const DBRequestKey *prefix);

    DISALLOW_COPY_AND_ASSIGN(InetVpnTable);
};

#endif
