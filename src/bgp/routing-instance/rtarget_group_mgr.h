/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_INSTANCE_RTARGET_GROUP_MGR_H_
#define SRC_BGP_ROUTING_INSTANCE_RTARGET_GROUP_MGR_H_

#include <boost/ptr_container/ptr_map.hpp>
#include <boost/shared_ptr.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>
#include <tbb/atomic.h>
#include <tbb/mutex.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/queue_task.h"
#include "base/lifetime.h"
#include "bgp/community.h"
#include "bgp/routing-instance/rtarget_group.h"
#include "bgp/rtarget/rtarget_address.h"
#include "db/db_table_partition.h"

class BgpRoute;
class BgpServer;
class BgpTable;
class RibOut;
class RibPeerSet;
class RTargetRoute;
class RTargetGroupMgr;
class TaskTrigger;

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

    void AddRouteTarget(RTargetGroupMgr *mgr, int part_id, BgpRoute *rt,
        RTargetList::const_iterator it);
    void DeleteRouteTarget(RTargetGroupMgr *mgr, int part_id, BgpRoute *rt,
        RTargetList::const_iterator it);

private:
    friend class RTargetGroupMgr;

    const RTargetList *GetList() const { return &list_; }
    RTargetList *GetMutableList() { return &list_; }

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
    void AddInterestedPeer(RTargetGroupMgr *mgr, RtGroup *rtgroup,
        RTargetRoute *rt, RtGroup::InterestedPeerList::const_iterator it);
    void DeleteInterestedPeer(RTargetGroupMgr *mgr, RtGroup *rtgroup,
        RTargetRoute *rt, RtGroup::InterestedPeerList::iterator it);

private:
    friend class RTargetGroupMgr;

    const RtGroup::InterestedPeerList *GetList() const { return &list_; }
    RtGroup::InterestedPeerList *GetMutableList() { return &list_; }

    RtGroup::InterestedPeerList list_;
};

struct RtGroupMgrReq {
    enum RequestType {
        SHOW_RTGROUP,
        SHOW_RTGROUP_PEER,
        SHOW_RTGROUP_SUMMARY,
    };

    RtGroupMgrReq(RequestType type, SandeshResponse *resp,
        const std::string &param = std::string())
        : type_(type), snh_resp_(resp), param_(param) {
    }

    RequestType type_;
    SandeshResponse *snh_resp_;
    std::string param_;

private:
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
// A mutex is used to protect the RtGroupMap since LocateRtGroup/GetRtGroup
// is called from multiple db::DBTable tasks concurrently. The same mutex is
// also used to protect the RtGroupRemoveList as multiple db::DBTable tasks
// can try to add RtGroups to the list concurrently.
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
// is added to all the RouteTargetTriggerLists, one per DBTable partition. The
// RouteTargetTriggerList keeps track of RouteTargets whose dependent BgpRoutes
// need to be re-evaluated.  It gets processed in the context of db::DBTable
// task. All RouteTargetTriggerLists can be processed concurrently since they
// work on different partitions.  As db::DBTable tasks are mutually exclusive
// with the bgp::RTFilter task, it is guaranteed that a RouteTargetTriggerList
// does not get modified while it's being processed.
//
class RTargetGroupMgr {
public:
    typedef boost::ptr_map<const RouteTarget, RtGroup> RtGroupMap;
    typedef RtGroupMap::const_iterator const_iterator;

    explicit RTargetGroupMgr(BgpServer *server);
    virtual ~RTargetGroupMgr();

    bool empty() const { return rtgroup_map_.empty(); }
    const_iterator begin() const { return rtgroup_map_.begin(); }
    const_iterator end() const { return rtgroup_map_.end(); }
    const_iterator lower_bound(const RouteTarget &rt) const {
        return rtgroup_map_.lower_bound(rt);
    }

    // RtGroup
    RtGroup *GetRtGroup(const RouteTarget &rt);
    RtGroup *GetRtGroup(const ExtCommunity::ExtCommunityValue &comm);
    RtGroup *LocateRtGroup(const RouteTarget &rt);
    void NotifyRtGroup(const RouteTarget &rt);
    void RemoveRtGroup(const RouteTarget &rt);

    virtual void GetRibOutInterestedPeers(RibOut *ribout,
             const ExtCommunity *ext_community,
             const RibPeerSet &peerset, RibPeerSet *new_peerset);
    void Enqueue(RtGroupMgrReq *req);
    void Initialize();
    void ManagedDelete();
    bool IsRTargetRoutesProcessed() const {
        return rtarget_route_list_.empty();
    }

private:
    friend class BgpXmppRTargetTest;
    friend class ReplicationTest;

    typedef std::map<BgpTable *,
            RtGroupMgrTableState *> RtGroupMgrTableStateList;
    typedef std::set<RTargetRoute *> RTargetRouteTriggerList;
    typedef std::set<RouteTarget> RouteTargetTriggerList;
    typedef std::set<RtGroup *> RtGroupRemoveList;

    void RTargetDepSync(DBTablePartBase *root, BgpRoute *rt,
                        DBTableBase::ListenerId id, VpnRouteState *dbstate,
                        const VpnRouteState::RTargetList *future);
    void RTargetPeerSync(BgpTable *table, RTargetRoute *rt,
                         DBTableBase::ListenerId id, RTargetState *dbstate,
                         const RtGroup::InterestedPeerList *future);
    void BuildRTargetDistributionGraph(BgpTable *table, RTargetRoute *rt,
                                       DBTableBase::ListenerId id);
    BgpServer *server() { return server_; }

    bool ProcessRTargetRouteList();
    void DisableRTargetRouteProcessing();
    void EnableRTargetRouteProcessing();
    bool IsRTargetRouteOnList(RTargetRoute *rt) const;

    bool ProcessRouteTargetList(int part_id);
    void AddRouteTargetToLists(const RouteTarget &rtarget);
    void DisableRouteTargetProcessing();
    void EnableRouteTargetProcessing();
    bool IsRouteTargetOnList(const RouteTarget &rtarget) const;

    bool ProcessRtGroupList();
    void DisableRtGroupProcessing();
    void EnableRtGroupProcessing();
    bool IsRtGroupOnList(RtGroup *rtgroup) const;

    DBTableBase::ListenerId GetListenerId(BgpTable *table);
    void UnregisterTables();
    bool VpnRouteNotify(DBTablePartBase *root, DBEntryBase *entry);
    bool RTargetRouteNotify(DBTablePartBase *root, DBEntryBase *entry);
    bool RequestHandler(RtGroupMgrReq *req);

    BgpServer *server_;
    tbb::mutex mutex_;
    RtGroupMap rtgroup_map_;
    RtGroupMgrTableStateList table_state_;
    boost::scoped_ptr<TaskTrigger> rtarget_route_trigger_;
    boost::scoped_ptr<TaskTrigger> remove_rtgroup_trigger_;
    std::vector<boost::shared_ptr<TaskTrigger> > rtarget_dep_triggers_;
    RTargetRouteTriggerList rtarget_route_list_;
    std::vector<RouteTargetTriggerList> rtarget_trigger_lists_;
    RtGroupRemoveList rtgroup_remove_list_;
    LifetimeRef<RTargetGroupMgr> master_instance_delete_ref_;

    DISALLOW_COPY_AND_ASSIGN(RTargetGroupMgr);
};

#endif  // SRC_BGP_ROUTING_INSTANCE_RTARGET_GROUP_MGR_H_
