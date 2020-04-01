/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <boost/statechart/custom_reaction.hpp>
#include <boost/statechart/event.hpp>
#include <boost/statechart/simple_state.hpp>
#include <boost/statechart/state.hpp>
#include <boost/statechart/state_machine.hpp>
#include <cmn/agent_cmn.h>
#include <route/route.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/mirror_table.h>
#include <oper/path_preference.h>

namespace sc = boost::statechart;
namespace mpl = boost::mpl;

SandeshTraceBufferPtr PathPreferenceTraceBuf(
        SandeshTraceBufferCreate("PathPreference", 5000));
struct EvStart : sc::event<EvStart> {
    EvStart() {
    }

    static const char *Name() {
        return "Start";
    }
};

struct EvTrafficSeen : sc::event<EvTrafficSeen> {
    EvTrafficSeen() {
    }
    static const char *Name() {
        return "TrafficSeen";
    }
};

struct EvWaitForTraffic : sc::event<EvWaitForTraffic> {
    EvWaitForTraffic() {
    }
    static const char *Name() {
        return "WaitForTraffic";
    }
};

struct EvSeqChange : sc::event<EvSeqChange> {
    EvSeqChange(uint32_t sequence) : sequence_(sequence) {
    }
    static const char *Name() {
        return "SeqChange";
    }
    uint32_t sequence_;
};

//Path transition to ECMP
struct EvActiveActiveMode : sc::event<EvActiveActiveMode> {
    EvActiveActiveMode() {
    }
    static const char *Name() {
        return "EcmpRoute";
    }
};

struct EvControlNodeInSync : sc::event<EvControlNodeInSync> {
    EvControlNodeInSync() {
    }
    static const char *Name() {
        return "Control node route in sync";
    }
};

struct Init : public sc::state<Init, PathPreferenceSM> {
    typedef mpl::list<
        sc::custom_reaction<EvStart>
    > reactions;

    Init(my_context ctx) : my_base(ctx) {
        PathPreferenceSM *state_machine = &context<PathPreferenceSM>();
        state_machine->Log("INIT");
    }

    sc::result react(const EvStart &event) {
        return transit<WaitForTraffic>();
    }
};

struct WaitForTraffic : sc::state<WaitForTraffic, PathPreferenceSM> {
    typedef mpl::list<
        sc::custom_reaction<EvTrafficSeen>,
        sc::custom_reaction<EvSeqChange>,
        sc::custom_reaction<EvWaitForTraffic>,
        sc::custom_reaction<EvActiveActiveMode>,
        sc::custom_reaction<EvControlNodeInSync>
    > reactions;

    WaitForTraffic(my_context ctx) : my_base(ctx) {
        PathPreferenceSM *state_machine = &context<PathPreferenceSM>();
        if (state_machine->wait_for_traffic() == false) {
            state_machine->set_wait_for_traffic(true);
            state_machine->set_preference(PathPreference::LOW);
            state_machine->EnqueuePathChange();
            state_machine->UpdateDependentRoute();
            state_machine->Log("Wait For Traffic");
        }
    }

    sc::result react(const EvTrafficSeen &event) {
        return transit<TrafficSeen>();
    }

    sc::result react(const EvSeqChange &event) {
        PathPreferenceSM *state_machine = &context<PathPreferenceSM>();
        state_machine->set_max_sequence(event.sequence_);
        return discard_event();
    }

    sc::result react(const EvWaitForTraffic &event) {
        return discard_event();
    }

    sc::result react(const EvActiveActiveMode &event) {
        return transit<ActiveActiveState>();
    }

    sc::result react(const EvControlNodeInSync &event) {
        return discard_event();
    }
};

struct TrafficSeen : sc::state<TrafficSeen, PathPreferenceSM> {
    typedef mpl::list<
        sc::custom_reaction<EvTrafficSeen>,
        sc::custom_reaction<EvSeqChange>,
        sc::custom_reaction<EvWaitForTraffic>,
        sc::custom_reaction<EvActiveActiveMode>,
        sc::custom_reaction<EvControlNodeInSync>
    > reactions;

    TrafficSeen(my_context ctx) : my_base(ctx) {
        PathPreferenceSM *state_machine = &context<PathPreferenceSM>();
        //Enqueue a route change
        if (state_machine->wait_for_traffic() == true) {
           state_machine->UpdateFlapTime();
           uint32_t seq = state_machine->max_sequence();
           state_machine->set_wait_for_traffic(false);
           seq++;
           state_machine->set_max_sequence(seq);
           if (state_machine->is_dependent_rt() == false) {
               state_machine->set_sequence(seq);
           }
           state_machine->set_preference(PathPreference::HIGH);
           state_machine->EnqueuePathChange();
           state_machine->UpdateDependentRoute();
           state_machine->Log("Traffic seen");
        }
    }

