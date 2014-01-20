/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>

#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <cmn/agent_cmn.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <controller/controller_export.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

SandeshTraceBufferPtr AgentDBwalkTraceBuf(SandeshTraceBufferCreate(
    AGENT_DBWALK_TRACE_BUF, 1000));

class AgentRouteTable::DeleteActor : public LifetimeActor {
  public:
    DeleteActor(AgentRouteTable *rt_table) : 
        LifetimeActor(Agent::GetInstance()->GetLifetimeManager()), 
        table_(rt_table) { 
    }
    virtual ~DeleteActor() { 
    }
    virtual bool MayDelete() const {
        if (table_->HasListeners() || table_->Size() != 0) {
            return false;
        }
        return true;
    }
    virtual void Shutdown() {
    }
    virtual void Destroy() {
        //Release refernce to VRF
        table_->vrf_delete_ref_.Reset(NULL);
        table_->SetVrfEntry(NULL);
    }

  private:
    AgentRouteTable *table_;
};

AgentRouteTable::AgentRouteTable(DB *db, const std::string &name) :
    RouteTable(db, name), walkid_(DBTableWalker::kInvalidWalkerId),
    db_(db), deleter_(new DeleteActor(this)),
    vrf_delete_ref_(this, NULL) { 
}

AgentRouteTable::~AgentRouteTable() {
};

auto_ptr<DBEntry> AgentRouteTable::AllocEntry(const DBRequestKey *k) const {
    const RouteKey *key = static_cast<const RouteKey*>(k);
    VrfKey vrf_key(key->GetVrfName());
    VrfEntry *vrf = 
        static_cast<VrfEntry *>(Agent::GetInstance()->
                                GetVrfTable()->Find(&vrf_key, true));
    AgentRoute *route = 
        static_cast<AgentRoute *>(key->AllocRouteEntry(vrf, false));
    return auto_ptr<DBEntry>(static_cast<DBEntry *>(route));
}

NextHop *AgentRouteTable::FindNextHop(NextHopKey *key) const {
    return static_cast<NextHop *>(Agent::GetInstance()->
                                  GetNextHopTable()->FindActiveEntry(key));
}

VrfEntry *AgentRouteTable::FindVrfEntry(const string &vrf_name) const {
    return Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf_name);
}

bool AgentRouteTable::DelPeerRoutes(DBTablePartBase *part, 
                                    DBEntryBase *entry, Peer *peer) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    if (route) {
        DeleteRoute(part, route, peer);
    }
    return true;
}

void AgentRouteTable::DeleteRouteDone(DBTableBase *base, 
                                      RouteTableWalkerState *state) {
    LOG(DEBUG, "Deleted all BGP injected routes for " << base->name());
    delete state;
}

bool AgentRouteTable::DelExplicitRouteWalkerCb(DBTablePartBase *part,
                                  DBEntryBase *entry) {
    return DelExplicitRoute(part, entry);
}

void AgentRouteTable::DeleteAllPeerRoutes() {
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();
    RouteTableWalkerState *state = new RouteTableWalkerState(deleter());
    walker->WalkTable(this, NULL, 
         boost::bind(&AgentRouteTable::DelExplicitRouteWalkerCb, this, _1, _2),
         boost::bind(&AgentRouteTable::DeleteRouteDone, this, _1, state));
}

// Algorithm to select an active path from multiple potential paths.
// Its a simple algorithm now to select path from 'lower' peer
// HOST and LOCAL_VM routes have 'peer' set to NULL. So, they take precedence
// always
bool AgentRouteTable::PathSelection(const Path &path1, const Path &path2) {
    const AgentPath &l_path = dynamic_cast<const AgentPath &> (path1);
    const AgentPath &r_path = dynamic_cast<const AgentPath &> (path2);
    return l_path.GetPeer()->ComparePath(r_path.GetPeer());
}

