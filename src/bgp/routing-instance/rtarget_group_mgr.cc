/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include "bgp/routing-instance/rtarget_group_mgr.h"

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include "base/task.h"
#include "base/task_annotations.h"

#include "bgp/bgp_config.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/routing-instance/routepath_replicator.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/rtarget_group.h"
#include "bgp/rtarget/rtarget_address.h"
#include "bgp/rtarget/rtarget_route.h"

int RTargetGroupMgr::rtfilter_task_id_ = -1;

RTargetGroupMgr::RTargetGroupMgr(BgpServer *server) : server_(server), 
    rtarget_route_trigger_(new TaskTrigger(
           boost::bind(&RTargetGroupMgr::ProcessRTargetRouteList, this),
           TaskScheduler::GetInstance()->GetTaskId("bgp::RTFilter"), 0)),
    remove_rtgroup_trigger_(new TaskTrigger(
           boost::bind(&RTargetGroupMgr::ProcessRtGroupList, this),
           TaskScheduler::GetInstance()->GetTaskId("bgp::RTFilter"), 0)),
    rtarget_trigger_lists_(DB::PartitionCount()),
    master_instance_delete_ref_(this, NULL) {

    if (rtfilter_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        rtfilter_task_id_ = scheduler->GetTaskId("bgp::RTFilter");
    }

   process_queue_ = 
        new WorkQueue<RtGroupMgrReq *>(rtfilter_task_id_, 0, 
             boost::bind(&RTargetGroupMgr::RequestHandler, this, _1));

    for (int i = 0; i < DB::PartitionCount(); i++) {
        rtarget_dep_triggers_.push_back(boost::shared_ptr<TaskTrigger>(new 
               TaskTrigger(boost::bind(&RTargetGroupMgr::ProcessRouteTargetList, 
                                       this, i), 
               TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), i)));
    }
}

bool RTargetGroupMgr::RequestHandler(RtGroupMgrReq *req) {
    CHECK_CONCURRENCY("bgp::RTFilter");

    switch (req->type_) {
        case RtGroupMgrReq::SHOW_RTGROUP: {
            ShowRtGroupResp *resp = 
                static_cast<ShowRtGroupResp *>(req->snh_resp_);
            std::vector<ShowRtGroupInfo> rtgroup_info_list;
            for (RtGroupMap::iterator it = rtgroup_map_.begin(); 
                 it != rtgroup_map_.end(); it++) {
                ShowRtGroupInfo info;
                RtGroup *rtgroup = it->second;
                info.set_rtarget(rtgroup->rt().ToString());
                std::vector<MemberTableList> export_members;
                BOOST_FOREACH(const RtGroup::RtGroupMembers::value_type &tables,
                              rtgroup->GetExportMembers()) {
                    MemberTableList member;
                    std::vector<std::string> export_tables;
                    BOOST_FOREACH(BgpTable *table, tables.second)
                        export_tables.push_back(table->name());
                    member.set_family(
                                      Address::FamilyToString(tables.first));
                    member.set_tables(export_tables);
                    export_members.push_back(member);
                }
                info.set_export_members(export_members);

                std::vector<MemberTableList> import_members;
                BOOST_FOREACH(const RtGroup::RtGroupMembers::value_type &tables,
                              rtgroup->GetImportMembers()) {
                    MemberTableList member;
                    std::vector<std::string> import_tables;
                    BOOST_FOREACH(BgpTable *table, tables.second)
                        import_tables.push_back(table->name());
                    member.set_tables(import_tables);
                    member.set_family(
                                      Address::FamilyToString(tables.first));
                    import_members.push_back(member);
                }
                info.set_import_members(import_members);

                const RtGroup::RTargetDepRouteList &dep_rt_list = 
                    rtgroup->DepRouteList();
                std::vector<std::string> rtlist;
                for (RtGroup::RTargetDepRouteList::const_iterator dep_it = 
                     dep_rt_list.begin(); dep_it != dep_rt_list.end(); 
                     dep_it++) {
                    for (RtGroup::RouteList::const_iterator dep_rt_it = 
                         dep_it->begin(); dep_rt_it != dep_it->end(); 
                         dep_rt_it++) {
                        rtlist.push_back((*dep_rt_it)->ToString());
                    }
                }
                info.set_dep_route(rtlist);

                std::vector<std::string> interested_peers;
                BOOST_FOREACH(const RtGroup::InterestedPeerList::value_type 
                              &peers, rtgroup->PeerList())
                    interested_peers.push_back(peers.first->peer_name());
                info.set_peers_interested(interested_peers);
                rtgroup_info_list.push_back(info);
            }
            resp->set_rtgroup_list(rtgroup_info_list);

            resp->Response();
            break;
        }
    }
    delete req;
    return true;
}