    sc::result react(const EvTrafficSeen &event) {
        return discard_event();
    }

    sc::result react(const EvWaitForTraffic &event) {
        return transit<WaitForTraffic>();
    }

    sc::result react(const EvSeqChange &event) {
        PathPreferenceSM *state_machine = &context<PathPreferenceSM>();
        if (event.sequence_ > state_machine->sequence()) {
            state_machine->set_max_sequence(event.sequence_);
            if (state_machine->IsPathFlapping()) {
                //If path is continuosly flapping
                //delay wihtdrawing of route
                if (state_machine->RetryTimerRunning() == false) {
                    state_machine->IncreaseRetryTimeout();
                    state_machine->StartRetryTimer();
                    state_machine->Log("Back off and retry update");
                }
                return discard_event();
            }
        }
        return transit<WaitForTraffic>();
    }

    sc::result react(const EvActiveActiveMode &event) {
        return transit<ActiveActiveState>();
    }

    sc::result react(const EvControlNodeInSync &event) {
        PathPreferenceSM *state_machine = &context<PathPreferenceSM>();
        state_machine->Log("in sync with control-node");
        return discard_event();
    }
};

struct ActiveActiveState : sc::state<ActiveActiveState, PathPreferenceSM> {
    typedef mpl::list<
        sc::custom_reaction<EvTrafficSeen>,
        sc::custom_reaction<EvSeqChange>,
        sc::custom_reaction<EvWaitForTraffic>,
        sc::custom_reaction<EvActiveActiveMode>,
        sc::custom_reaction<EvControlNodeInSync>
    > reactions;

    ActiveActiveState(my_context ctx) : my_base(ctx) {
        PathPreferenceSM *state_machine = &context<PathPreferenceSM>();
        //Enqueue a route change
        if (state_machine->preference() == PathPreference::LOW ||
            state_machine->wait_for_traffic() == true) {
           state_machine->set_wait_for_traffic(false);
           uint32_t seq = 0;
           state_machine->set_max_sequence(seq);
           state_machine->set_sequence(seq);
           state_machine->set_preference(PathPreference::HIGH);
           state_machine->EnqueuePathChange();
           state_machine->UpdateDependentRoute();
           state_machine->Log("Ecmp path");
        }
    }

    sc::result react(const EvTrafficSeen &event) {
        return discard_event();
    }

    sc::result react(const EvWaitForTraffic &event) {
        return transit<WaitForTraffic>();
    }

    sc::result react(const EvSeqChange &event) {
        PathPreferenceSM *state_machine = &context<PathPreferenceSM>();
        state_machine->set_max_sequence(event.sequence_);
        return transit<WaitForTraffic>();
    }

    sc::result react(const EvActiveActiveMode &event) {
        return discard_event();
    }

    sc::result react(const EvControlNodeInSync &event) {
        return discard_event();
    }
};

PathPreferenceSM::PathPreferenceSM(Agent *agent, const Peer *peer,
    AgentRoute *rt, bool is_dependent_route,
    const PathPreference &pref): agent_(agent), peer_(peer),
    rt_(rt), path_preference_(0, PathPreference::LOW, false, false),
    max_sequence_(0), timer_(NULL), timeout_(kMinInterval),
    flap_count_(0), is_dependent_rt_(is_dependent_route),
    dependent_rt_(this) {
    path_preference_ = pref;
    initiate();
    process_event(EvStart());
    backoff_timer_fired_time_ = UTCTimestampUsec();
}

PathPreferenceSM::~PathPreferenceSM() {
    if (timer_ != NULL) {
        timer_->Cancel();
        TimerManager::DeleteTimer(timer_);
    }

    PathDependencyList::iterator iter = dependent_routes_.begin();
    for (;iter != dependent_routes_.end(); iter++) {
        PathPreferenceSM *path_sm = iter.operator->();
        path_sm->process_event(EvWaitForTraffic());
    }

    timer_ = NULL;
}

bool PathPreferenceSM::Retry() {
    flap_count_ = 0;
    backoff_timer_fired_time_ = UTCTimestampUsec();
    process_event(EvWaitForTraffic());
    return false;
}