void AgentRouteTable::EvaluateUnresolvedNH(void) {
    //Trigger a change on all unresolved route
    for (UnresolvedNHTree::iterator it = unresolved_nh_tree_.begin();
         it != unresolved_nh_tree_.end(); ++it) {
        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        const NextHop *nh = *it;
        DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
        NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
        nh_key->sub_op_ = AgentKey::RESYNC;
        req.key = key;
        req.data.reset(NULL);
        Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);
    }
    unresolved_nh_tree_.clear();
}

void AgentRouteTable::AddUnresolvedNH(const NextHop *nh) {
    unresolved_nh_tree_.insert(nh);
}

void AgentRouteTable::RemoveUnresolvedNH(const NextHop *nh) {
    unresolved_nh_tree_.erase(nh);
}

void AgentRouteTable::EvaluateUnresolvedRoutes(void) {
    //Trigger a change on all unresolved route
    for (UnresolvedRouteTree::iterator it = unresolved_rt_tree_.begin();
         it !=  unresolved_rt_tree_.end(); ++it) {
       const AgentRoute *rt = *it;
       rt->RouteResyncReq(); 
    }
    unresolved_rt_tree_.clear();
}

void AgentRouteTable::AddUnresolvedRoute(const AgentRoute *rt) {
    unresolved_rt_tree_.insert(rt);
}

void AgentRouteTable::RemoveUnresolvedRoute(const AgentRoute *rt) {
    unresolved_rt_tree_.erase(rt);
}

void AgentRouteTable::DeleteRoute(DBTablePartBase *part,
                                  AgentRoute *rt, const Peer *peer) {
    bool notify = false;
    RouteInfo rt_info;

    if (rt == NULL) {
        return;
    }
    // Remember to notify if path being deleted is active one
    const AgentPath *active_path = static_cast<const AgentPath *>(rt->GetActivePath());
    if (active_path == NULL) {
        return;
    }
    notify = true;
    if (rt->FindPath(peer)) {
        rt->FillTrace(rt_info, AgentRoute::DELETE_PATH, rt->FindPath(peer));
        OPER_TRACE(Route, rt_info);
    }

    if (notify) {
        AGENT_ROUTE_LOG("Deleted route", rt->ToString(), GetVrfName(), 
                        peer);
    }

    // Remove path from the route
    rt->RemovePath(peer);

    // Delete route if no more paths 
    if (rt->GetActivePath() == NULL) {
        RouteInfo rt_info_del;
        rt->FillTrace(rt_info_del, AgentRoute::DELETE, NULL);
        OPER_TRACE(Route, rt_info_del);
        RemoveUnresolvedRoute(rt);
        rt->UpdateDependantRoutes();
        rt->UpdateNH();
        ProcessDelete(rt);
        part->Delete(rt);
        return;
    } else if (notify) {
        // Notify
        part->Notify(rt);
    }
}

