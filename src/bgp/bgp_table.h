/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_TABLE_H_
#define SRC_BGP_BGP_TABLE_H_

#include <tbb/atomic.h>

#include <map>
#include <string>
#include <vector>

#include "base/lifetime.h"
#include "bgp/bgp_rib_policy.h"
#include "db/db_table_walker.h"
#include "route/table.h"

class BgpServer;
class BgpRoute;
class BgpPath;
class IPeer;
class Path;
class PathResolver;
class RibOut;
class RibPeerSet;
class Route;
class RoutingInstance;
class SchedulingGroupManager;
class UpdateInfoSList;
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

        RequestData(const BgpAttrPtr &attrs, uint32_t flags, uint32_t label,
                    uint64_t subscription_gen_id = 0)
            : attrs_(attrs), subscription_gen_id_(subscription_gen_id) {
            nexthops_.push_back(NextHop(flags,
                                   attrs ? attrs->nexthop() : Ip4Address(0),
                                   label));
        }

        RequestData(const BgpAttrPtr &attrs, NextHops nexthops,
                    uint64_t subscription_gen_id = 0) :
            attrs_(attrs), nexthops_(nexthops),
            subscription_gen_id_(subscription_gen_id) {
        }

        NextHops &nexthops() { return nexthops_; }
        BgpAttrPtr &attrs() { return attrs_; }
        void set_attrs(BgpAttrPtr attrs) { attrs_ = attrs; }
        void set_subscription_gen_id(uint64_t subscription_gen_id) {
            subscription_gen_id_ = subscription_gen_id;
        }
        uint64_t subscription_gen_id() const { return subscription_gen_id_; }

    private:
        BgpAttrPtr attrs_;
        NextHops nexthops_;
        uint64_t subscription_gen_id_;
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
    virtual bool IsRoutingPolicySupported() const { return false; }
    virtual bool IsRouteAggregationSupported() const { return false; }
    virtual std::auto_ptr<DBEntry> AllocEntryStr(
        const std::string &key) const = 0;

    virtual BgpRoute *RouteReplicate(BgpServer *server, BgpTable *table,
                                     BgpRoute *src, const BgpPath *path,
                                     ExtCommunityPtr community) = 0;

    static bool PathSelection(const Path &path1, const Path &path2);
    UpdateInfo *GetUpdateInfo(RibOut *ribout, BgpRoute *route,
                              const RibPeerSet &peerset);

    void ManagedDelete();
    virtual void RetryDelete();
    void Shutdown();
    virtual bool MayDelete() const;
    bool IsDeleted() const { return deleter()->IsDeleted(); }
    virtual PathResolver *CreatePathResolver();
    void DestroyPathResolver();

    RoutingInstance *routing_instance() { return rtinstance_; }
    const RoutingInstance *routing_instance() const { return rtinstance_; }
    virtual void set_routing_instance(RoutingInstance *rtinstance);
    BgpServer *server();
    const BgpServer *server() const;
    PathResolver *path_resolver() { return path_resolver_; }
    const PathResolver *path_resolver() const { return path_resolver_; }

    virtual void Input(DBTablePartition *root, DBClient *client,
                       DBRequest *req);
    void Input(DBTablePartBase *root, IPeer *peer, BgpRoute *rt,
               int action_mask);

    LifetimeActor *deleter();
    const LifetimeActor *deleter() const;
    size_t GetPendingRiboutsCount(size_t *markers) const;

    void UpdatePathCount(const BgpPath *path, int count);
    const uint64_t GetPrimaryPathCount() const { return primary_path_count_; }
    const uint64_t GetSecondaryPathCount() const {
        return secondary_path_count_;
    }
    const uint64_t GetInfeasiblePathCount() const {
        return infeasible_path_count_;
    }

    // Check whether the route is aggregate route
    bool IsAggregateRoute(const BgpRoute *route) const;

    // Check whether the route is contributing route to aggregate route
    bool IsContributingRoute(const BgpRoute *route) const;

private:
    void ProcessLlgrState(const RibOut *ribout, const BgpPath *path,
                          BgpAttr *attr);
    bool InputCommon(DBTablePartBase *root, BgpRoute *rt, BgpPath *path,
                     const IPeer *peer, DBRequest *req,
                     DBRequest::DBOperation oper, BgpAttrPtr attrs,
                     uint32_t path_id, uint32_t flags, uint32_t label);
    void InputCommonPostProcess(DBTablePartBase *root, BgpRoute *rt,
                                bool notify_rt);

    class DeleteActor;
    friend class BgpTableTest;
    virtual BgpRoute *TableFind(DBTablePartition *rtp,
            const DBRequestKey *prefix) = 0;
    RoutingInstance *rtinstance_;
    PathResolver *path_resolver_;
    RibOutMap ribout_map_;

    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<BgpTable> instance_delete_ref_;
    tbb::atomic<uint64_t> primary_path_count_;
    tbb::atomic<uint64_t> secondary_path_count_;
    tbb::atomic<uint64_t> infeasible_path_count_;

    DISALLOW_COPY_AND_ASSIGN(BgpTable);
};

#endif  // SRC_BGP_BGP_TABLE_H_