void PathPreferenceSM::StartRetryTimer() {
    if (timer_ == NULL) {
        timer_ = TimerManager::CreateTimer(
                *(agent_->event_manager())->io_service(),
                "Stale cleanup timer",
                TaskScheduler::GetInstance()->GetTaskId("db::DBTable"),
                0, false);
    }
    timer_->Start(timeout_,
                  boost::bind(&PathPreferenceSM::Retry, this));
}

void PathPreferenceSM::CancelRetryTimer() {
    if (timer_ == NULL) {
        return;
    }
    timer_->Cancel();
}

bool PathPreferenceSM::RetryTimerRunning() {
    if (timer_ == NULL) {
        return false;
    }
    return timer_->running();
}

void PathPreferenceSM::IncreaseRetryTimeout() {
    timeout_ = timeout_ * 2;
    if (timeout_ > kMaxInterval) {
        timeout_ = kMaxInterval;
    }
}

bool PathPreferenceSM::IsFlap() const {
    uint64_t time_sec = (UTCTimestampUsec() -
                         last_stable_high_priority_change_at_)/1000;
    if (time_sec > kMinInterval) {
        return false;
    }
    return true;
}

void PathPreferenceSM::DecreaseRetryTimeout() {
    uint64_t time_sec =
        (UTCTimestampUsec() - backoff_timer_fired_time_)/1000;
    if (time_sec > kMinInterval) {
        timeout_ = kMinInterval;
    }
}

void PathPreferenceSM::UpdateFlapTime() {
    if (IsFlap()) {
        flap_count_++;
    } else {
        DecreaseRetryTimeout();
        last_stable_high_priority_change_at_ = UTCTimestampUsec();
        flap_count_ = 0;
    }
}

bool PathPreferenceSM::IsPathFlapping() const {
    if (flap_count_ >= kMaxFlapCount && IsFlap()) {
        return true;
    }
    return false;
}

void PathPreferenceSM::UpdateDependentRoute() {
    if (is_dependent_rt_ == true) {
        return;
    }

    PathDependencyList::iterator iter = dependent_routes_.begin();
    for (;iter != dependent_routes_.end(); iter++) {
        PathPreferenceSM *path_sm = iter.operator->();
        if (path_preference_.preference() == PathPreference::HIGH) {
            path_sm->process_event(EvTrafficSeen());
        } else {
            path_sm->process_event(EvWaitForTraffic());
        }
    }
}

void PathPreferenceSM::Process() {
     uint32_t max_sequence = 0;
     const AgentPath *best_path = NULL;

     const AgentPath *local_path =  rt_->FindPath(peer_);
     //Dont act on notification of derived routes
     if (is_dependent_rt_) {
         path_preference_.set_ecmp(local_path->path_preference().ecmp());
         if (dependent_rt_.get()) {
             if (dependent_rt_->path_preference_.preference() ==
                     PathPreference::HIGH) {
                 process_event(EvTrafficSeen());
             } else {
                 process_event(EvWaitForTraffic());
             }
         }
         return;
     }

     if (local_path->path_preference().ecmp() == true) {
         path_preference_.set_ecmp(true);
         //If a path is ecmp, just set the priority to HIGH
         process_event(EvActiveActiveMode());
         return;
     }

     if (ecmp() == true) {
         path_preference_.set_ecmp(local_path->path_preference().ecmp());
         //Route transition from ECMP to non ECMP,
         //move to wait for traffic state
         process_event(EvWaitForTraffic());
         return;
     }

     //Check if BGP path is present
     for (Route::PathList::iterator it = rt_->GetPathList().begin();
          it != rt_->GetPathList().end(); ++it) {
         const AgentPath *path =
             static_cast<const AgentPath *>(it.operator->());
         if (path == local_path) {
             if (path->path_preference().sequence() < sequence()) {
                 EnqueuePathChange();
             }
             continue;
         }
         //Get best preference and sequence no from all BGP peer
         if (max_sequence < path->sequence()) {
             max_sequence = path->sequence();
             best_path = path;
         }
     }

     if (!best_path) {
         return;
     }

     if (max_sequence > sequence()) {
         process_event(EvSeqChange(max_sequence));
     } else if (sequence() == max_sequence &&
             best_path->ComputeNextHop(agent_) ==
             local_path->ComputeNextHop(agent_)) {
         //Control node chosen path and local path are same
         process_event(EvControlNodeInSync());
     } else if (sequence() == max_sequence &&
             best_path->ComputeNextHop(agent_) !=
             local_path->ComputeNextHop(agent_)) {
         process_event(EvWaitForTraffic());
     }

     UpdateDependentRoute();
}

