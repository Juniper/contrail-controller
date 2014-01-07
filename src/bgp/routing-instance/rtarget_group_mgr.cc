/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include "bgp/routing-instance/rtarget_group_mgr.h"

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include "base/task.h"
#include "base/task_annotations.h"

#include "bgp/bgp_config.h"
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
           boost::bind(&RTargetGroupMgr::RemoveRtGroups, this),
           TaskScheduler::GetInstance()->GetTaskId("bgp::RTFilter"), 0)),
    rtarget_dep_trigger_(new TaskTrigger(
           boost::bind(&RTargetGroupMgr::ProcessRouteTargetList, this),
           TaskScheduler::GetInstance()->GetTaskId("bgp::RTFilter"), 0)) {

    if (rtfilter_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        rtfilter_task_id_ = scheduler->GetTaskId("bgp::RTFilter");
    }

   process_queue_ = 
        new WorkQueue<RtGroupMgrReq *>(rtfilter_task_id_, 0, 
             boost::bind(&RTargetGroupMgr::RequestHandler, this, _1));

    id_ = server->routing_instance_mgr()->RegisterInstanceOpCallback(
        boost::bind(&RTargetGroupMgr::RoutingInstanceCallback, this, _1, _2));
}

bool RTargetGroupMgr::RequestHandler(RtGroupMgrReq *req) {
    CHECK_CONCURRENCY("bgp::RTFilter");

    switch (req->type_) {
        case RtGroupMgrReq::SHOW_RTGROUP: {
            ShowRtGroupResp *resp = 
                static_cast<ShowRtGroupResp *>(req->snh_resp_);
            std::vector<ShowRtGroupInfo> rtgroup_info_list;
            for (RtGroupMap::iterator it = rt_group_map_.begin(); 
                 it != rt_group_map_.end(); it++) {
                ShowRtGroupInfo info;
                info.set_rtarget(it->second->rt().ToString());
                std::vector<std::string> l3vpn_export_tables;
                BOOST_FOREACH(BgpTable *bgptable, 
                              it->second->GetExportTables(Address::INETVPN)) {
                    l3vpn_export_tables.push_back(bgptable->name());
                }
                info.set_l3vpn_export_tables(l3vpn_export_tables);

                std::vector<std::string> l3vpn_import_tables;
                BOOST_FOREACH(BgpTable *bgptable, 
                              it->second->GetImportTables(Address::INETVPN)) {
                    l3vpn_import_tables.push_back(bgptable->name());
                }
                info.set_l3vpn_import_tables(l3vpn_import_tables);

                std::vector<std::string> evpn_export_tables;
                BOOST_FOREACH(BgpTable *bgptable, 
                              it->second->GetExportTables(Address::EVPN)) {
                    evpn_export_tables.push_back(bgptable->name());
                }
                info.set_evpn_export_tables(evpn_export_tables);

                std::vector<std::string> evpn_import_tables;
                BOOST_FOREACH(BgpTable *bgptable, 
                              it->second->GetImportTables(Address::EVPN)) {
                    evpn_import_tables.push_back(bgptable->name());
                }
                info.set_evpn_import_tables(evpn_import_tables);

                const RtGroup::RTargetDepRouteList &dep_rt_list = 
                    it->second->DepRouteList();
                std::vector<std::string> rtlist;
                for (RtGroup::RTargetDepRouteList::const_iterator dep_it = 
                     dep_rt_list.begin(); dep_it != dep_rt_list.end(); dep_it++) {
                    for (RtGroup::RouteList::const_iterator dep_rt_it = 
                         dep_it->begin(); dep_rt_it != dep_it->end(); dep_rt_it++) {
                        rtlist.push_back((*dep_rt_it)->ToString());
                    }
                }
                info.set_dep_route(rtlist);
                std::vector<std::string> interested_peers;
                const RtGroup::InterestedPeerList &peer_list = it->second->PeerList();
                for (RtGroup::InterestedPeerList::const_iterator peer_it = 
                     peer_list.begin(); peer_it != peer_list.end(); peer_it++) {
                    interested_peers.push_back(peer_it->first->peer_name());
                }
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
            rtarget_trigger_list_.insert(rtarget);
            impacted_peers.insert(const_cast<BgpPeer *>(cur_it->first));
            cur_it++;
        } else if (*cur_it > *dbstate_it) {
            dbstate_next_it++;
            // Remove the Peer from RtGroup
            rtgroup->RemoveInterestedPeer(dbstate_it->first, rt);
            impacted_peers.insert(const_cast<BgpPeer *>(cur_it->first));
            rtarget_trigger_list_.insert(rtarget);
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
        rtarget_trigger_list_.insert(rtarget);
        assert(r.second);
    }
    for (dbstate_next_it = dbstate_it; 
         dbstate_it != dbstate->GetMutableList()->end(); 
         dbstate_it = dbstate_next_it) {
        dbstate_next_it++;
        // Remove the route from rtarget to route dep tree
        rtgroup->RemoveInterestedPeer(dbstate_it->first, rt);
        impacted_peers.insert(const_cast<BgpPeer *>(dbstate_it->first));
        rtarget_trigger_list_.insert(rtarget);
        dbstate->GetMutableList()->erase(dbstate_it);
    }

    if (dbstate->GetList().empty()) {
        rt->ClearState(table, id);
        delete dbstate;
        RemoveRtGroup(rtarget);
    }
    if (rtarget == RouteTarget::null_rtarget &&  !impacted_peers.empty()) {
        BOOST_FOREACH(BgpPeer *bgppeer, impacted_peers) {
            bgppeer->RegisterToVpnTables(false);
        }
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
        if (path->GetPeer()->IsXmppPeer()) {
            continue;
        }
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

bool RTargetGroupMgr::ProcessRouteTargetList() {
    CHECK_CONCURRENCY("bgp::RTFilter");

    for (RouteTargetTriggerList::iterator it = rtarget_trigger_list_.begin(), 
         itnext; it != rtarget_trigger_list_.end(); it = itnext) {
        itnext = it;
        itnext++;
        RtGroup *rtgroup = GetRtGroup(*it);
        if (rtgroup) {
            const RtGroup::RTargetDepRouteList &dep_rt_list = 
                rtgroup->DepRouteList();
            for (RtGroup::RTargetDepRouteList::const_iterator dep_it = 
                 dep_rt_list.begin(); dep_it != dep_rt_list.end(); dep_it++) {
                for (RtGroup::RouteList::const_iterator dep_rt_it = 
                     dep_it->begin(); dep_rt_it != dep_it->end(); dep_rt_it++) {
                    DBTablePartBase *partition = 
                        (*dep_rt_it)->get_table_partition();
                    partition->Notify(*dep_rt_it);
                }
            }
        }
        rtarget_trigger_list_.erase(it);
    }
    return true;
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
         it != rtarget_route_list_.end(); it++)
        BuildRTargetDistributionGraph(table, *it, id);

    rtarget_route_list_.clear();

    if (rtarget_trigger_list_.size())
        rtarget_dep_trigger_->Set();
    return true;
}


void RTargetGroupMgr::RoutingInstanceCallback(std::string name, int op) {
    if (name == BgpConfigManager::kMasterInstance) {
        if ((op == RoutingInstanceMgr::INSTANCE_DELETE) && 
            rt_group_map_.empty()) {
            UnregisterTables();
        } else if (op == RoutingInstanceMgr::INSTANCE_ADD) {
            RoutingInstanceMgr *mgr = server()->routing_instance_mgr();
            RoutingInstance *master =
                mgr->GetRoutingInstance(BgpConfigManager::kMasterInstance);
            BgpTable *vpntable = master->GetTable(Address::INETVPN);
            DBTableBase::ListenerId id = 
                vpntable->Register(boost::bind(&RTargetGroupMgr::VpnRouteNotify, 
                                                  this, _1, _2));
            RtGroupMgrTableState *ts = new RtGroupMgrTableState(vpntable, id);
            table_state_.insert(std::make_pair(vpntable, ts));

            vpntable = master->GetTable(Address::EVPN);
            id = 
                vpntable->Register(boost::bind(&RTargetGroupMgr::VpnRouteNotify, 
                                                 this, _1, _2));
            ts = new RtGroupMgrTableState(vpntable, id);
            table_state_.insert(std::make_pair(vpntable, ts));

            BgpTable *rttable = master->GetTable(Address::RTARGET);
            id = 
                rttable->Register(boost::bind(&RTargetGroupMgr::RTargetRouteNotify, 
                                              this, _1, _2));
            ts = new RtGroupMgrTableState(rttable, id);
            table_state_.insert(std::make_pair(rttable, ts));
        }
    }
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
    server()->routing_instance_mgr()->UnregisterInstanceOpCallback(id_);
}

// Search a RtGroup
RtGroup *RTargetGroupMgr::GetRtGroup(const RouteTarget &rt) {
    tbb::mutex::scoped_lock lock(mutex_);
    RtGroupMap::iterator loc = rt_group_map_.find(rt);
    if (loc != rt_group_map_.end()) {
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
    RtGroupMap::iterator loc = rt_group_map_.find(rt);
    RtGroup *group = (loc != rt_group_map_.end()) ? loc->second : NULL;
    if (group == NULL) {
        group = new RtGroup(rt);
        rt_group_map_.insert(rt, group);
    }
    return group;
}

void RTargetGroupMgr::RemoveRtGroup(const RouteTarget &rt) {
    tbb::mutex::scoped_lock lock(mutex_);
    RtGroupMap::iterator loc = rt_group_map_.find(rt);
    RtGroup *rtgroup = (loc != rt_group_map_.end()) ? loc->second : NULL;
    assert(rtgroup);

    BOOST_FOREACH(const RtGroup::RtGroupMembers::value_type &family_members, 
                  rtgroup->GetImportMembers()) {
        if (!family_members.second.empty()) return;
    }

    BOOST_FOREACH(const RtGroup::RtGroupMembers::value_type &family_members, 
                  rtgroup->GetExportMembers()) {
        if (!family_members.second.empty()) return;
    }

    if (rtgroup->RouteDepListEmpty() && rtgroup->peer_list_empty()) {
        rtgroup_remove_list_.insert(rt);
        remove_rtgroup_trigger_->Set();
    }
}

void RTargetGroupMgr::UnregisterTables() {
    CHECK_CONCURRENCY("bgp::Config", "bgp::RTFilter");

    if (rt_group_map_.empty()) {
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
        }
    }
}

bool RTargetGroupMgr::RemoveRtGroups() {
    CHECK_CONCURRENCY("bgp::RTFilter");

    for (RtGroupRemoveList::iterator it = rtgroup_remove_list_.begin(); 
         it != rtgroup_remove_list_.end(); it++) {
        RtGroupMap::iterator loc = rt_group_map_.find(*it);
        RtGroup *rtgroup = (loc != rt_group_map_.end()) ? loc->second : NULL;
        assert(rtgroup);

        BOOST_FOREACH(const RtGroup::RtGroupMembers::value_type &family_members, 
                      rtgroup->GetImportMembers()) {
            if (!family_members.second.empty()) continue;
        }

        BOOST_FOREACH(const RtGroup::RtGroupMembers::value_type &family_members, 
                      rtgroup->GetExportMembers()) {
            if (!family_members.second.empty()) continue;
        }

        if (rtgroup->RouteDepListEmpty() && rtgroup->peer_list_empty()) {
            rt_group_map_.erase(*it);
        }
    }

    rtgroup_remove_list_.clear();

    if (rt_group_map_.empty()) UnregisterTables();

    return true;
}

RtGroupMgrTableState::RtGroupMgrTableState(BgpTable *table, 
                                           DBTableBase::ListenerId id)
    : id_(id), table_delete_ref_(this, table->deleter()) {
    assert(table->deleter() != NULL);
}

RtGroupMgrTableState::~RtGroupMgrTableState() {
}

// TODO: verify that the RoutePathReplicator is going to Leave this table.
void RtGroupMgrTableState::ManagedDelete() {
}
