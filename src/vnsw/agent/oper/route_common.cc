/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>

#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <cmn/agent_cmn.h>
#include <oper/agent_route.h>
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

string AgentRouteTableAPIS::GetSuffix(TableType type) {
    switch (type) {
      case AgentRouteTableAPIS::INET4_UNICAST:
          return ".uc.route.0";
      case AgentRouteTableAPIS::INET4_MULTICAST:
          return ".mc.route.0";
      case AgentRouteTableAPIS::LAYER2:
          return ".l2.route.0";
      default:
          return "";
    }
}

void AgentRouteTableAPIS::CreateRouteTablesInVrf(DB *db, const string &name,
                                          AgentRouteTable *table_list[]) {
    for (int rt_table_cnt = 0; rt_table_cnt < AgentRouteTableAPIS::MAX;
         rt_table_cnt++) {
        table_list[rt_table_cnt] = static_cast<AgentRouteTable *>
            (db->CreateTable(name + AgentRouteTableAPIS::GetSuffix(
               static_cast<AgentRouteTableAPIS::TableType>(rt_table_cnt))));
    }
}

DBTableBase *AgentRouteTableAPIS::CreateRouteTable(DB *db, const std::string &name,
                                                   TableType type) {
    AgentRouteTable *table;
    size_t index;

    switch (type) {
      case AgentRouteTableAPIS::INET4_UNICAST:
          table = static_cast<AgentRouteTable *>(new Inet4UnicastAgentRouteTable(db, name));
          index = name.rfind(GetSuffix(AgentRouteTableAPIS::INET4_UNICAST));
          break;
      case AgentRouteTableAPIS::INET4_MULTICAST:
          table = static_cast<AgentRouteTable *>(new Inet4MulticastAgentRouteTable(db, name));
          index = name.rfind(GetSuffix(AgentRouteTableAPIS::INET4_MULTICAST));
          break;
      case AgentRouteTableAPIS::LAYER2:
          table = static_cast<AgentRouteTable *>(new Layer2AgentRouteTable(db, name));
          index = name.rfind(GetSuffix(AgentRouteTableAPIS::LAYER2));
          break;
      default:
          return NULL;
    }
    table->Init();
    assert(index != string::npos);
    string vrf = name.substr(0, index);
    VrfEntry *vrf_entry = 
        static_cast<VrfEntry *>(Agent::GetInstance()->
                                GetVrfTable()->FindVrfFromName(vrf));
    assert(vrf_entry);
    table->SetVrfEntry(vrf_entry);
    table->SetVrfDeleteRef(vrf_entry->deleter());

    if (RouteTableTree[type] == NULL)
        RouteTableTree[type] = table;
    return table;
};

const NextHop* AgentPath::GetNextHop(void) const {
    if (nh_) {
        return nh_.get();
    }

    if (unresolved_ == true) {
        DiscardNH key;
        return static_cast<NextHop *>
            (Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&key));
    }

    //Indirect route's path, get direct route's NH
    const NextHop *nh = dependant_rt_.get()->GetActiveNextHop();
    if (nh == NULL) {
        assert(0);
    }
    return nh;
}

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
    RouteTable(db, name), db_(db), deleter_(new DeleteActor(this)),
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
    RouteEntry *route = 
        static_cast<RouteEntry *>(key->AllocRouteEntry(vrf, false));
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
    RouteEntry *route = static_cast<RouteEntry *>(entry);
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
       const RouteEntry *rt = *it;
       rt->RouteResyncReq(); 
    }
    unresolved_rt_tree_.clear();
}

void AgentRouteTable::AddUnresolvedRoute(const RouteEntry *rt) {
    unresolved_rt_tree_.insert(rt);
}

void AgentRouteTable::RemoveUnresolvedRoute(const RouteEntry *rt) {
    unresolved_rt_tree_.erase(rt);
}