void RTargetGroupMgr::Enqueue(RtGroupMgrReq *req) { 
    process_queue_->Enqueue(req); 
}

void ShowRtGroupReq::HandleRequest() const {
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(client_context());
    RTargetGroupMgr *mgr =  bsc->bgp_server->rtarget_group_mgr();
    ShowRtGroupResp *resp = new ShowRtGroupResp;
    resp->set_context(context());
    RtGroupMgrReq  *req = 
        new RtGroupMgrReq(RtGroupMgrReq::SHOW_RTGROUP, resp);
    mgr->Enqueue(req);
}

void RTargetGroupMgr::RTargetPeerSync(BgpTable *table, RTargetRoute *rt, 
                                      DBTableBase::ListenerId id, 
                                      RTargetState *dbstate,
                                      RtGroup::InterestedPeerList &current) {
    CHECK_CONCURRENCY("bgp::RTFilter");

    std::set<BgpPeer *> impacted_peers;
    RouteTarget rtarget = rt->GetPrefix().rtarget();
    RtGroup *rtgroup = LocateRtGroup(rtarget);
    if (!rtgroup) return;

    RtGroup::InterestedPeerList::iterator cur_it = current.begin();
    RtGroup::InterestedPeerList::iterator dbstate_next_it, dbstate_it;
    dbstate_it = dbstate_next_it = dbstate->GetMutableList()->begin();
    std::pair<RtGroup::InterestedPeerList::iterator, bool> r;

    while (cur_it != current.end() && 
           dbstate_it != dbstate->GetMutableList()->end()) {
        if (*cur_it < *dbstate_it) {
            // Add to DBState
            r = dbstate->GetMutableList()->insert(*cur_it);
            assert(r.second);
            // Add Peer to RtGroup
            rtgroup->AddInterestedPeer(cur_it->first, rt);
            AddRouteTargetToLists(rtarget);
            impacted_peers.insert(const_cast<BgpPeer *>(cur_it->first));
            cur_it++;
        } else if (*cur_it > *dbstate_it) {
            dbstate_next_it++;
            // Remove the Peer from RtGroup
            rtgroup->RemoveInterestedPeer(dbstate_it->first, rt);
            impacted_peers.insert(const_cast<BgpPeer *>(cur_it->first));
            AddRouteTargetToLists(rtarget);
            // Remove from DBstate
            dbstate->GetMutableList()->erase(dbstate_it);
            dbstate_it = dbstate_next_it;
        } else {
            // Update
            cur_it++;
            dbstate_it++;
        }
        dbstate_next_it = dbstate_it;
    }
    for (; cur_it != current.end(); ++cur_it) {
        r = dbstate->GetMutableList()->insert(*cur_it);
        // Add route to rtarget to route dep tree
        rtgroup->AddInterestedPeer(cur_it->first, rt);
        impacted_peers.insert(const_cast<BgpPeer *>(cur_it->first));
        AddRouteTargetToLists(rtarget);
        assert(r.second);
    }
    for (dbstate_next_it = dbstate_it; 
         dbstate_it != dbstate->GetMutableList()->end(); 
         dbstate_it = dbstate_next_it) {
        dbstate_next_it++;
        // Remove the route from rtarget to route dep tree
        rtgroup->RemoveInterestedPeer(dbstate_it->first, rt);
        impacted_peers.insert(const_cast<BgpPeer *>(dbstate_it->first));
        AddRouteTargetToLists(rtarget);
        dbstate->GetMutableList()->erase(dbstate_it);
    }

    if (dbstate->GetList().empty()) {
        rt->ClearState(table, id);
        delete dbstate;
        RemoveRtGroup(rtarget);
    }
}