//  Input handler for Route Table.
//  Adds a route entry if not present.
//      Adds path to route entry
//      Paths are sorted in order of their precedence
//  A DELETE request always removes path from the peer
//      Route entry with no paths is automatically deleted
void AgentRouteTable::Input(DBTablePartition *part, DBClient *client,
                            DBRequest *req) {
    RouteKey *key = static_cast<RouteKey *>(req->key.get());
    RouteData *data = static_cast<RouteData *>(req->data.get());
    AgentRouteTable *table = static_cast<AgentRouteTable *>(part->parent());
    AgentRoute *rt = NULL;
    AgentPath *path = NULL;
    bool notify = false;
    bool route_added = false;
    RouteInfo rt_info;

    VrfEntry *vrf = table->FindVrfEntry(key->GetVrfName());
    if (!vrf && req->oper == DBRequest::DB_ENTRY_DELETE) {
        return;
    } else {
        if (!vrf) {
            LOG(DEBUG, "Route ignored. VRF <" << key->GetVrfName()
                << "> not found.");
            return;
        }
    }

    AgentRouteTable *vrf_table = key->GetRouteTableFromVrf(vrf);
    if (vrf_table != this) {
        DBTablePartition *p = static_cast<DBTablePartition *>
            (vrf_table->GetTablePartition(key));
        vrf_table->Input(p, client, req);
        return;
    }

    rt = static_cast<AgentRoute *>(part->Find(key));
    if (key->sub_op_ == AgentKey::RESYNC) {
        if (rt) {
            rt->Sync();
            notify = true;
        }
    } else if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
        if(vrf->IsDeleted()) {
            //Route change, received on deleted VRF
            return;
        }

        if (rt && rt->IsDeleted()) {
            rt->ClearDelete();
            ProcessAdd(rt);
            notify = true;
        }

        if (data->GetOp() == RouteData::CHANGE) {
            // Add route if not present already
            if (rt == NULL) {
                //If route is a gateway route first check
                //if its corresponding direct route is present
                //or not, if not present dont add the route
                //just maintain it in unresolved list
                rt = static_cast<AgentRoute *>(key->AllocRouteEntry(vrf, 
                                               data->IsMulticast()));
                assert(rt->GetVrfEntry() != NULL);
                part->Add(rt);
                // Mark path as NULL so that its allocated below
                path = NULL;
                ProcessAdd(rt);
                rt->FillTrace(rt_info, AgentRoute::ADD, NULL);
                OPER_TRACE(Route, rt_info);
                route_added = true;
            } else {
                // RT present. Check if path is also present by peer
                path = rt->FindPath(key->GetPeer());
            }

            // Allocate path if not yet present
            if (path == NULL) {
                path = new AgentPath(key->GetPeer(), rt);
                rt->InsertPath(path);
                data->AddChangePath(path);
                rt->FillTrace(rt_info, AgentRoute::ADD_PATH, path);
                OPER_TRACE(Route, rt_info);
                notify = true;
            } else {
                // Let path know of route change and update itself
                notify = data->AddChangePath(path);
                rt->FillTrace(rt_info, AgentRoute::CHANGE_PATH, path);
                OPER_TRACE(Route, rt_info);
                AGENT_ROUTE_LOG("Path change", rt->ToString(), GetVrfName(),
                                key->GetPeer());
            }

            //TODO remove sync check via path
            if (path->RouteNeedsSync()) 
                rt->Sync();

            if (route_added == true) {
                AGENT_ROUTE_LOG("Added route", rt->ToString(), GetVrfName(),
                                key->GetPeer());
            }
        }

        if (route_added) {
            EvaluateUnresolvedRoutes();
            EvaluateUnresolvedNH();
        }
    } else {
        DeleteRoute(part, rt, key->GetPeer());
    }

    //If this route has a unresolved path, insert to unresolved list
    if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE ||
        key->sub_op_ == AgentKey::RESYNC) {
        if (rt && rt->HasUnresolvedPath() == true) {
            AddUnresolvedRoute(rt);
        }
    }

    //Route changed, trigger change on dependent routes
    if (notify) {
        part->Notify(rt);
        rt->UpdateDependantRoutes();
        rt->UpdateNH();
    }
}

bool AgentRouteTable::DelExplicitRoute(DBTablePartBase *part,
                                     DBEntryBase *entry) {
    AgentRoute *route = static_cast<Inet4UnicastRouteEntry *>(entry);
    if (route && !route->IsDeleted()) {
        //Remove all contral-node injected routes
        for(Route::PathList::iterator it = route->GetPathList().begin();
            it != route->GetPathList().end();) {
            const AgentPath *path =
                static_cast<const AgentPath *>(it.operator->());
            const Peer *peer = path->GetPeer();
            it++;
            if (peer && peer->GetType() == Peer::BGP_PEER) {
                DeleteRoute(part, route, path->GetPeer());
            }
        }
    }
    return true;
}

void AgentRouteTable::SetVrfEntry(VrfEntryRef vrf) {
    vrf_entry_ = vrf;
}

void AgentRouteTable::SetVrfDeleteRef(LifetimeActor *ref) {
    vrf_delete_ref_.Reset(ref);
}

LifetimeActor *AgentRouteTable::deleter() {
    return deleter_.get();
}

void AgentRouteTable::ManagedDelete() {
    //Delete all the routes
    DeleteAllPeerRoutes();
    deleter_->Delete();
}