void PathPreferenceSM::Log(std::string state) {
    std::string dependent_ip_str = "";
    if (is_dependent_rt()) {
        dependent_ip_str = dependent_ip().to_string();
    }

    PATH_PREFERENCE_TRACE(rt_->vrf()->GetName(), rt_->GetAddressString(),
                          preference(), sequence(), state, timeout(),
                          dependent_ip_str, flap_count_);
}

void PathPreferenceSM::EnqueuePathChange() {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key = rt_->GetDBRequestKey();
    AgentRouteKey *key = static_cast<AgentRouteKey *>(req.key.get());
    key->sub_op_ = AgentKey::RESYNC;
    key->set_peer(peer_);
    req.data.reset(new PathPreferenceData(path_preference_));

    if (rt_->vrf() == NULL) {
        return;
    }

    AgentRouteTable *table = NULL;
    if (rt_->GetTableType() == Agent::EVPN) {
        table = agent_->fabric_evpn_table();
    } else if (rt_->GetTableType() == Agent::INET4_UNICAST) {
        table = agent_->fabric_inet4_unicast_table();
    } else if (rt_->GetTableType() == Agent::INET4_MPLS) {
        table = agent_->fabric_inet4_mpls_table();
    } else if (rt_->GetTableType() == Agent::INET6_UNICAST) {
        table = agent_->fabric_inet4_unicast_table();
    }

    if (table) {
        table->Enqueue(&req);
    }
}

PathPreferenceIntfState::RouteAddrList::RouteAddrList() :
    family_(Address::INET), ip_(), plen_(0), vrf_name_(), seen_(false) {
}

PathPreferenceIntfState::RouteAddrList::RouteAddrList
    (const Address::Family &family, const IpAddress &ip, uint32_t plen,
     const std::string &vrf) :
    family_(family), ip_(ip), plen_(plen), vrf_name_(vrf), seen_(false) {
}

bool PathPreferenceIntfState::RouteAddrList::operator<(
        const RouteAddrList &rhs) const {
    if (family_ != rhs.family_) {
        return family_ < rhs.family_;
    }

    if (ip_ != rhs.ip_) {
        return ip_ < rhs.ip_;
    }

    if (plen_ != rhs.plen_) {
        return plen_ < rhs.plen_;
    }

    return vrf_name_ < rhs.vrf_name_;
}

bool PathPreferenceIntfState::RouteAddrList::operator==(
        const RouteAddrList &rhs) const {
    if ((family_ == rhs.family_) && (ip_ == rhs.ip_) &&
        (plen_ == rhs.plen_) && (vrf_name_ == rhs.vrf_name_)) {
        return true;
    }
    return false;
}

PathPreferenceIntfState::PathPreferenceIntfState(const VmInterface *intf):
    intf_(intf) {
}

PathPreferenceState::PathPreferenceState(Agent *agent,
    AgentRoute *rt): agent_(agent), rt_(rt) {
}

PathPreferenceState::~PathPreferenceState() {
    PeerPathPreferenceMap::iterator path_preference_it =
        path_preference_peer_map_.begin();
    while (path_preference_it != path_preference_peer_map_.end()) {
        PeerPathPreferenceMap::iterator prev_it = path_preference_it++;
        PathPreferenceSM *path_preference_sm = prev_it->second;
        delete path_preference_sm;
        path_preference_peer_map_.erase(prev_it);
    }

    PathPreferenceModule *path_module =
        agent_->oper_db()->route_preference_module();
    path_module->DeleteUnresolvedPath(this);
}

//Given a VRF table the table listener id for given route
bool
PathPreferenceState::GetRouteListenerId(const VrfEntry* vrf,
                                        const Agent::RouteTableType &table_type,
                                        DBTableBase::ListenerId &rt_id) const {
    if (vrf == NULL) {
        return false;
    }

    DBTableBase::ListenerId vrf_id =
        agent_->oper_db()->route_preference_module()->vrf_id();
    const PathPreferenceVrfState *vrf_state =
        static_cast<const PathPreferenceVrfState *>(
                vrf->GetState(agent_->vrf_table(), vrf_id));
    if (!vrf_state) {
        return false;
    }

    rt_id = DBTableBase::kInvalidId;
    if (table_type == Agent::EVPN) {
        rt_id = vrf_state->evpn_rt_id_;
    } else if (table_type == Agent::INET4_UNICAST) {
        rt_id = vrf_state->uc_rt_id_;
    } else if (table_type == Agent::INET4_MPLS) {
        rt_id = vrf_state->mpls_rt_id_;
    } else if (table_type == Agent::INET6_UNICAST) {
        rt_id = vrf_state->uc6_rt_id_;
    } else {
        return false;
    }

    return true;
}

