/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_rtgroup_mgr_h
#define ctrlplane_rtgroup_mgr_h

#include <boost/ptr_container/ptr_map.hpp>

#include <base/queue_task.h>
#include <base/lifetime.h>

#include <tbb/mutex.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>

#include "bgp/bgp_table.h"
#include "bgp/bgp_route.h"
#include "bgp/community.h"
#include "bgp/routing-instance/rtarget_group.h"
#include "bgp/rtarget/rtarget_address.h"
#include "db/db_table_partition.h"

#include "bgp/routing-instance/rtarget_group_types.h"

class BgpServer;
class RTargetRoute;


class RtGroupMgrTableState {
public:
    RtGroupMgrTableState(BgpTable *table, DBTableBase::ListenerId id);
    ~RtGroupMgrTableState();

    void ManagedDelete();

    DBTableBase::ListenerId GetListenerId() const {
        return id_;
    }

private:
    DBTableBase::ListenerId id_;
    LifetimeRef<RtGroupMgrTableState> table_delete_ref_;
    DISALLOW_COPY_AND_ASSIGN(RtGroupMgrTableState);
};

class VpnRouteState : public DBState {
public:
    typedef std::set<RouteTarget> RTargetList;
    const RTargetList &GetList() const {
        return list_;
    }

    RTargetList *GetMutableList() {
        return &list_;
    }
private:
    RTargetList list_;
};

class RTargetState : public DBState {
public:
    const RtGroup::InterestedPeerList &GetList() const {
        return list_;
    }

    RtGroup::InterestedPeerList *GetMutableList() {
        return &list_;
    }
private:
    RtGroup::InterestedPeerList list_;
};

struct RtGroupMgrReq {
    enum RequestType {
        SHOW_RTGROUP,
    };

    RtGroupMgrReq(RequestType type, SandeshResponse *resp) 
        : type_(type), snh_resp_(resp) {
    }
    RequestType type_;
    SandeshResponse *snh_resp_;
    DISALLOW_COPY_AND_ASSIGN(RtGroupMgrReq);
};

class RTargetGroupMgr {
public:
    typedef boost::ptr_map<const RouteTarget, RtGroup> RtGroupMap;
    typedef std::map<BgpTable *, 
            RtGroupMgrTableState *> RtGroupMgrTableStateList;
    typedef std::set<RTargetRoute *> RTargetRouteTriggerList;
    typedef std::set<RouteTarget> RouteTargetTriggerList;

    RTargetGroupMgr(BgpServer *);
    virtual ~RTargetGroupMgr();

    // RtGroup
    RtGroup *GetRtGroup(const RouteTarget &rt);
    RtGroup *GetRtGroup(const ExtCommunity::ExtCommunityValue &comm);
    RtGroup *LocateRtGroup(const RouteTarget &rt);
    RtGroupMap &GetRtGroupMap() { return rt_group_map_; }
    void RemoveRtGroup(const RouteTarget &rt);

    void Enqueue(RtGroupMgrReq *req);
private:
    static int rtfilter_task_id_;
    void RTargetDepSync(DBTablePartBase *root, BgpRoute *rt, 
                       DBTableBase::ListenerId id, VpnRouteState *dbstate,
                       VpnRouteState::RTargetList &current);

    void RTargetPeerSync(BgpTable *table, RTargetRoute *rt, 
                       DBTableBase::ListenerId id, RTargetState *dbstate,
                       RtGroup::InterestedPeerList &current);

    void BuildRTargetDistributionGraph(BgpTable *table, RTargetRoute *rt, 
                                       DBTableBase::ListenerId id);

    BgpServer *server() { return server_; }

    void RoutingInstanceCallback(std::string name, int op);

    bool ProcessRTargetRouteList();
    bool ProcessRouteTargetList();
    bool UnregisterTables();

    bool VpnRouteNotify(DBTablePartBase *root,
                        DBEntryBase *entry);

    bool RTargetRouteNotify(DBTablePartBase *root,
                            DBEntryBase *entry);

    bool RequestHandler(RtGroupMgrReq *req);

    DBTableBase::ListenerId GetListenerId(BgpTable *table);

    BgpServer *server_;
    tbb::mutex mutex_;
    RtGroupMap rt_group_map_;
    RtGroupMgrTableStateList table_state_;
    boost::scoped_ptr<TaskTrigger> rtarget_route_trigger_;
    boost::scoped_ptr<TaskTrigger> unreg_trigger_;
    boost::scoped_ptr<TaskTrigger> rtarget_dep_trigger_;
    RTargetRouteTriggerList rtarget_route_list_;
    RouteTargetTriggerList rtarget_trigger_list_;
    WorkQueue<RtGroupMgrReq *> *process_queue_;
    int id_;
};
#endif