void RTargetGroupMgr::BuildRTargetDistributionGraph(BgpTable *table, 
                                RTargetRoute *rt, DBTableBase::ListenerId id) {
    CHECK_CONCURRENCY("bgp::RTFilter");

    RTargetState *dbstate = 
        static_cast<RTargetState *>(rt->GetState(table, id));

    RtGroup::InterestedPeerList peer_list;

    if (rt->IsDeleted() || !rt->BestPath() ||
        !rt->BestPath()->IsFeasible()) {
        RTargetPeerSync(table, rt, id, dbstate, peer_list);
        return;
    }

    for (Route::PathList::iterator it = rt->GetPathList().begin(); 
         it != rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        if (!path->IsFeasible()) break;
        if (path->GetPeer()->IsXmppPeer()) continue;
        const BgpPeer *peer = static_cast<const BgpPeer *>(path->GetPeer());
        BgpProto::BgpPeerType peer_type = peer->PeerType();

        std::pair<RtGroup::InterestedPeerList::iterator,bool> ret = 
            peer_list.insert(std::pair<const BgpPeer *, 
                 RtGroup::RTargetRouteList>(peer, RtGroup::RTargetRouteList()));
        assert(ret.second);
        ret.first->second.insert(rt);
        if (peer_type == BgpProto::EBGP) {
            break;
        }
    }

    RTargetPeerSync(table, rt, id, dbstate, peer_list);
}

bool RTargetGroupMgr::ProcessRouteTargetList(int part_id) {
    CHECK_CONCURRENCY("db::DBTable");

    BOOST_FOREACH(const RouteTarget &rtarget, rtarget_trigger_lists_[part_id]) {
        RtGroup *rtgroup = GetRtGroup(rtarget);
        if (!rtgroup)
            continue;
        const RtGroup::RTargetDepRouteList &dep_rt_list =
            rtgroup->DepRouteList();
        BOOST_FOREACH(BgpRoute *route, dep_rt_list[part_id]) {
            DBTablePartBase *dbpart = route->get_table_partition();
            dbpart->Notify(route);
        }
    }

    rtarget_trigger_lists_[part_id].clear();
    return true;
}

void RTargetGroupMgr::AddRouteTargetToLists(const RouteTarget &rtarget) {
    for (int idx = 0; idx < DB::PartitionCount(); ++idx) {
        rtarget_trigger_lists_[idx].insert(rtarget);
        rtarget_dep_triggers_[idx]->Set();
    }
}

void RTargetGroupMgr::DisableRouteTargetProcessing() {
    for (int idx = 0; idx < DB::PartitionCount(); ++idx) {
        rtarget_dep_triggers_[idx]->set_disable();
    }
}

void RTargetGroupMgr::EnableRouteTargetProcessing() {
    for (int idx = 0; idx < DB::PartitionCount(); ++idx) {
        rtarget_dep_triggers_[idx]->set_enable();
    }
}

bool RTargetGroupMgr::IsRouteTargetOnList(const RouteTarget &rtarget) const {
    for (int idx = 0; idx < DB::PartitionCount(); ++idx) {
        if (rtarget_trigger_lists_[idx].find(rtarget) !=
            rtarget_trigger_lists_[idx].end()) {
            return true;
        }
    }
    return false;
}

bool RTargetGroupMgr::ProcessRTargetRouteList() {
    CHECK_CONCURRENCY("bgp::RTFilter");

    RoutingInstanceMgr *mgr = server()->routing_instance_mgr();
    RoutingInstance *master = 
        mgr->GetRoutingInstance(BgpConfigManager::kMasterInstance);
    BgpTable *table = master->GetTable(Address::RTARGET);

    // Get the Listener id
    DBTableBase::ListenerId id = GetListenerId(table);

    for (RTargetRouteTriggerList::iterator it = rtarget_route_list_.begin(); 
         it != rtarget_route_list_.end(); it++) {
        BuildRTargetDistributionGraph(table, *it, id);
    }

    rtarget_route_list_.clear();
    return true;
}

void RTargetGroupMgr::DisableRTargetRouteProcessing() {
    rtarget_route_trigger_->set_disable();
}

void RTargetGroupMgr::EnableRTargetRouteProcessing() {
    rtarget_route_trigger_->set_enable();
}