void AgentRouteTable::MayResumeDelete(bool is_empty) {
    if (!deleter()->IsDeleted()) {
        return;
    }

    //
    // If the table has entries, deletion cannot be resumed
    //
    if (!is_empty) {
        return;
    }

    Agent::GetInstance()->GetLifetimeManager()->Enqueue(deleter());
}

AgentRoute *AgentRouteTable::FindActiveEntry(const RouteKey *key) {
    AgentRoute *entry = static_cast<AgentRoute *>(Find(key));
    if (entry && entry->IsDeleted()) {
        return NULL;
    }
    return entry;
}

bool AgentRouteTable::NotifyRouteEntryWalk(AgentXmppChannel *bgp_xmpp_peer,
                                  DBState *vrf_entry_state,
                                  bool associate, bool unicast_walk, bool multicast_walk,
                                  DBTablePartBase *part, DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    VrfExport::State *vs = static_cast<VrfExport::State *>(vrf_entry_state);

    if (!(unicast_walk && multicast_walk)) {
        if ((unicast_walk && route->IsMulticast()) ||
            (multicast_walk && !route->IsMulticast())) {
            return true;
        }
    }

    RouteExport::State *state =
        static_cast<RouteExport::State *>(route->GetState(part->parent(),
                      vs->rt_export_[GetTableType()]->GetListenerId()));
    if (state) {
        state->force_chg_ = true;
    }

    vs->rt_export_[GetTableType()]->
        Notify(bgp_xmpp_peer, associate, GetTableType(), part, entry);
    return true;
}

void AgentRouteTable::MulticastRouteNotifyDone(DBTableBase *base,
                                      DBState *state, Peer *peer) {
    VrfExport::State *vrf_state = static_cast<VrfExport::State *>(state);

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Done walk ", GetTableName(),
                    vrf_state->mcwalkid_[GetTableType()], 
                    peer->GetName(),
                    "Add/Del Route", peer->NoOfWalks());

    vrf_state->mcwalkid_[GetTableType()] = 
        DBTableWalker::kInvalidWalkerId;
}

void AgentRouteTable::UnicastRouteNotifyDone(DBTableBase *base,
                                             DBState *state, Peer *peer) {
    VrfExport::State *vrf_state = static_cast<VrfExport::State *>(state);

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Done walk ", GetTableName(),
                    vrf_state->ucwalkid_[GetTableType()], 
                    peer->GetName(),
                    "Add/Del Route", peer->NoOfWalks());

    vrf_state->ucwalkid_[GetTableType()] = 
        DBTableWalker::kInvalidWalkerId;
}

void AgentRouteTable::RebakeRouteEntryWalkDone(DBTableBase *part,
                                               bool unicast_walk, 
                                               bool multicast_walk) {
    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Done route rebake walk ", 
                       GetTableName(), walkid_, "", "", 0);
    walkid_ = DBTableWalker::kInvalidWalkerId;
}

bool AgentRouteTable::RebakeRouteEntryWalk(bool unicast_walk, 
                                           bool multicast_walk,
                                           DBTablePartBase *part, 
                                           DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    route->RouteResyncReq();
    return true;
}

void AgentRouteTable::RouteTableWalkerRebake(VrfEntry *vrf,
                                              bool unicast_walk,
                                              bool multicast_walk) {
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();

    if (walkid_ != DBTableWalker::kInvalidWalkerId) {
        AGENT_DBWALK_TRACE(AgentDBWalkLog, "Cancel route rebake walk ", 
                           GetTableName(), walkid_, "", "", 0);
        walker->WalkCancel(walkid_);
    }

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Starting route rebake walk ", 
                       GetTableName(), walkid_, "", "", 0);
    walkid_ = walker->WalkTable(this, NULL, 
                boost::bind(&AgentRouteTable::RebakeRouteEntryWalk, 
                            this, unicast_walk, multicast_walk, 
                            _1, _2),
                boost::bind(&AgentRouteTable::RebakeRouteEntryWalkDone, 
                            this, _1, unicast_walk, multicast_walk));
}