PathPreferenceSM*
PathPreferenceState::GetDependentPath(const AgentPath *path) const {

    if (path->path_preference().IsDependentRt() == false) {
        return NULL;
    }
    uint32_t plen = 32;
    if (path->path_preference().dependent_ip().is_v6()) {
        plen = 128;
    }

    InetUnicastRouteKey key(path->peer(), path->path_preference().vrf(),
                            path->path_preference().dependent_ip(), plen);
    const VrfEntry *vrf =
        agent_->vrf_table()->FindVrfFromName(path->path_preference().vrf());
    if (!vrf) {
        return NULL;
    }

    AgentRouteTable *table = NULL;
    Agent::RouteTableType table_type = Agent::INET4_UNICAST;
    if (path->path_preference().dependent_ip().is_v4()) {
        table = static_cast<AgentRouteTable *>(
                                 vrf->GetInet4UnicastRouteTable());
    } else if (path->path_preference().dependent_ip().is_v6()) {
        table = static_cast<AgentRouteTable *>(
                                 vrf->GetInet6UnicastRouteTable());
        table_type = Agent::INET6_UNICAST;
    }
    AgentRoute *rt = static_cast<AgentRoute *>(table->Find(&key));
    if (rt == NULL) {
        return NULL;
    }

    DBTableBase::ListenerId rt_id = DBTableBase::kInvalidId;
    GetRouteListenerId(vrf, table_type, rt_id);

    PathPreferenceState *state = static_cast<PathPreferenceState *>(
            rt->GetState(table, rt_id));
    if (state == NULL) {
        return NULL;
    }
    return state->GetSM(path->peer());
}

void PathPreferenceState::Process(bool &should_resolve) {
    PathPreferenceModule *path_module =
        agent_->oper_db()->route_preference_module();
    //Set all the path as not seen, eventually when path is seen
    //flag would be set appropriatly
     PeerPathPreferenceMap::iterator path_preference_it =
         path_preference_peer_map_.begin();
     while (path_preference_it != path_preference_peer_map_.end()) {
         PathPreferenceSM *path_preference_sm = path_preference_it->second;
         path_preference_sm->set_seen(false);
         path_preference_it++;
     }

     for (Route::PathList::iterator it = rt_->GetPathList().begin();
          it != rt_->GetPathList().end(); ++it) {
         const AgentPath *path =
             static_cast<const AgentPath *>(it.operator->());
         if (path->peer() == NULL) {
             continue;
         }
         if (path->peer()->GetType() != Peer::LOCAL_VM_PORT_PEER) {
             continue;
         }

         bool new_path_added = false;
         PathPreferenceSM *path_preference_sm;
         if (path_preference_peer_map_.find(path->peer()) ==
                 path_preference_peer_map_.end()) {
             //Add new path
             path_preference_sm =
                 new PathPreferenceSM(agent_, path->peer(), rt_,
                                      false, path->path_preference());
             path_preference_peer_map_.insert(
                std::pair<const Peer *, PathPreferenceSM *>
                (path->peer(), path_preference_sm));
             new_path_added = true;
         } else {
             path_preference_sm =
                 path_preference_peer_map_.find(path->peer())->second;
         }
         bool dependent_rt = path->path_preference().IsDependentRt();
         if (dependent_rt) {
             PathPreferenceSM *sm = GetDependentPath(path);
             if (sm == NULL) {
                 //Path is unresolved,
                 //add it to unresolved list
                 path_module->AddUnresolvedPath(this);
             } else {
                 path_module->DeleteUnresolvedPath(this);
             }
             path_preference_sm->set_dependent_rt(sm);
         } else {
             path_preference_sm->set_dependent_rt(NULL);
         }
         path_preference_sm->set_is_dependent_rt(dependent_rt);
         path_preference_sm->set_dependent_ip(
                 path->path_preference().dependent_ip());

         path_preference_sm->set_seen(true);
         path_preference_sm->Process();

         if (dependent_rt == false && new_path_added) {
             should_resolve = true;
         }
     }

     //Delete all path not seen, in latest path list
     path_preference_it = path_preference_peer_map_.begin();
     while (path_preference_it != path_preference_peer_map_.end()) {
         PeerPathPreferenceMap::iterator prev_it = path_preference_it++;
         PathPreferenceSM *path_preference_sm = prev_it->second;
         if (path_preference_sm->seen() == false) {
             delete path_preference_sm;
             path_preference_peer_map_.erase(prev_it);
         }
     }
}