void AgentRouteTable::DeleteRoute(DBTablePartBase *part,
                                  RouteEntry *rt, const Peer *peer) {
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
        rt->FillTrace(rt_info, RouteEntry::DELETE_PATH, rt->FindPath(peer));
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
        rt->FillTrace(rt_info_del, RouteEntry::DELETE, NULL);
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
    RouteEntry *rt = NULL;
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

    rt = static_cast<RouteEntry *>(part->Find(key));
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
                rt = static_cast<RouteEntry *>(key->AllocRouteEntry(vrf, 
                                               data->IsMulticast()));
                assert(rt->GetVrfEntry() != NULL);
                part->Add(rt);
                // Mark path as NULL so that its allocated below
                path = NULL;
                ProcessAdd(rt);
                rt->FillTrace(rt_info, RouteEntry::ADD, NULL);
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
                rt->FillTrace(rt_info, RouteEntry::ADD_PATH, path);
                OPER_TRACE(Route, rt_info);
                notify = true;
            } else {
                // Let path know of route change and update itself
                notify = data->AddChangePath(path);
                rt->FillTrace(rt_info, RouteEntry::CHANGE_PATH, path);
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
        //If this route has a unresolved path, insert to unresolved list 
        if (rt->HasUnresolvedPath() == true) {
            AddUnresolvedRoute(rt);
        }
    } else {
        DeleteRoute(part, rt, key->GetPeer());
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
    RouteEntry *route = static_cast<Inet4UnicastRouteEntry *>(entry);
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

RouteEntry *AgentRouteTable::FindActiveEntry(const RouteKey *key) {
    RouteEntry *entry = static_cast<RouteEntry *>(Find(key));
    if (entry && entry->IsDeleted()) {
        return NULL;
    }
    return entry;
}

bool AgentRouteTable::NotifyRouteEntryWalk(AgentXmppChannel *bgp_xmpp_peer,
                                  DBState *vrf_entry_state,
                                  bool associate, bool unicast_walk, bool multicast_walk,
                                  DBTablePartBase *part, DBEntryBase *entry) {
    RouteEntry *route = static_cast<RouteEntry *>(entry);
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

uint32_t RouteEntry::GetMplsLabel() const { 
    return GetActivePath()->GetLabel();
};

const string &RouteEntry::GetDestVnName() const { 
    return GetActivePath()->GetDestVnName();
};

string RouteEntry::ToString() const {
    return "Route Entry";
}

bool RouteEntry::IsLess(const DBEntry &rhs) const {
    int cmp = CompareTo(static_cast<const Route &>(rhs));
    return (cmp < 0);
};

uint32_t RouteEntry::GetVrfId() const {
    return vrf_->GetVrfId();
}

void RouteEntry::InsertPath(const AgentPath *path) {
	const Path *prev_front = front();
    insert(path);
    Sort(&AgentRouteTable::PathSelection, prev_front);
}

void RouteEntry::RemovePath(const Peer *peer) {
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

AgentPath *RouteEntry::FindPath(const Peer *peer) const {
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
const AgentPath *RouteEntry::GetActivePath() const {
    return static_cast<const AgentPath *>(front());
}

const NextHop *RouteEntry::GetActiveNextHop() const {
    const AgentPath *path = GetActivePath();
    if (path == NULL)
        return NULL;

    return path->GetNextHop();
}

const Peer *RouteEntry::GetActivePeer() const {
    const AgentPath *path = GetActivePath();
    if (path == NULL)
        return NULL;
    return path->GetPeer();
}

bool RouteEntry::CanDissociate() const {
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
                can_dissociate &= !obj->Layer2Forwarding();
            }
        }
    }
    return can_dissociate;
}

//If a direct route has changed, invoke a change on
//tunnel NH dependent on it
void RouteEntry::UpdateNH(void) {
    for (RouteEntry::tunnel_nh_iterator iter = tunnel_nh_list_.begin(); 
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
void RouteEntry::UpdateDependantRoutes(void) {
    for (RouteEntry::iterator iter = begin(); iter != end(); iter++) {
        RouteEntry *rt = iter.operator->();
        rt->RouteResyncReq();
    }
}

bool RouteEntry::HasUnresolvedPath(void) {
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

bool RouteEntry::Sync(void) {
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

bool AgentPath::ChangeNH(NextHop *nh) {
    // If NH is not found, point route to discard NH
    if (nh == NULL) {
        //TODO convert to oper_trace
        //LOG(DEBUG, "NH not found for route <" << path->vrf_name_ << 
        //    ":"  << rt_key->addr_.to_string() << "/" << rt_key->plen_
        //    << ">. Setting NH to Discard NH ");
        DiscardNHKey key;
        nh = static_cast<NextHop *>(Agent::GetInstance()->
                                    GetNextHopTable()->FindActiveEntry(&key));
    }

    if (nh_ != nh) {
        nh_ = nh;
        return true;
    }
    return false;
}

bool AgentPath::Sync(RouteEntry *sync_route) {
    bool ret = false;
    bool unresolved = false;

    //Check if there is change in policy on the interface
    //If yes update the path to point to policy enabled NH
    if (nh_.get() && nh_->GetType() == NextHop::INTERFACE) {
        const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh_.get());
        const VmPortInterface *vm_port = 
            static_cast<const VmPortInterface *>(intf_nh->GetInterface());

        bool policy = vm_port->IsPolicyEnabled();
        if (force_policy_) {
            policy = true;
        }

        if (intf_nh->PolicyEnabled() != policy) {
            //Make path point to policy enabled interface
            InterfaceNHKey key(new VmPortInterfaceKey(vm_port->GetUuid(), ""),
                                policy, intf_nh->GetFlags());
            nh_ = static_cast<NextHop *>
                (Agent::GetInstance()->
                 GetNextHopTable()->FindActiveEntry(&key));
            // If NH is not found, point route to discard NH
            if (nh_ == NULL) {
                LOG(DEBUG, "Interface NH for <" 
                    << boost::lexical_cast<std::string>(vm_port->GetUuid())
                    << " : policy = " << policy);
                DiscardNHKey key;
                nh_ = static_cast<NextHop *>
                    (Agent::GetInstance()->
                     GetNextHopTable()->FindActiveEntry(&key));
            }
        }
    }

    if (vrf_name_ == Agent::GetInstance()->NullString()) {
        return ret;
    }
 
    Inet4UnicastRouteEntry *rt = 
        Inet4UnicastAgentRouteTable::FindRoute(vrf_name_, gw_ip_);
    if (rt == sync_route) {
        rt = NULL;
    }

    if (rt == NULL) {
        unresolved = true;
    } else if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
        Inet4UnicastAgentRouteTable::AddArpReq(vrf_name_, gw_ip_);
        unresolved = true;
    } else {
        unresolved = false;
    }

    if (unresolved_ != unresolved) {
        unresolved_ = unresolved;
        ret = true;
    }
    //Reset to new gateway route, no nexthop for indirect route
    if (dependant_rt_.get() != rt) {
        dependant_rt_.reset(rt);
        ret = true;
    }
    return ret;
}

bool HostRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;
    InterfaceNHKey key(intf_.Clone(), false, InterfaceNHFlags::INET4);
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    if (path->GetDestVnName() != dest_vn_name_) {
        path->SetDestVnName(dest_vn_name_);
        ret = true;
    }
    if (path->GetProxyArp() != proxy_arp_) {
        path->SetProxyArp(proxy_arp_);
        ret = true;
    }

    path->SetUnresolved(false);
    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
} 

bool VirtualHostInterfaceRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;
    InterfaceNHKey key(intf_.Clone(), false, InterfaceNHFlags::INET4);
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    if (path->GetDestVnName() != dest_vn_name_) {
        path->SetDestVnName(dest_vn_name_);
        ret = true;
    }

    if (path->GetLabel() != label_) {
        path->SetLabel(label_);
        ret = true;
    }

    if (path->GetTunnelBmap() != tunnel_bmap_) {
        path->SetTunnelBmap(tunnel_bmap_);
        ret = true;
    }

    path->SetUnresolved(false);
    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
}

bool DropRoute::AddChangePath(AgentPath *path) {
    NextHop *nh = NULL;
    DiscardNHKey key;
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    path->SetUnresolved(false);
    if (path->ChangeNH(nh) == true)
        return true;

    return false;
}

bool LocalVmRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;

    //TODO Based on key table type pick up interface
    VmPortInterfaceKey intf_key(intf_.uuid_, "");
    VmPortInterface *vm_port = static_cast<VmPortInterface *>
        (Agent::GetInstance()->GetInterfaceTable()->FindActiveEntry(&intf_key));

    bool policy = false;
    // Use policy based NH if policy enabled on interface
    if (vm_port && vm_port->IsPolicyEnabled()) {
        policy = true;
    }
    // If policy force-enabled in request, enable policy
    path->SetForcePolicy(force_policy_);
    if (force_policy_) {
        policy = true;
    }
    InterfaceNHKey key(intf_.Clone(), policy, flags_);
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));

    if (path->GetLabel() != label_) {
        path->SetLabel(label_);
        ret = true;
    }

    if (path->GetTunnelBmap() != tunnel_bmap_) {
        path->SetTunnelBmap(tunnel_bmap_);
        ret = true;
    }

    if (path->GetDestVnName() != dest_vn_name_) {
        path->SetDestVnName(dest_vn_name_);
        ret = true;
    }

    if (path->GetProxyArp() != proxy_arp_) {
        path->SetProxyArp(proxy_arp_);
        ret = true;
    }

    path_sg_list = path->GetSecurityGroupList();
    if (path_sg_list != sg_list_) {
        path->SetSecurityGroupList(sg_list_);
        ret = true;
    }

    // When BGP path was added, the policy flag in BGP path was based on
    // interface config at that instance. If the policy flag changes in
    // path for "Local Peer", we should change policy flag on BGP peer
    // also. Check if policy has changed and enable SYNC of all path in
    // this case
    // Ideally his is needed only for LocalPath. But, having code for all
    // paths does not have any problem
    bool old_policy = false;
    bool new_policy = false;
    if (path->GetNextHop() && path->GetNextHop()->PolicyEnabled())
        old_policy = true;
    if (nh && nh->PolicyEnabled())
        new_policy = true;
    if (old_policy != new_policy) {
        sync_route_ = true;
    }

    path->SetUnresolved(false);
    path->SyncRoute(sync_route_);
    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
}