void AgentRouteTable::RouteTableWalkerNotify(VrfEntry *vrf,
                                            AgentXmppChannel *bgp_xmpp_peer,
                                            DBState *state,
                                            bool associate, bool unicast_walk,
                                            bool multicast_walk) {
    boost::system::error_code ec;
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();
    VrfExport::State *vrf_state = static_cast<VrfExport::State *>(state);

    if (multicast_walk) {
        if (vrf_state->mcwalkid_[GetTableType()] != 
                DBTableWalker::kInvalidWalkerId) {
            AGENT_DBWALK_TRACE(AgentDBWalkLog, "Cancel multicast/bcast walk ", 
                  GetTableName(), vrf_state->mcwalkid_[GetTableType()], 
                  bgp_xmpp_peer->GetBgpPeer()->GetName(), 
                  "Add/Withdraw Route",
                  bgp_xmpp_peer->GetBgpPeer()->NoOfWalks()); 
            walker->WalkCancel(vrf_state->mcwalkid_[GetTableType()]);
        }
        vrf_state->mcwalkid_[GetTableType()] = 
            walker->WalkTable(this, NULL,
             boost::bind(&AgentRouteTable::NotifyRouteEntryWalk, this,
             bgp_xmpp_peer, state, associate, false, true, _1, _2),
             boost::bind(&AgentRouteTable::MulticastRouteNotifyDone, 
                        this, _1, state, bgp_xmpp_peer->GetBgpPeer()));

        AGENT_DBWALK_TRACE(AgentDBWalkLog, "Start multicast/bcast walk ", 
                  GetTableName(), vrf_state->mcwalkid_[GetTableType()], 
                  bgp_xmpp_peer->GetBgpPeer()->GetName(), 
                  (associate)? "Add route": "Withdraw Route",
                  bgp_xmpp_peer->GetBgpPeer()->NoOfWalks());
    } 
    
    if (unicast_walk) {
        if (vrf_state->ucwalkid_[GetTableType()] != 
                DBTableWalker::kInvalidWalkerId) {
            AGENT_DBWALK_TRACE(AgentDBWalkLog, "Cancel ucast walk ", 
                  GetTableName(),
                  vrf_state->ucwalkid_[GetTableType()], 
                  bgp_xmpp_peer->GetBgpPeer()->GetName(), 
                  "Add/Withdraw Route",
                  bgp_xmpp_peer->GetBgpPeer()->NoOfWalks());
            walker->WalkCancel(vrf_state->ucwalkid_[GetTableType()]);
        }
        vrf_state->ucwalkid_[GetTableType()] = 
            walker->WalkTable(this, NULL,
             boost::bind(&AgentRouteTable::NotifyRouteEntryWalk, this,
             bgp_xmpp_peer, state, associate, true, false, _1, _2),
             boost::bind(&AgentRouteTable::UnicastRouteNotifyDone, 
                        this, _1, state, bgp_xmpp_peer->GetBgpPeer()));

        AGENT_DBWALK_TRACE(AgentDBWalkLog, "Start ucast walk ", 
                  GetTableName(), vrf_state->ucwalkid_[GetTableType()], 
                  bgp_xmpp_peer->GetBgpPeer()->GetName(), 
                  (associate)? "Add route": "Withdraw Route",
                  bgp_xmpp_peer->GetBgpPeer()->NoOfWalks());
    }
}

uint32_t AgentRoute::GetMplsLabel() const { 
    return GetActivePath()->GetLabel();
};

const string &AgentRoute::GetDestVnName() const { 
    return GetActivePath()->GetDestVnName();
};

string AgentRoute::ToString() const {
    return "Route Entry";
}

bool AgentRoute::IsLess(const DBEntry &rhs) const {
    int cmp = CompareTo(static_cast<const Route &>(rhs));
    return (cmp < 0);
};

uint32_t AgentRoute::GetVrfId() const {
    return vrf_->GetVrfId();
}

void AgentRoute::InsertPath(const AgentPath *path) {
	const Path *prev_front = front();
    insert(path);
    Sort(&AgentRouteTable::PathSelection, prev_front);
}

