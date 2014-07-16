/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_rtgroup_mgr_h
#define ctrlplane_rtgroup_mgr_h

#include <boost/ptr_container/ptr_map.hpp>
#include <boost/shared_ptr.hpp>

#include <base/queue_task.h>
#include <base/lifetime.h>

#include <tbb/atomic.h>
#include <tbb/mutex.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>

#include "bgp/bgp_table.h"
#include "bgp/bgp_route.h"
#include "bgp/community.h"
#include "bgp/routing-instance/rtarget_group.h"
#include "bgp/routing-instance/rtarget_group_types.h"
#include "bgp/rtarget/rtarget_address.h"
#include "db/db_table_partition.h"

class BgpServer;
class RTargetRoute;

//
// This keeps track of the RTargetGroupMgr's listener state for a BgpTable.
// The BgpTable could be a VPN table or bgp.rtarget.0.
//
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

//
// The RTargetGroupMgr sets VPNRouteState on dependent VPN BgpRoutes.  This
// VPNRouteState is simply a list of the current RouteTargets that we have
// seen and processed for the BgpRoute. When the RouteTargets for a BgpRoute
// get updated, the VPNRouteState is used to update the appropriate RtGroups
// list of dependent VPN routes.
//
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

//
// The RTargetGroupMgr sets RTargetState on RTargetRoutes. This RTargetState
// is the current InterestedPeerList that we have seen and processed for the
// RTargetRoute.  When a BgpPath is added or deleted for a RTargetRoute the
// RTargetState is used to update the appropriate RtGroups InterestedPeerList.
//
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

//
// This class implements the core logic required to construct and update the
// policy for RouteTarget based constrained distribution of BGP VPN routes.
// This policy is applied when exporting BgpRoutes from VPN tables such as
// bgp.l3vpn.0 and bgp.evpn.0.
//
// The RtGroupMap keeps track of all RtGroups using a map of RouteTarget to
// RtGroup pointers. A RtGroup is created the first time it's needed. There
// are 3 possible triggers:
//
// 1. RoutePathReplicator needs to associate BgpTables with the RouteTarget.
// 2. A VPN BgpRoute with the RouteTarget as one of it's targets is received.
// 3. A RTargetRoute for the RouteTarget is received.
//
// The users of RtGroups invoke RemoveRtGroup when they no longer need it. If
// the RtGroup is eligible to be deleted i.e. it has no state associated with
// it, it gets added to the RtGroupRemoveList.  The list is processed in the
// context of the bgp::RTFilter task.  The RtGroup is deleted if it's still
// eligible for deletion i.e. it hasn't been resurrected after being enqueued.
// Processing RtGroup deletion in this manner avoids race conditions wherein
// one db::DBTable task deletes an RtGroup while another db::DBTable task has
// a pointer to it.
//
// The RTargetGroupMgr needs to register as a listener for all VPN tables and
// for bgp.rtarget.0. It keeps track of it's listener ids for the tables using
// the RtGroupMgrTableStateList.  Note that it does not need to register for
// any VRF tables, only for the VPN tables in the master routing instance and
// bgp.rtarget.0 (which also belongs to the master instance).
//
// The RTargetGroupMgr registers as a listener for all VPN tables so that it
// can maintain the list of the dependent VPN routes for a given RouteTarget.
// The actual dependency is maintained in the associated RtGroup object based
// on APIs invoked from the RTargetGroupMgr.
//
// The RTargetGroupMgr sets VPNRouteState on dependent VPN BgpRoutes.  This
// VPNRouteState is simply a list of the current RouteTargets that we have
// seen and processed for the BgpRoute. When the RouteTargets for a BgpRoute
// get updated, the VPNRouteState is used to update the appropriate RtGroups
// list of dependent VPN routes.
//
// The VPNRouteState and the list of dependent VPN BgpRoutes are updated from
// the context of the db::DBTable task i.e. when processing notification for a
// VPN BgpRoute.
//
// The RTargetGroupMgr also registers as a listener for bgp.rtarget.0 so that
// it can maintain the list of interested peers for a given RouteTarget.  The
// actual dependency is maintained in the associated RtGroup object based on
// APIs invoked from the RTargetGroupMgr.
//
// The RTargetGroupMgr sets RTargetState on RTargetRoutes. This RTargetState
// is the current InterestedPeerList that we have seen and processed for the
// RTargetRoute.  When a BgpPath is added or deleted for a RTargetRoute the
// RTargetState is used to update the appropriate RtGroups InterestedPeerList.
//
// The RTargetState and the InterestedPeerList are not updated directly from
// the context of the db::DBTable task.  Instead, the RTargetRoute is added
// to the RTargetRouteTriggerList. The RTargetRouteTriggerList keeps track of
// RTargetRoutes that need to be processed and is evaluated from context of
// bgp::RTFilter task. This lets us absorb multiple changes to a RTargetRoute
// in one shot.  Since the bgp::RTFilter task is mutually exclusive with the
// db::DBTable task, this also prevents any concurrency issues wherein the
// BgpExport::Export method for the VPN tables accesses the InterestedPeerList
// for an RtGroups while it's being modified on account of changes to the
// RTargetRoute.
//
// When a RTargetRoute in the RTargetRouteTriggerList is processed, we figure
// out if InterestedPeerList has changed.  If so, the RouteTarget in question
// is added to the RouteTargetTriggerList.  The RouteTargetTriggerList keeps
// track of RouteTargets whose dependent BgpRoutes need to be re-evaluated. It
// gets processed in the context of the bgp::RTFilter task.  Since we do not
// allow more than 1 bgp::RTFilter task to run at the same time, this ensures
// that the RouteTargetTriggerList is not modified while it's being processed.
//
// A mutex is used to protect the RtGroupMap since LocateRtGroup/GetRtGroup
// is called from multiple db::DBTable tasks concurrently. The same mutex is
// also used to protect the RtGroupRemoveList as multiple db::DBTable tasks
// can try to add RtGroups to the list concurrently.
//
class RTargetGroupMgr {
public:
    typedef boost::ptr_map<const RouteTarget, RtGroup> RtGroupMap;
    typedef std::map<BgpTable *, 
            RtGroupMgrTableState *> RtGroupMgrTableStateList;
    typedef std::set<RTargetRoute *> RTargetRouteTriggerList;
    typedef std::set<RouteTarget> RouteTargetTriggerList;
    typedef std::set<RtGroup *> RtGroupRemoveList;

