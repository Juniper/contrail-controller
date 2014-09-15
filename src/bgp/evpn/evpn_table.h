/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_evpn_table_h
#define ctrlplane_evpn_table_h

#include "bgp/bgp_table.h"
#include "bgp/evpn/evpn_route.h"

class EvpnManager;

class EvpnTable : public BgpTable {
public:
    struct RequestKey : BgpTable::RequestKey {
        RequestKey(const EvpnPrefix &prefix, const IPeer *ipeer)
            : prefix(prefix), peer(ipeer) {
        }
        EvpnPrefix prefix;
        const IPeer *peer;
        virtual const IPeer *GetPeer() const { return peer; }
    };

    EvpnTable(DB *db, const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;
    virtual std::auto_ptr<DBEntry> AllocEntryStr(const std::string &key) const;

    virtual Address::Family family() const { return Address::EVPN; }
    bool IsDefault() const;
    virtual bool IsVpnTable() const { return IsDefault(); }

    virtual size_t Hash(const DBEntry *entry) const;
    virtual size_t Hash(const DBRequestKey *key) const;

    virtual BgpRoute *RouteReplicate(BgpServer *server, BgpTable *src_table,
                                     BgpRoute *src_rt, const BgpPath *path, 
                                     ExtCommunityPtr ptr);

    virtual bool Export(RibOut *ribout, Route *route,
                        const RibPeerSet &peerset,
                        UpdateInfoSList &info_slist);

    static size_t HashFunction(const EvpnPrefix &prefix);
    static DBTableBase *CreateTable(DB *db, const std::string &name);

    void CreateEvpnManager();
    void DestroyEvpnManager();
    EvpnManager *GetEvpnManager();
    virtual void set_routing_instance(RoutingInstance *rtinstance);

private:
    virtual BgpRoute *TableFind(DBTablePartition *rtp,
                                const DBRequestKey *prefix);
    EvpnManager *evpn_manager_;

    DISALLOW_COPY_AND_ASSIGN(EvpnTable);
};

#endif