void AgentRoute::RemovePath(const Peer *peer) {
    for(Route::PathList::iterator it = GetPathList().begin(); 
        it != GetPathList().end(); it++) {
        AgentPath *path = static_cast<AgentPath *>(it.operator->());
        if (path->GetPeer() == peer) {
            const Path *prev_front = front();
            remove(path);
            path->ClearSecurityGroupList();
            Sort(&AgentRouteTable::PathSelection, prev_front);
            delete path;
            return;
        }
    }
}

AgentPath *AgentRoute::FindPath(const Peer *peer) const {
    for(Route::PathList::const_iterator it = GetPathList().begin(); 
        it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->GetPeer() == peer) {
            return const_cast<AgentPath *>(path);
        }
    }
    return NULL;
}

// First path in list is always treated as active path.
const AgentPath *AgentRoute::GetActivePath() const {
    return static_cast<const AgentPath *>(front());
}

const NextHop *AgentRoute::GetActiveNextHop() const {
    const AgentPath *path = GetActivePath();
    if (path == NULL)
        return NULL;

    return path->GetNextHop();
}

const Peer *AgentRoute::GetActivePeer() const {
    const AgentPath *path = GetActivePath();
    if (path == NULL)
        return NULL;
    return path->GetPeer();
}

bool AgentRoute::CanDissociate() const {
    bool can_dissociate = IsDeleted();
    if (IsMulticast()) {
        const NextHop *nh = GetActiveNextHop();
        const CompositeNH *cnh = static_cast<const CompositeNH *>(nh);
        if (cnh && cnh->ComponentNHCount() == 0) 
            return true;
        if (GetTableType() == AgentRouteTableAPIS::LAYER2) {
            const MulticastGroupObject *obj = 
                MulticastHandler::GetInstance()->
                FindFloodGroupObject(GetVrfEntry()->GetName());
            if (obj) {
                can_dissociate &= !obj->Ipv4Forwarding();
            }
        }

        if (GetTableType() == AgentRouteTableAPIS::INET4_MULTICAST) {
            const MulticastGroupObject *obj = 
                MulticastHandler::GetInstance()->
                FindFloodGroupObject(GetVrfEntry()->GetName());
            if (obj) {
                can_dissociate &= !obj->layer2_forwarding();
            }
        }
    }
    return can_dissociate;
}

//If a direct route has changed, invoke a change on
//tunnel NH dependent on it
void AgentRoute::UpdateNH(void) {
    for (AgentRoute::tunnel_nh_iterator iter = tunnel_nh_list_.begin(); 
         iter != tunnel_nh_list_.end(); iter++) {
        NextHop *nh = static_cast<NextHop *>(iter.operator->());
        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
        NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
        nh_key->sub_op_ = AgentKey::RESYNC;
        req.key = key;
        req.data.reset(NULL);
        Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);
    }
}

//If a direct route get modified invariably trigger change
//on all dependent indirect routes, coz if a nexthop has 
//changed we need to update the same in datapath for indirect
//routes
void AgentRoute::UpdateDependantRoutes(void) {
    for (AgentRoute::iterator iter = begin(); iter != end(); iter++) {
        AgentRoute *rt = iter.operator->();
        rt->RouteResyncReq();
    }
}

bool AgentRoute::HasUnresolvedPath(void) {
    for(Route::PathList::const_iterator it = GetPathList().begin();
            it != GetPathList().end(); it++) {
        const AgentPath *path =
            static_cast<const AgentPath *>(it.operator->());
        if (path->IsUnresolved() == true) {
            return true;
        }
    }

    return false;
}

bool AgentRoute::Sync(void) {
    bool ret = false;
    for(Route::PathList::iterator it = GetPathList().begin();
        it != GetPathList().end(); it++) {
        AgentPath *path = static_cast<AgentPath *>(it.operator->());
        if (path->Sync(this) == true) {
            if (GetActivePath() == path) {
                ret = true;
            }
        }
    }
    return ret;
}