    RTargetGroupMgr(BgpServer *);
    virtual ~RTargetGroupMgr();

    // RtGroup
    RtGroup *GetRtGroup(const RouteTarget &rt);
    RtGroup *GetRtGroup(const ExtCommunity::ExtCommunityValue &comm);
    RtGroup *LocateRtGroup(const RouteTarget &rt);
    RtGroupMap &GetRtGroupMap() { return rtgroup_map_; }
    void RemoveRtGroup(const RouteTarget &rt);
    virtual void GetRibOutInterestedPeers(RibOut *ribout, 
             const ExtCommunity *ext_community, 
             const RibPeerSet &peerset, RibPeerSet &new_peerset);
    void Enqueue(RtGroupMgrReq *req);
    void Initialize();
    void ManagedDelete();
    bool IsRTargetRoutesProcessed() { return rtarget_route_list_.empty(); }

private:
    static int rtfilter_task_id_;

    friend class RTargetPeerTest;
    void RTargetDepSync(DBTablePartBase *root, BgpRoute *rt, 
                        DBTableBase::ListenerId id, VpnRouteState *dbstate,
                        VpnRouteState::RTargetList &current);
    void RTargetPeerSync(BgpTable *table, RTargetRoute *rt, 
                         DBTableBase::ListenerId id, RTargetState *dbstate,
                         RtGroup::InterestedPeerList &current);
    void BuildRTargetDistributionGraph(BgpTable *table, RTargetRoute *rt, 
                                       DBTableBase::ListenerId id);
    BgpServer *server() { return server_; }
    bool ProcessRTargetRouteList();
    void TriggerRTGroupDepWalk();
    bool ProcessRouteTargetList(int part_id);
    DBTableBase::ListenerId GetListenerId(BgpTable *table);
    void UnregisterTables();
    bool RemoveRtGroups();
    bool VpnRouteNotify(DBTablePartBase *root, DBEntryBase *entry);
    bool RTargetRouteNotify(DBTablePartBase *root, DBEntryBase *entry);
    bool RequestHandler(RtGroupMgrReq *req);
    void DisableRtargetRouteProcessing() {
        rtarget_route_trigger_->set_disable();
    }
    void EnableRtargetRouteProcessing() {
        rtarget_route_trigger_->set_enable();
    }

    BgpServer *server_;
    tbb::mutex mutex_;
    RtGroupMap rtgroup_map_;
    RtGroupMgrTableStateList table_state_;
    boost::scoped_ptr<TaskTrigger> rtarget_route_trigger_;
    boost::scoped_ptr<TaskTrigger> remove_rtgroup_trigger_;
    std::vector<boost::shared_ptr<TaskTrigger> > rtarget_dep_triggers_;
    RTargetRouteTriggerList rtarget_route_list_;
    tbb::atomic<long> num_dep_rt_triggers_;
    RouteTargetTriggerList rtarget_trigger_list_;
    RouteTargetTriggerList pending_rtarget_trigger_list_;
    RtGroupRemoveList rtgroup_remove_list_;
    WorkQueue<RtGroupMgrReq *> *process_queue_;
    LifetimeRef<RTargetGroupMgr> master_instance_delete_ref_;

    DISALLOW_COPY_AND_ASSIGN(RTargetGroupMgr);
};

#endif
