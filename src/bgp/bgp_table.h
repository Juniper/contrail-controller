/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_table_h
#define ctrlplane_bgp_table_h

#include <map>
#include <tbb/atomic.h>

#include "base/lifetime.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_update.h"
#include "route/table.h"
#include "bgp/bgp_path.h"
#include "bgp_ribout.h"
#include "db/db_table_walker.h"

class BgpServer;
class BgpRoute;
class BgpPath;
class Path;
class Route;
class RoutingInstance;
class SchedulingGroupManager;
struct UpdateInfo;

class BgpTable : public RouteTable {
public:
    typedef std::map<RibExportPolicy, RibOut *> RibOutMap;

    struct RequestKey : DBRequestKey {
        virtual const IPeer *GetPeer() const = 0;
    };

    struct RequestData : DBRequestData {
        struct NextHop {
            NextHop() : flags_(0), address_(Ip4Address(0)), label_(0) { }
            NextHop(uint32_t flags, IpAddress address, uint32_t label) :
                    flags_(flags), address_(address), label_(label) { }
            uint32_t flags_;
            IpAddress address_;
            uint32_t label_;
            RouteDistinguisher source_rd_;
            ExtCommunity::ExtCommunityList tunnel_encapsulations_;
        };

        typedef std::vector<NextHop> NextHops;

        RequestData(const BgpAttrPtr &attrs, uint32_t flags, uint32_t label)
            : attrs_(attrs) {
            nexthops_.push_back(NextHop(flags,
                                   attrs ? attrs->nexthop() : Ip4Address(0),
                                   label));
        }
        RequestData(const BgpAttrPtr &attrs, NextHops nexthops) :
            attrs_(attrs), nexthops_(nexthops) {
        }

        NextHops &nexthops() { return nexthops_; }
        BgpAttrPtr &attrs() { return attrs_; }
        void set_attrs(BgpAttrPtr attrs) { attrs_ = attrs; }

    private:
        BgpAttrPtr attrs_;
        NextHops nexthops_;
    };

    BgpTable(DB *db, const std::string &name);
    ~BgpTable();

    const RibOutMap &ribout_map() { return ribout_map_; }
    RibOut *RibOutFind(const RibExportPolicy &policy);
    RibOut *RibOutLocate(SchedulingGroupManager *mgr,
                         const RibExportPolicy &policy);
    void RibOutDelete(const RibExportPolicy &policy);

    virtual bool Export(RibOut *ribout, Route *route,
                        const RibPeerSet &peerset,
                        UpdateInfoSList &uinfo_slist) = 0;

    virtual Address::Family family() const = 0;
    virtual bool IsVpnTable() const { return false; }
    virtual std::auto_ptr<DBEntry> AllocEntryStr(const std::string &key) const = 0;

    virtual BgpRoute *RouteReplicate(BgpServer *server, BgpTable *table, 
                                     BgpRoute *src, const BgpPath *path, 
                                     ExtCommunityPtr community) = 0;

    static bool PathSelection(const Path &path1, const Path &path2);
    UpdateInfo *GetUpdateInfo(RibOut *ribout, BgpRoute *route,
                              const RibPeerSet &peerset);

    void ManagedDelete();
    virtual void RetryDelete();
    void Shutdown();
    bool MayDelete() const;
    bool IsDeleted() const { return deleter()->IsDeleted(); }

    RoutingInstance *routing_instance() { return rtinstance_; }
    const RoutingInstance *routing_instance() const { return rtinstance_; }
    virtual void set_routing_instance(RoutingInstance *rtinstance);
    BgpServer *server();

    virtual void Input(DBTablePartition *root, DBClient *client,
                       DBRequest *req);
    void InputCommon(DBTablePartBase *root, BgpRoute *rt, BgpPath *path,
                     const IPeer *peer, DBRequest *req,
                     DBRequest::DBOperation oper, BgpAttrPtr attrs,
                     uint32_t path_id, uint32_t flags, uint32_t label);

    LifetimeActor *deleter();
    const LifetimeActor *deleter() const;
    size_t GetPendingRiboutsCount(size_t &markers);

    void UpdatePathCount(const BgpPath *path, int count);
    const uint64_t GetPrimaryPathCount() const { return primary_path_count_; }
    const uint64_t GetSecondaryPathCount() const {
        return secondary_path_count_;
    }
    const uint64_t GetInfeasiblePathCount() const {
        return infeasible_path_count_;
    }

private:
    class DeleteActor;
    friend class BgpTableTest;
    virtual BgpRoute *TableFind(DBTablePartition *rtp,
            const DBRequestKey *prefix) = 0;
    RoutingInstance *rtinstance_;
    RibOutMap ribout_map_;

    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<BgpTable> instance_delete_ref_;
    tbb::atomic<uint64_t> primary_path_count_;
    tbb::atomic<uint64_t> secondary_path_count_;
    tbb::atomic<uint64_t> infeasible_path_count_;

    DISALLOW_COPY_AND_ASSIGN(BgpTable);
};

#endif