PathPreferenceSM* PathPreferenceState::GetSM(const Peer *peer) {
    if (path_preference_peer_map_.find(peer) ==
            path_preference_peer_map_.end()) {
        return NULL;
    }
    return path_preference_peer_map_.find(peer)->second;
}

PathPreferenceRouteListener::PathPreferenceRouteListener(Agent *agent,
    AgentRouteTable *table): agent_(agent), rt_table_(table),
    id_(DBTableBase::kInvalidId), table_delete_ref_(this, table->deleter()),
    deleted_(false) {
    managed_delete_walk_ref_ = table->
        AllocWalker(boost::bind(&PathPreferenceRouteListener::DeleteState,
                                  this, _1, _2),
                    boost::bind(&PathPreferenceRouteListener::Walkdone, this,
                                _1, _2, this));
}

void PathPreferenceRouteListener::Init() {
    id_ = rt_table_->Register(boost::bind(&PathPreferenceRouteListener::Notify,
                                          this,
                                          _1, _2));
}

void PathPreferenceRouteListener::Delete() {
    set_deleted();
    //Managed delete walk need to be done only once.
    if (managed_delete_walk_ref_.get()) {
        rt_table_->WalkAgain(managed_delete_walk_ref_);
    }
}

void PathPreferenceRouteListener::ManagedDelete() {
    Delete();
}

void PathPreferenceRouteListener::Walkdone(DBTable::DBTableWalkRef walk_ref,
                                       DBTableBase *partition,
                                       PathPreferenceRouteListener *state) {
    rt_table_->Unregister(id_);
    table_delete_ref_.Reset(NULL);
    if (walk_ref.get() != NULL)
        (static_cast<DBTable *>(partition))->ReleaseWalker(walk_ref);
    managed_delete_walk_ref_ = NULL;
    delete state;
}

bool PathPreferenceRouteListener::DeleteState(DBTablePartBase *partition,
                                              DBEntryBase *e) {
    PathPreferenceState *state =
        static_cast<PathPreferenceState *>(e->GetState(rt_table_, id_));
    if (state) {
        e->ClearState(rt_table_, id_);
        delete state;
    }
    return true;
}

void PathPreferenceRouteListener::Notify(DBTablePartBase *partition,
                                         DBEntryBase *e) {
    PathPreferenceState *state =
        static_cast<PathPreferenceState *>(e->GetState(rt_table_, id_));
    if (e->IsDeleted()) {
        if (state) {
            e->ClearState(rt_table_, id_);
            delete state;
        }
        return;
    }

    if (deleted_) return;

    AgentRoute *rt = static_cast<AgentRoute *>(e);
    for (Route::PathList::iterator it = rt->GetPathList().begin();
          it != rt->GetPathList().end(); ++it) {
        const AgentPath *path =
             static_cast<const AgentPath *>(it.operator->());
        if (path->peer() == NULL) {
            continue;
        }
        if (path->peer()->GetType() != Peer::LOCAL_VM_PORT_PEER) {
            continue;
        }
        if (path->path_preference().static_preference() == true) {
            return;
        }
    }

    if (!state) {
        state = new PathPreferenceState(agent_, rt);
    }
    bool should_resolve = false;
    state->Process(should_resolve);
    e->SetState(rt_table_, id_, state);

    if (should_resolve) {
        PathPreferenceModule *path_module =
                    agent_->oper_db()->route_preference_module();
        path_module->Resolve();
    }
}

PathPreferenceModule::PathPreferenceModule(Agent *agent):
    agent_(agent), vrf_id_(DBTableBase::kInvalidId),
    work_queue_(TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0,
                boost::bind(&PathPreferenceModule::DequeueEvent, this, _1)) {
    work_queue_.set_name("Path Preference");
}