bool VlanNhRoute::AddChangePath(AgentPath *path) { 
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;

    assert(intf_.type_ == Interface::VMPORT);
    VlanNHKey key(intf_.uuid_, tag_);

    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    if (nh) {
        assert(nh->GetType() == NextHop::VLAN);
    }

    if (path->GetLabel() != label_) {
        path->SetLabel(label_);
        ret = true;
    }

    if (path->GetDestVnName() != dest_vn_name_) {
        path->SetDestVnName(dest_vn_name_);
        ret = true;
    }

    path_sg_list = path->GetSecurityGroupList();
    if (path_sg_list != sg_list_) {
        path->SetSecurityGroupList(sg_list_);
        ret = true;
    }

    path->SetUnresolved(false);
    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
}

bool RemoteVmRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;

    TunnelNHKey key(server_vrf_, Agent::GetInstance()->GetRouterId(),
                    server_ip_, false, TunnelType::ComputeType(tunnel_bmap_)); 
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    if (path->GetLabel() != label_) {
        path->SetLabel(label_);
        ret = true;
    }

    if (path->GetTunnelBmap() != tunnel_bmap_) {
        path->SetTunnelBmap(tunnel_bmap_);
        ret = true;
    }

    if (path->GetDestVnName() != dest_vn_name_) {
        path->SetDestVnName(dest_vn_name_);
        ret = true;
    }

    path->SetUnresolved(false);
    if (path->ChangeNH(nh) == true)
        ret = true;

    path_sg_list = path->GetSecurityGroupList();
    if (path_sg_list != sg_list_) {
        path->SetSecurityGroupList(sg_list_);
        ret = true;
    }

    return ret;
}