bool RTargetGroupMgr::IsRTargetRouteOnList(RTargetRoute *rt) const {
    return rtarget_route_list_.find(rt) != rtarget_route_list_.end();
}

void RTargetGroupMgr::Initialize() {
    RoutingInstanceMgr *mgr = server()->routing_instance_mgr();
    RoutingInstance *master =
        mgr->GetRoutingInstance(BgpConfigManager::kMasterInstance);
    assert(master);

    master_instance_delete_ref_.Reset(master->deleter());

    RoutingInstance::RouteTableList const table_list = master->GetTables();
    DBTableBase::ListenerId id;
    RtGroupMgrTableState *ts = NULL;
    for (RoutingInstance::RouteTableList::const_iterator it = table_list.begin();
         it != table_list.end(); ++it) {
        if (!it->second->IsVpnTable()) continue;

        BgpTable *vpntable = it->second;
        id = vpntable->Register(boost::bind(&RTargetGroupMgr::VpnRouteNotify, 
                                            this, _1, _2));
        ts = new RtGroupMgrTableState(vpntable, id);
        table_state_.insert(std::make_pair(vpntable, ts));
    }

    BgpTable *rttable = master->GetTable(Address::RTARGET);
    id = rttable->Register(boost::bind(&RTargetGroupMgr::RTargetRouteNotify, 
                                      this, _1, _2));
    ts = new RtGroupMgrTableState(rttable, id);
    table_state_.insert(std::make_pair(rttable, ts));
}

void RTargetGroupMgr::ManagedDelete() {
    if (rtgroup_map_.empty()) remove_rtgroup_trigger_->Set();
}

void 
RTargetGroupMgr::RTargetDepSync(DBTablePartBase *root, BgpRoute *rt, 
                                DBTableBase::ListenerId id,
                                VpnRouteState *dbstate,
                                VpnRouteState::RTargetList &current) {
    CHECK_CONCURRENCY("db::DBTable");

    VpnRouteState::RTargetList::iterator cur_it = current.begin();
    VpnRouteState::RTargetList::iterator dbstate_next_it, dbstate_it;
    BgpTable *table = static_cast<BgpTable *>(root->parent());

    if (dbstate == NULL) {
        dbstate = new VpnRouteState();
        rt->SetState(table, id, dbstate);
    }
    dbstate_it = dbstate_next_it = dbstate->GetMutableList()->begin();
    std::pair<VpnRouteState::RTargetList::iterator, bool> r;

    while (cur_it != current.end() && 
           dbstate_it != dbstate->GetMutableList()->end()) {
        if (*cur_it < *dbstate_it) {
            // Add RTarget to DBState
            r = dbstate->GetMutableList()->insert(*cur_it);
            assert(r.second);
            // Add route to rtarget to route dep tree
            RtGroup *rtgroup = LocateRtGroup(*cur_it);
            rtgroup->AddDepRoute(root->index(), rt);
            cur_it++;
        } else if (*cur_it > *dbstate_it) {
            // Remove from DBstate
            dbstate_next_it++;
            // Remove the route from rtarget to route dep tree
            RtGroup *rtgroup = GetRtGroup(*dbstate_it);
            rtgroup->RemoveDepRoute(root->index(), rt);
            RemoveRtGroup(*dbstate_it);
            dbstate->GetMutableList()->erase(dbstate_it);
            dbstate_it = dbstate_next_it;
        } else {
            // Update
            cur_it++;
            dbstate_it++;
        }
        dbstate_next_it = dbstate_it;
    }
    for (; cur_it != current.end(); ++cur_it) {
        r = dbstate->GetMutableList()->insert(*cur_it);
        assert(r.second);
        // Add route to rtarget to route dep tree
        RtGroup *rtgroup = LocateRtGroup(*cur_it);
        rtgroup->AddDepRoute(root->index(), rt);
    }
    for (dbstate_next_it = dbstate_it; 
         dbstate_it != dbstate->GetMutableList()->end(); 
         dbstate_it = dbstate_next_it) {
        dbstate_next_it++;
        // Remove the route from rtarget to route dep tree
        RtGroup *rtgroup = GetRtGroup(*dbstate_it);
        rtgroup->RemoveDepRoute(root->index(), rt);
        RemoveRtGroup(*dbstate_it);
        dbstate->GetMutableList()->erase(dbstate_it);
    }

    if (dbstate->GetList().empty()) {
        rt->ClearState(root->parent(), id);
        delete dbstate;
    }
}

