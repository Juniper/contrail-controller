/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EVPN_EVPN_TABLE_H_
#define SRC_BGP_EVPN_EVPN_TABLE_H_

#include <string>

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
    virtual void AddRemoveCallback(const DBEntryBase *entry, bool add) const;

    virtual Address::Family family() const { return Address::EVPN; }
    bool IsMaster() const;
    virtual bool IsVpnTable() const { return IsMaster(); }

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
    const EvpnManager *GetEvpnManager() const;
    virtual void set_routing_instance(RoutingInstance *rtinstance);

    uint64_t mac_route_count() const { return mac_route_count_; }
    uint64_t unique_mac_route_count() const { return unique_mac_route_count_; }
    uint64_t im_route_count() const { return im_route_count_; }

private:
    virtual BgpRoute *TableFind(DBTablePartition *rtp,
                                const DBRequestKey *prefix);
    EvpnManager *evpn_manager_;
    mutable tbb::atomic<uint64_t> mac_route_count_;
    mutable tbb::atomic<uint64_t> unique_mac_route_count_;
    mutable tbb::atomic<uint64_t> im_route_count_;

    DISALLOW_COPY_AND_ASSIGN(EvpnTable);
};

#endif  // SRC_BGP_EVPN_EVPN_TABLE_H_