bool ResolveRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;
    ResolveNHKey key;

    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    path->SetUnresolved(false);
    if (path->GetDestVnName() != Agent::GetInstance()->GetFabricVnName()) {
        path->SetDestVnName(Agent::GetInstance()->GetFabricVnName());
        ret = true;
    }
    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
}

bool ReceiveRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;

    //TODO check if it needs to know table type
    ReceiveNHKey key(intf_.Clone(), policy_);
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    path->SetUnresolved(false);

    if (path->GetDestVnName() != vn_) {
        path->SetDestVnName(vn_);
        ret = true;
    }

    if (path->GetProxyArp() != proxy_arp_) {
        path->SetProxyArp(proxy_arp_);
        ret = true;
    }

    if (path->GetLabel() != label_) {
        path->SetLabel(label_);
        ret = true;
    }

    if (path->GetTunnelBmap() != tunnel_bmap_) {
        path->SetTunnelBmap(tunnel_bmap_);
        ret = true;
    }

    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
}

bool MulticastRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;

    CompositeNHKey key(vrf_name_, grp_addr_,
                       src_addr_, false, comp_type_);
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    path->SetDestVnName(vn_name_);
    path->SetUnresolved(false);
    ret = true;

    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
}

///////////////////////////////////////////////
// Sandesh routines below (route_sandesh.cc) 
//////////////////////////////////////////////
//TODO make it generic 
void UnresolvedNH::HandleRequest() const {

    VrfEntry *vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromId(0);
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }
   
    int count = 0;
    std::string empty(""); 
    AgentRouteTable *rt_table = static_cast<AgentRouteTable *>
        (vrf->GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST));
    AgentRouteTable::const_nh_iterator it;
    NhListResp *resp = new NhListResp();

    //TODO - Convert inet4ucroutetable to agentroutetable
    it = rt_table->unresolved_nh_begin();
    for (;it != rt_table->unresolved_nh_end(); it++) {
        count++;
        const NextHop *nh = *it;
        nh->DBEntrySandesh(resp, empty);
        if (count == 1) {
            resp->set_context(context()+"$");
            resp->Response();
            count = 0;
            resp = new NhListResp();
        }
    }

    resp->set_context(context());
    resp->Response();
    return;
}