DBTableBase::ListenerId RTargetGroupMgr::GetListenerId(BgpTable *table) {
    RtGroupMgrTableStateList::iterator loc = table_state_.find(table);
    assert(loc != table_state_.end());
    RtGroupMgrTableState *ts = loc->second;
    DBTableBase::ListenerId id = ts->GetListenerId();
    assert(id != DBTableBase::kInvalidId);
    return id;
}

bool RTargetGroupMgr::VpnRouteNotify(DBTablePartBase *root,
                                     DBEntryBase *entry) {
    CHECK_CONCURRENCY("db::DBTable");

    BgpTable *table = static_cast<BgpTable *>(root->parent());
    BgpRoute *rt = static_cast<BgpRoute *>(entry);
    // Get the Listener id
    DBTableBase::ListenerId id = GetListenerId(table);

    // Get the dbstate
    VpnRouteState *dbstate = 
        static_cast<VpnRouteState *>(rt->GetState(table, id));

    VpnRouteState::RTargetList list;

    if (entry->IsDeleted() || !rt->BestPath() ||
        !rt->BestPath()->IsFeasible()) {
        if (!dbstate)
            return true;
        RTargetDepSync(root, rt, id, dbstate, list);
        return true;
    }

    const BgpPath *path = rt->BestPath();
    const BgpAttr *attr = path->GetAttr();
    const ExtCommunity *ext_community = attr->ext_community();

    if (ext_community) {
        // Gather all Route Target
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm, 
                      ext_community->communities()) {
            if (ExtCommunity::is_route_target(comm)) {
                list.insert(RouteTarget(comm));
            }
        }
    }

    RTargetDepSync(root, rt, id, dbstate, list);
    return true;
}

bool RTargetGroupMgr::RTargetRouteNotify(DBTablePartBase *root,
                                         DBEntryBase *entry) {
    CHECK_CONCURRENCY("db::DBTable");

    BgpTable *table = static_cast<BgpTable *>(root->parent());
    RTargetRoute *rt = static_cast<RTargetRoute *>(entry);
    // Get the Listener id
    DBTableBase::ListenerId id = GetListenerId(table);

    // Get the dbstate
    RTargetState *dbstate = 
        static_cast<RTargetState *>(rt->GetState(table, id));

    if (!dbstate) {
        if (rt->IsDeleted()) return true;
        dbstate = new RTargetState();
        rt->SetState(table, id, dbstate);
    }
    if (rtarget_route_list_.empty())
        rtarget_route_trigger_->Set();
    rtarget_route_list_.insert(rt);
    return true;
}

RTargetGroupMgr::~RTargetGroupMgr() {
    delete process_queue_;
    assert(rtgroup_map_.empty());
}

// Search a RtGroup
RtGroup *RTargetGroupMgr::GetRtGroup(const RouteTarget &rt) {
    tbb::mutex::scoped_lock lock(mutex_);
    RtGroupMap::iterator loc = rtgroup_map_.find(rt);
    if (loc != rtgroup_map_.end()) {
        return loc->second;
    }
    return NULL;
}

// Search a RtGroup
RtGroup *RTargetGroupMgr::GetRtGroup(const ExtCommunity::ExtCommunityValue
                                        &community) {
    RouteTarget rt(community);
    return GetRtGroup(rt);
}

RtGroup *RTargetGroupMgr::LocateRtGroup(const RouteTarget &rt) {
    tbb::mutex::scoped_lock lock(mutex_);
    RtGroupMap::iterator loc = rtgroup_map_.find(rt);
    RtGroup *group = (loc != rtgroup_map_.end()) ? loc->second : NULL;
    if (group == NULL) {
        group = new RtGroup(rt);
        rtgroup_map_.insert(rt, group);
    }
    return group;
}

void RTargetGroupMgr::RemoveRtGroup(const RouteTarget &rt) {
    tbb::mutex::scoped_lock lock(mutex_);
    RtGroupMap::iterator loc = rtgroup_map_.find(rt);
    RtGroup *rtgroup = (loc != rtgroup_map_.end()) ? loc->second : NULL;
    assert(rtgroup);

    rtgroup_remove_list_.insert(rtgroup);
    remove_rtgroup_trigger_->Set();
}