bool PathPreferenceModule::DequeueEvent(PathPreferenceEventContainer event) {
    const Interface *intf =
        agent_->interface_table()->FindInterface(event.interface_index_);
    if (intf == NULL || (intf->type() != Interface::VM_INTERFACE)) {
        return true;
    }

    const VmInterface *vm_intf = static_cast<const VmInterface *>(intf);

    const VrfEntry *vrf =
        agent_->vrf_table()->FindVrfFromId(event.vrf_index_);
    if (vrf == NULL) {
        return true;
    }

    const PathPreferenceVrfState *state =
        static_cast<const PathPreferenceVrfState *>(
        vrf->GetState(agent_->vrf_table(), vrf_id_));

    if (!state) {
        return true;
    }

    PathPreferenceState *path_preference = NULL;
    PathPreferenceSM *path_preference_sm = NULL;
    const PathPreferenceState *cpath_preference = NULL;

    EvpnRouteKey evpn_key(NULL, vrf->GetName(),
                 event.mac_, event.ip_,
                 EvpnAgentRouteTable::ComputeHostIpPlen(event.ip_),
                 event.vxlan_id_);
    const EvpnRouteEntry *evpn_rt =
              static_cast<const EvpnRouteEntry *>(
              vrf->GetEvpnRouteTable()->FindActiveEntry(&evpn_key));

    if (evpn_rt) {
        cpath_preference = static_cast<const PathPreferenceState *>(
                               evpn_rt->GetState(vrf->GetEvpnRouteTable(),
                               state->evpn_rt_id_));
        if (cpath_preference) {
            path_preference = const_cast<PathPreferenceState *>(cpath_preference);
            path_preference_sm = path_preference->GetSM(vm_intf->peer());
            if (path_preference_sm) {
                EvTrafficSeen ev;
                path_preference_sm->process_event(ev);
            }
        }
    }

    EvpnRouteKey evpn_null_ip_key(NULL, vrf->GetName(), event.mac_,
                                  Ip4Address(0), 32, event.vxlan_id_);
    evpn_rt = static_cast<const EvpnRouteEntry *>(
              vrf->GetEvpnRouteTable()->FindActiveEntry(&evpn_null_ip_key));
    if (evpn_rt) {
        cpath_preference = static_cast<const PathPreferenceState *>(
                               evpn_rt->GetState(vrf->GetEvpnRouteTable(),
                               state->evpn_rt_id_));
        if (cpath_preference) {
            path_preference = const_cast<PathPreferenceState *>(cpath_preference);
            path_preference_sm = path_preference->GetSM(vm_intf->peer());
            if (path_preference_sm) {
                EvTrafficSeen ev;
                path_preference_sm->process_event(ev);
            }
        }
    }

    InetUnicastRouteKey rt_key(NULL, vrf->GetName(), event.ip_, event.plen_);
    const InetUnicastRouteEntry *rt = NULL;
    if (event.ip_.is_v4()) {
        rt = static_cast<const InetUnicastRouteEntry *>(
            vrf->GetInet4UnicastRouteTable()->FindActiveEntry(&rt_key));
    } else if(event.ip_.is_v6()) {
        rt = static_cast<const InetUnicastRouteEntry *>(
            vrf->GetInet6UnicastRouteTable()->FindActiveEntry(&rt_key));
    }
    if (!rt) {
        return true;
    }

    if (event.ip_.is_v4()) {
        cpath_preference = static_cast<const PathPreferenceState *>(
            rt->GetState(vrf->GetInet4UnicastRouteTable(), state->uc_rt_id_));
    } else if(event.ip_.is_v6()) {
        cpath_preference = static_cast<const PathPreferenceState *>(
            rt->GetState(vrf->GetInet6UnicastRouteTable(), state->uc6_rt_id_));
    }
    if (!cpath_preference) {
        return true;
    }

    path_preference = const_cast<PathPreferenceState *>(cpath_preference);
    path_preference_sm = path_preference->GetSM(vm_intf->peer());
    if (path_preference_sm) {
        EvTrafficSeen ev;
        path_preference_sm->process_event(ev);
    }
    return true;
}

