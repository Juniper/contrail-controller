/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_enet_table_h
#define ctrlplane_enet_table_h

#include "bgp/bgp_table.h"
#include "bgp/enet/enet_route.h"

class EnetTable : public BgpTable {
public:
    struct RequestKey : BgpTable::RequestKey {
        RequestKey(const EnetPrefix &prefix, const IPeer *ipeer)
            : prefix(prefix), peer(ipeer) {
        }
        EnetPrefix prefix;
        const IPeer *peer;
        virtual const IPeer *GetPeer() const { return peer; }
    };

    EnetTable(DB *db, const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;
    virtual std::auto_ptr<DBEntry> AllocEntryStr(const std::string &key) const;

    virtual Address::Family family() const { return Address::ENET; }

    virtual size_t Hash(const DBEntry *entry) const;
    virtual size_t Hash(const DBRequestKey *key) const;

    virtual BgpRoute *RouteReplicate(BgpServer *server, BgpTable *src_table,
                                     BgpRoute *src_rt, const BgpPath *path, 
                                     ExtCommunityPtr ptr,
                                     OriginVnPtr origin_vn);


    virtual bool Export(RibOut *ribout, Route *route,
                        const RibPeerSet &peerset,
                        UpdateInfoSList &info_slist);

    static size_t HashFunction(const EnetPrefix &prefix);
    static DBTableBase *CreateTable(DB *db, const std::string &name);

private:
    virtual BgpRoute *TableFind(DBTablePartition *rtp,
                                const DBRequestKey *prefix);

    DISALLOW_COPY_AND_ASSIGN(EnetTable);
};

#endif