//TODO IMplement filltrace in path class
void RouteEntry::FillTrace(RouteInfo &rt_info, Trace event, 
                           const AgentPath *path) {
    rt_info.set_ip(ToString());
    rt_info.set_vrf(GetVrfEntry()->GetName());

    switch(event) {
    case ADD:{
        rt_info.set_op("ADD");
        break;
    }

    case DELETE: {
        rt_info.set_op("DELETE");
        break;
    }

    case ADD_PATH:
    case DELETE_PATH:
    case CHANGE_PATH: {
        if (event == ADD_PATH) {
            rt_info.set_op("PATH ADD");
        } else if (event == CHANGE_PATH) {
            rt_info.set_op("PATH CHANGE");
        } else if (event == DELETE_PATH) {
            rt_info.set_op("PATH DELETE");
        }
        if (path->GetPeer()) {
            rt_info.set_peer(path->GetPeer()->GetName());
        }
        const NextHop *nh = path->GetNextHop();
        if (nh == NULL) {
            rt_info.set_nh_type("<NULL>");
            break;
        }

        switch (nh->GetType()) {
        case NextHop::TUNNEL: {
            const TunnelNH *tun = static_cast<const TunnelNH *>(nh);
            rt_info.set_nh_type("TUNNEL");
            rt_info.set_dest_server(tun->GetDip()->to_string());
            rt_info.set_dest_server_vrf(tun->GetVrf()->GetName());
            break;
        }

        case NextHop::ARP:{
            rt_info.set_nh_type("DIRECT");
            break;
        }

        case NextHop::INTERFACE: {
            const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
            rt_info.set_nh_type("INTERFACE");
            rt_info.set_intf(intf_nh->GetInterface()->GetName());
            break;
        }

        case NextHop::RECEIVE: {
            const ReceiveNH *rcv_nh = static_cast<const ReceiveNH *>(nh);
            rt_info.set_nh_type("RECEIVE");
            rt_info.set_intf(rcv_nh->GetInterface()->GetName());
            break;
        }

        case NextHop::DISCARD: {
            rt_info.set_nh_type("DISCARD");
            break;
        }

        case NextHop::VLAN: {
            rt_info.set_nh_type("VLAN");
            break;
        }

        case NextHop::RESOLVE: {
            rt_info.set_nh_type("RESOLVE");
            break;
        }

        case NextHop::COMPOSITE: {
            rt_info.set_nh_type("COMPOSITE");
            break;
        }
  
        default:
            assert(0);
            break;
        }
       break;
    }
    }
}

