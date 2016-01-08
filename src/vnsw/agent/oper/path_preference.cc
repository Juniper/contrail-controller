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
           if (state_machine->flap_count() == 0) {
               state_machine->DecreaseRetryTimeout();
           }
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
        if (state_machine->preference() == PathPreference::LOW) {
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
}

PathPreferenceSM::~PathPreferenceSM() {
    if (timer_ != NULL) {
        timer_->Cancel();
        TimerManager::DeleteTimer(timer_);
    }
    timer_ = NULL;
}

bool PathPreferenceSM::Retry() {
    flap_count_ = 0;
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

void PathPreferenceSM::DecreaseRetryTimeout() {
    timeout_ = timeout_ / 2;
    if (timeout_ < kMinInterval) {
        timeout_ = kMinInterval;
    }
}

void PathPreferenceSM::UpdateFlapTime() {
    uint64_t time_sec = (UTCTimestampUsec() - last_high_priority_change_at_)/1000;

    //Update last flap time
    last_high_priority_change_at_ = UTCTimestampUsec();
    if (time_sec < timeout_ + kMinInterval) {
        flap_count_++;
    } else {
        flap_count_ = 0;
    }
}

bool PathPreferenceSM::IsPathFlapping() const {
    if (flap_count_ >= kMaxFlapCount) {
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

     //Dont act on notification of derived routes
     if (is_dependent_rt_) {
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

     const AgentPath *local_path =  rt_->FindPath(peer_);
     if (local_path->path_preference().ecmp() == true) {
         path_preference_.set_ecmp(true);
         //If a path is ecmp, just set the priority to HIGH
         process_event(EvActiveActiveMode());
         return;
     }

     //Check if BGP path is present
     for (Route::PathList::iterator it = rt_->GetPathList().begin();
          it != rt_->GetPathList().end(); ++it) {
         const AgentPath *path =
             static_cast<const AgentPath *>(it.operator->());
         if (path == local_path) {
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

     if (ecmp() == true) {
         path_preference_.set_ecmp(local_path->path_preference().ecmp());
         //Route transition from ECMP to non ECMP,
         //move to wait for traffic state
         process_event(EvWaitForTraffic());
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
    PATH_PREFERENCE_TRACE(rt_->vrf()->GetName(), rt_->GetAddressString(),
                          preference(), sequence(), state, timeout());
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
        table = rt_->vrf()->GetEvpnRouteTable();
    } else if (rt_->GetTableType() == Agent::INET4_UNICAST) {
        table = rt_->vrf()->GetInet4UnicastRouteTable();
    } else if (rt_->GetTableType() == Agent::INET6_UNICAST) {
        table = rt_->vrf()->GetInet6UnicastRouteTable();
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

void PathPreferenceIntfState::Insert(RouteAddrList &rt, bool traffic_seen) {
    std::set<RouteAddrList>::const_iterator it = dependent_rt_.find(rt);
    if (it != dependent_rt_.end()) {
        it->seen_ = true;
        return;
    }

    rt.seen_ = true;
    dependent_rt_.insert(rt);
}

void PathPreferenceIntfState::DeleteOldEntries() {
    std::set<RouteAddrList>::const_iterator it = dependent_rt_.begin();
    while (it != dependent_rt_.end()) {
        std::set<RouteAddrList>::const_iterator prev_it = it++;
        if (prev_it->seen_ == false) {
            dependent_rt_.erase(prev_it);
            continue;
        }
        prev_it->seen_ = false;
    }
}

/* Updates the preference of routes dependent on given IP/prefix */
void PathPreferenceIntfState::UpdateDependentRoute(std::string vrf_name,
                                                   Ip4Address ip, uint32_t plen,
                                                   bool traffic_seen,
                                                   PathPreferenceModule
                                                   *path_preference_module) {
    if (instance_ip_.vrf_name_ != vrf_name ||
        instance_ip_.plen_ != plen ||
        instance_ip_.ip_ != ip) {
        return;
    }

    Agent *agent = path_preference_module->agent();
    std::set<RouteAddrList>::const_iterator it = dependent_rt_.begin();
    for(;it != dependent_rt_.end();it++) {
        const VrfEntry *vrf =
            agent->vrf_table()->FindVrfFromName(it->vrf_name_);
        if (vrf == NULL) {
            continue;
        }
        const PathPreferenceVrfState *state =
            static_cast<const PathPreferenceVrfState *>(
            vrf->GetState(agent->vrf_table(),
            path_preference_module->vrf_id()));
        if (!state) {
            continue;
        }

        InetUnicastRouteKey rt_key(NULL, it->vrf_name_, it->ip_.to_v4(),
                                   it->plen_);
        const InetUnicastRouteEntry *rt =
            static_cast<const InetUnicastRouteEntry *>(
            vrf->GetInet4UnicastRouteTable()->FindActiveEntry(&rt_key));
        if (!rt) {
            continue;
        }

        const PathPreferenceState *cpath_preference =
            static_cast<const PathPreferenceState *>(
            rt->GetState(vrf->GetInet4UnicastRouteTable(), state->uc_rt_id_));
        if (!cpath_preference) {
            continue;
        }

        PathPreferenceState *path_preference =
            const_cast<PathPreferenceState *>(cpath_preference);
        PathPreferenceSM *path_preference_sm =
            path_preference->GetSM(intf_->peer());
        if (path_preference_sm && traffic_seen) {
            EvTrafficSeen ev;
            path_preference_sm->process_event(ev);
        }
    }
}

void PathPreferenceIntfState::Notify() {
    //Copy over instance IP
    if (intf_->vrf()) {
        instance_ip_.vrf_name_ = intf_->vrf()->GetName();
    }
    instance_ip_.ip_ = intf_->primary_ip_addr();
    instance_ip_.plen_ = 32;

    //Check if the native IP is active
    bool traffic_seen = true;
    if (intf_->WaitForTraffic() == true) {
        traffic_seen = false;
    }

    //Go thru floating ip
    const VmInterface::FloatingIpSet &fip_list = intf_->floating_ip_list().list_;
    VmInterface::FloatingIpSet::const_iterator it = fip_list.begin();
    for (;it != fip_list.end(); ++it) {
        if (it->floating_ip_.is_v4()) {
            RouteAddrList rt(Address::INET, it->floating_ip_.to_v4(), 32,
                             it->vrf_name_);
            Insert(rt, traffic_seen);
        }
    }

    //Go thru interface static routes
    const VmInterface::ServiceVlanSet &service_vlan_set =
        intf_->service_vlan_list().list_;
    VmInterface::ServiceVlanSet::const_iterator service_vlan_it =
        service_vlan_set.begin();
    for (;service_vlan_it != service_vlan_set.end(); ++service_vlan_it) {
        if (!service_vlan_it->addr_.is_unspecified()) {
            RouteAddrList rt(Address::INET, service_vlan_it->addr_, 32,
                             service_vlan_it->vrf_name_);
            Insert(rt, traffic_seen);
        }
    }

    //Go thru interface static routes
    const VmInterface::StaticRouteSet &static_rt_list =
        intf_->static_route_list().list_;
    VmInterface::StaticRouteSet::const_iterator static_rt_it =
        static_rt_list.begin();
    for (;static_rt_it != static_rt_list.end(); ++static_rt_it) {
        if (static_rt_it->addr_.is_v4()) {
            if (static_rt_it->vrf_ == "") {
                continue;
            }
            RouteAddrList rt(Address::INET, static_rt_it->addr_,
                             static_rt_it->plen_, static_rt_it->vrf_);
            Insert(rt, traffic_seen);
        }
    }
    //Delete all old entries not present in new list
    DeleteOldEntries();
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
    if (rt_->GetTableType() == Agent::EVPN) {
        rt_id = vrf_state->evpn_rt_id_;
    } else if (rt_->GetTableType() == Agent::INET4_UNICAST) {
        rt_id = vrf_state->uc_rt_id_;
    } else if (rt_->GetTableType() == Agent::INET6_UNICAST) {
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
    if (path->path_preference().dependent_ip().is_v4()) {
        table = static_cast<AgentRouteTable *>(
                                 vrf->GetInet4UnicastRouteTable());
    } else if (path->path_preference().dependent_ip().is_v6()) {
        table = static_cast<AgentRouteTable *>(
                                 vrf->GetInet6UnicastRouteTable());
    }
    AgentRoute *rt = static_cast<AgentRoute *>(table->Find(&key));
    if (rt == NULL) {
        return NULL;
    }

    DBTableBase::ListenerId rt_id = DBTableBase::kInvalidId;
    GetRouteListenerId(vrf, rt_id);

    PathPreferenceState *state = static_cast<PathPreferenceState *>(
            rt->GetState(table, rt_id));
    if (state == NULL) {
        return NULL;
    }
    return state->GetSM(path->peer());
}

void PathPreferenceState::Process() {
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

         path_preference_sm->set_seen(true);
         path_preference_sm->Process();

         if (dependent_rt == false) {
             //Resolve path which may not be resolved yet
             path_module->Resolve();
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
}

void PathPreferenceRouteListener::Init() {
    id_ = rt_table_->Register(boost::bind(&PathPreferenceRouteListener::Notify,
                                          this,
                                          _1, _2));
}

void PathPreferenceRouteListener::Delete() {
    set_deleted();
    DBTableWalker *walker = agent_->db()->GetWalker();
    walker->WalkTable(rt_table_, NULL,
                      boost::bind(&PathPreferenceRouteListener::DeleteState,
                                  this, _1, _2),
                      boost::bind(&PathPreferenceRouteListener::Walkdone, this,
                                  _1, this));
}

void PathPreferenceRouteListener::ManagedDelete() {
    Delete();
}

void PathPreferenceRouteListener::Walkdone(DBTableBase *partition,
                                       PathPreferenceRouteListener *state) {
    rt_table_->Unregister(id_);
    table_delete_ref_.Reset(NULL);
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
    state->Process();
    e->SetState(rt_table_, id_, state);
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

    EvpnRouteKey evpn_key(NULL, vrf->GetName(), event.mac_,
                          event.ip_, event.vxlan_id_);
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
                                  Ip4Address(0), event.vxlan_id_);
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
#if 0
    //Enqueue event for same on all dependent routes of interface
    const PathPreferenceIntfState *cintf_state =
        static_cast<const PathPreferenceIntfState *>(
        vm_intf->GetState(agent_->interface_table(), intf_id_));
    PathPreferenceIntfState *intf_state =
        const_cast<PathPreferenceIntfState *>(cintf_state);
    /* Only events with IPv4 IP is enqueued now */
    intf_state->UpdateDependentRoute(vrf->GetName(), event.ip_.to_v4(),
                                     event.plen_, true, this);
#endif
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
    if (vm_intf->local_preference() != VmInterface::INVALID) {
        return;
    }

    const VrfEntry *vrf = agent_->vrf_table()->FindVrfFromId(vrf_index);
    if (vrf == NULL) {
        return;
    }

    InetUnicastRouteEntry *rt = vrf->GetUcRoute(ip);
    EvpnRouteKey key(vm_intf->peer(), vrf->GetName(), mac, ip,
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
    event.ip_ = rt->addr();
    event.plen_ = rt->plen();
    event.interface_index_ = interface_index;
    event.vrf_index_ = vrf_index;
    event.mac_ = mac;
    event.vxlan_id_ = vm_intf->ethernet_tag();
    work_queue_.Enqueue(event);
}

void PathPreferenceModule::VrfNotify(DBTablePartBase *partition,
                                     DBEntryBase *e) {
   const VrfEntry *vrf = static_cast<const VrfEntry *>(e);
   PathPreferenceVrfState *vrf_state =
       static_cast<PathPreferenceVrfState *>(e->GetState(partition->parent(),
                                                          vrf_id_));

   if (vrf->IsDeleted() && vrf_state) {
       e->ClearState(partition->parent(), vrf_id_);
       delete vrf_state;
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

   vrf_state = new PathPreferenceVrfState(uc_rt_listener->id(),
                                          evpn_rt_listener->id(),
                                          uc6_rt_listener->id());

   e->SetState(partition->parent(), vrf_id_, vrf_state);
   return;
}

void PathPreferenceModule::IntfNotify(DBTablePartBase *partition,
                                       DBEntryBase *e) {
    const Interface *intf = static_cast<const Interface *>(e);

    if (intf->type() != Interface::VM_INTERFACE) {
        return;
    }

    PathPreferenceIntfState *intf_state =
        static_cast<PathPreferenceIntfState *>(e->GetState(partition->parent(),
                                                            intf_id_));
    if (intf->IsDeleted()) {
        if (intf_state) {
            e->ClearState(partition->parent(), intf_id_);
            delete intf_state;
        }
        return;
    }

    const VmInterface *vm_intf = static_cast<const VmInterface *>(intf);
    if (!intf_state) {
        intf_state = new PathPreferenceIntfState(vm_intf);
    }
    intf_state->Notify();
    e->SetState(partition->parent(), intf_id_, intf_state);
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

    //Process all the elements
    std::set<PathPreferenceState *>::iterator it = tmp.begin();
    for(; it != tmp.end(); it++) {
        (*it)->Process();
    }
}

void PathPreferenceModule::Init() {
    vrf_id_ = agent_->vrf_table()->Register(
                  boost::bind(&PathPreferenceModule::VrfNotify, this, _1, _2));
#if 0
    intf_id_ = agent_->interface_table()->Register(
                  boost::bind(&PathPreferenceModule::IntfNotify, this, _1, _2));
#endif
}

void PathPreferenceModule::Shutdown() {
    agent_->vrf_table()->Unregister(vrf_id_);
#if 0
    agent_->interface_table()->Unregister(intf_id_);
#endif
}