void RTargetGroupMgr::GetRibOutInterestedPeers(RibOut *ribout, 
             const ExtCommunity *ext_community, 
             const RibPeerSet &peerset, RibPeerSet &new_peerset) {
    RtGroupInterestedPeerSet peer_set; 
    RtGroup *null_rtgroup = GetRtGroup(RouteTarget::null_rtarget);
    if (null_rtgroup) peer_set = null_rtgroup->GetInterestedPeers();
    BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm, 
                  ext_community->communities()) {
        if (ExtCommunity::is_route_target(comm)) {
            RtGroup *rtgroup = GetRtGroup(comm);
            if (!rtgroup) continue;
            peer_set |= rtgroup->GetInterestedPeers();
        }
    }
    RibOut::PeerIterator iter(ribout, peerset);
    while (iter.HasNext()) {
        int current_index = iter.index();
        IPeerUpdate *peer = iter.Next();
        BgpPeer *tmp = dynamic_cast<BgpPeer *>(peer);
        assert(tmp);
        if (tmp->IsFamilyNegotiated(Address::RTARGET)) {
            if (!peer_set.test(tmp->GetIndex())) {
                new_peerset.reset(current_index);
            }
        }
    }
}

void RTargetGroupMgr::UnregisterTables() {
    CHECK_CONCURRENCY("bgp::RTFilter");

    if (rtgroup_map_.empty()) {
        RoutingInstanceMgr *mgr = server()->routing_instance_mgr();
        RoutingInstance *master = 
            mgr->GetRoutingInstance(BgpConfigManager::kMasterInstance);
        if (master && master->deleted()) {
            for (RtGroupMgrTableStateList::iterator it = 
                 table_state_.begin(), itnext; it != table_state_.end(); 
                 it = itnext) {
                itnext = it;
                itnext++;
                RtGroupMgrTableState *ts = it->second;
                DBTableBase::ListenerId id = ts->GetListenerId();
                BgpTable *bgptable = it->first;
                bgptable->Unregister(id);
                table_state_.erase(it);
                delete ts;
            }
            master_instance_delete_ref_.Reset(NULL);
        }
    }
}

bool RTargetGroupMgr::ProcessRtGroupList() {
    CHECK_CONCURRENCY("bgp::RTFilter");
    BOOST_FOREACH(RtGroup *rtgroup, rtgroup_remove_list_) {
        bool members_empty = true;
        BOOST_FOREACH(const RtGroup::RtGroupMembers::value_type &family_members, 
                      rtgroup->GetImportMembers()) {
            if (!family_members.second.empty()) {
                members_empty = false;
                break;
            }
        }
        if (!members_empty) continue;

        members_empty = true;
        BOOST_FOREACH(const RtGroup::RtGroupMembers::value_type &family_members, 
                      rtgroup->GetExportMembers()) {
            if (!family_members.second.empty()) {
                members_empty = false;
                break;
            }
        }
        if (!members_empty) continue;

        if (rtgroup->RouteDepListEmpty() && rtgroup->peer_list_empty()) {
            RouteTarget rt = rtgroup->rt();
            rtgroup_map_.erase(rt);
        }
    }

    rtgroup_remove_list_.clear();

    if (rtgroup_map_.empty()) UnregisterTables();

    return true;
}

void RTargetGroupMgr::DisableRtGroupProcessing() {
    remove_rtgroup_trigger_->set_disable();
}

void RTargetGroupMgr::EnableRtGroupProcessing() {
    remove_rtgroup_trigger_->set_enable();
}

bool RTargetGroupMgr::IsRtGroupOnList(RtGroup *rtgroup) const {
    return rtgroup_remove_list_.find(rtgroup) != rtgroup_remove_list_.end();
}

RtGroupMgrTableState::RtGroupMgrTableState(BgpTable *table, 
                                           DBTableBase::ListenerId id)
    : id_(id), table_delete_ref_(this, table->deleter()) {
    assert(table->deleter() != NULL);
}

RtGroupMgrTableState::~RtGroupMgrTableState() {
}

void RtGroupMgrTableState::ManagedDelete() {
}
