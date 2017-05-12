/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_RTARGET_RTARGET_TABLE_H_
#define SRC_BGP_RTARGET_RTARGET_TABLE_H_

#include <string>

#include "bgp/bgp_attr.h"
#include "bgp/bgp_table.h"
#include "bgp/rtarget/rtarget_address.h"
#include "bgp/rtarget/rtarget_route.h"

class BgpServer;
class BgpRoute;

class RTargetTable : public BgpTable {
public:
    struct RequestKey : BgpTable::RequestKey {
        RequestKey(const RTargetPrefix &prefix, const IPeer *ipeer)
            : prefix(prefix), peer(ipeer) {
        }
        RTargetPrefix prefix;
        const IPeer *peer;
        virtual const IPeer *GetPeer() const { return peer; }
    };

    RTargetTable(DB *db, const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;
    virtual std::auto_ptr<DBEntry> AllocEntryStr(const std::string &key) const;

    virtual Address::Family family() const { return Address::RTARGET; }

    virtual size_t Hash(const DBEntry *entry) const;
    virtual size_t Hash(const DBRequestKey *key) const;

    virtual BgpRoute *RouteReplicate(BgpServer *server, BgpTable *src_table,
                                     BgpRoute *src_rt, const BgpPath *path,
                                     ExtCommunityPtr ptr);

    virtual bool Export(RibOut *ribout, Route *route,
                        const RibPeerSet &peerset,
                        UpdateInfoSList &info_slist);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    time_t last_updated() const { return last_updated_; }

private:
    virtual BgpRoute *TableFind(DBTablePartition *rtp,
                                const DBRequestKey *prefix);
    void AddRemoveCallback(const DBEntryBase *entry, bool add) const;

    mutable tbb::atomic<time_t> last_updated_;

    DISALLOW_COPY_AND_ASSIGN(RTargetTable);
};

#endif  // SRC_BGP_RTARGET_RTARGET_TABLE_H_