void PathPreferenceModule::EnqueueTrafficSeen(IpAddress ip, uint32_t plen,
                                              uint32_t interface_index,
                                              uint32_t vrf_index,
                                              const MacAddress &mac) {
    const Interface *intf =
        agent_->interface_table()->FindInterface(interface_index);
    if (intf == NULL || (intf->type() != Interface::VM_INTERFACE)) {
        return;
    }

    const VmInterface *vm_intf = static_cast<const VmInterface *>(intf);


    //If the local preference is set by config, we dont identify Active
    //node dynamically
    if (vm_intf->local_preference() != 0) {
        return;
    }

    const VrfEntry *vrf = agent_->vrf_table()->FindVrfFromId(vrf_index);
    if (vrf == NULL) {
        return;
    }

    if (vrf == vm_intf->forwarding_vrf()) {
        vrf = vm_intf->vrf();
    }

    InetUnicastRouteEntry *rt = vrf->GetUcRoute(ip);
    EvpnRouteKey key(vm_intf->peer(), vrf->GetName(), mac, ip,
                     EvpnAgentRouteTable::ComputeHostIpPlen(ip),
                     vm_intf->ethernet_tag());
    EvpnRouteEntry *evpn_rt = static_cast<EvpnRouteEntry *>(
        vrf->GetEvpnRouteTable()->FindActiveEntry(&key));
    if (!rt && !evpn_rt) {
        return;
    }

    const AgentPath *path = NULL;
    const AgentPath *evpn_path = NULL;

    if (rt) {
        path = rt->FindPath(vm_intf->peer());
    }
    if (evpn_rt) {
        evpn_path = evpn_rt->FindPath(vm_intf->peer());
    }

    if (!path && !evpn_path) {
        return;
    }

    PathPreferenceEventContainer event;
    if (rt) {
        event.ip_ = rt->addr();
        event.plen_ = rt->plen();
    } else {
        // (0 IP + Mac) event required for EVPN
        event.ip_ = IpAddress();
        event.plen_ = 32;
    }
    event.interface_index_ = interface_index;
    event.vrf_index_ = vrf_index;
    event.mac_ = mac;
    event.vxlan_id_ = vm_intf->ethernet_tag();
    work_queue_.Enqueue(event);

    if (vm_intf->forwarding_vrf() != vm_intf->vrf() &&
        vm_intf->forwarding_vrf()->vrf_id() == vrf_index) {
        event.vrf_index_ = vm_intf->vrf()->vrf_id();
        work_queue_.Enqueue(event);
    }
}

void PathPreferenceModule::VrfNotify(DBTablePartBase *partition,
                                     DBEntryBase *e) {
   const VrfEntry *vrf = static_cast<const VrfEntry *>(e);
   PathPreferenceVrfState *vrf_state =
       static_cast<PathPreferenceVrfState *>(e->GetState(partition->parent(),
                                                          vrf_id_));

   if (vrf->IsDeleted()) {
       if (vrf_state) {
           e->ClearState(partition->parent(), vrf_id_);
           delete vrf_state;
       }
       return;
   }

   if (vrf_state) {
       return;
   }

   PathPreferenceRouteListener *uc_rt_listener =
       new PathPreferenceRouteListener(agent_,
                                       vrf->GetInet4UnicastRouteTable());
   uc_rt_listener->Init();

   PathPreferenceRouteListener *evpn_rt_listener =
       new PathPreferenceRouteListener(agent_,
                                       vrf->GetEvpnRouteTable());
   evpn_rt_listener->Init();

   PathPreferenceRouteListener *uc6_rt_listener =
       new PathPreferenceRouteListener(agent_,
                                       vrf->GetInet6UnicastRouteTable());
   uc6_rt_listener->Init();
   PathPreferenceRouteListener *mpls_rt_listener =
       new PathPreferenceRouteListener(agent_,
                                       vrf->GetInet4MplsUnicastRouteTable());
   mpls_rt_listener->Init();

   vrf_state = new PathPreferenceVrfState(uc_rt_listener->id(),
                                          evpn_rt_listener->id(),
                                          uc6_rt_listener->id(),
                                          mpls_rt_listener->id());

   e->SetState(partition->parent(), vrf_id_, vrf_state);
   return;
}

void PathPreferenceModule::AddUnresolvedPath(PathPreferenceState *sm) {
    unresolved_paths_.insert(sm);
}

void PathPreferenceModule::DeleteUnresolvedPath(PathPreferenceState *sm) {
    std::set<PathPreferenceState *>::iterator it = unresolved_paths_.find(sm);
    if (it != unresolved_paths_.end()) {
        unresolved_paths_.erase(it);
    }
}

void PathPreferenceModule::Resolve() {
    std::set<PathPreferenceState *> tmp = unresolved_paths_;
    unresolved_paths_.clear();

    bool resolve_path = false;
    //Process all the elements
    std::set<PathPreferenceState *>::iterator it = tmp.begin();
    for(; it != tmp.end(); it++) {
        (*it)->Process(resolve_path);
    }
}

void PathPreferenceModule::Init() {
    vrf_id_ = agent_->vrf_table()->Register(
                  boost::bind(&PathPreferenceModule::VrfNotify, this, _1, _2));
}

void PathPreferenceModule::Shutdown() {
    agent_->vrf_table()->Unregister(vrf_id_);
}
