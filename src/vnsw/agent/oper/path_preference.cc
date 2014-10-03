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
        sc::custom_reaction<EvActiveActiveMode>
    > reactions;

    WaitForTraffic(my_context ctx) : my_base(ctx) {
        PathPreferenceSM *state_machine = &context<PathPreferenceSM>();
        if (state_machine->wait_for_traffic() == false) {
            state_machine->set_wait_for_traffic(true);
            state_machine->set_preference(PathPreference::LOW);
            state_machine->EnqueuePathChange();
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
};

struct TrafficSeen : sc::state<TrafficSeen, PathPreferenceSM> {
    typedef mpl::list<
        sc::custom_reaction<EvTrafficSeen>,
        sc::custom_reaction<EvSeqChange>,
        sc::custom_reaction<EvWaitForTraffic>,
        sc::custom_reaction<EvActiveActiveMode>
    > reactions;

    TrafficSeen(my_context ctx) : my_base(ctx) {
        PathPreferenceSM *state_machine = &context<PathPreferenceSM>();
        //Enqueue a route change
        if (state_machine->wait_for_traffic() == true) {
            uint32_t seq = state_machine->max_sequence();
           state_machine->set_wait_for_traffic(false);
           seq++;
           state_machine->set_max_sequence(seq);
           state_machine->set_sequence(seq);
           state_machine->set_preference(PathPreference::HIGH);
           state_machine->EnqueuePathChange();
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
            return transit<WaitForTraffic>();
        }
        return discard_event();
    }

    sc::result react(const EvActiveActiveMode &event) {
        return transit<ActiveActiveState>();
    }
};

struct ActiveActiveState : sc::state<ActiveActiveState, PathPreferenceSM> {
    typedef mpl::list<
        sc::custom_reaction<EvTrafficSeen>,
        sc::custom_reaction<EvSeqChange>,
        sc::custom_reaction<EvWaitForTraffic>,
        sc::custom_reaction<EvActiveActiveMode>
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
};



PathPreferenceSM::PathPreferenceSM(Agent *agent, const Peer *peer,
    InetUnicastRouteEntry *rt): agent_(agent), peer_(peer), rt_(rt),
    path_preference_(0, PathPreference::LOW, false, false), max_sequence_(0) {
    initiate();
    process_event(EvStart());
}

void PathPreferenceSM::Process() {
     uint32_t max_sequence = 0;
     const AgentPath *best_path = NULL;

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
             best_path->nexthop(agent_) != local_path->nexthop(agent_)) {
         //Control node chosen  path and local path are different
         //move to wait for traffic state
         process_event(EvWaitForTraffic());
     } else if (ecmp() == true) {
         path_preference_.set_ecmp(local_path->path_preference().ecmp());
         //Route transition from ECMP to non ECMP,
         //move to wait for traffic state
         process_event(EvWaitForTraffic());
     }
}

void PathPreferenceSM::Log(std::string state) {
    PATH_PREFERENCE_TRACE(rt_->vrf()->GetName(), rt_->GetAddressString(),
                          preference(), sequence(), state);
}

void PathPreferenceSM::EnqueuePathChange() {
    std::string vrf_name = rt_->vrf()->GetName();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    InetUnicastRouteKey *rt_key =
        new InetUnicastRouteKey(peer_, vrf_name, rt_->addr(), rt_->plen());
    rt_key->sub_op_ = AgentKey::RESYNC;
    req.key.reset(rt_key);
    req.data.reset(new PathPreferenceData(path_preference_));
    AgentRouteTable *table =
        agent_->vrf_table()->GetInet4UnicastRouteTable(vrf_name);
    if (table) {
        table->Enqueue(&req);
    }
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

void PathPreferenceIntfState::UpdateDependentRoute(std::string vrf_name,
                                                    IpAddress ip, uint32_t plen,
                                                    bool traffic_seen,
                                                    PathPreferenceModule
                                                    *path_preference_module) {
    if (ip.is_v6()) {
        //TODO:IPv6 handling
        return;
    }
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
            rt->GetState(vrf->GetInet4UnicastRouteTable(), state->id()));
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
    instance_ip_.ip_ = intf_->ip_addr();
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
        RouteAddrList rt;
        rt.vrf_name_ = it->vrf_name_;
        if (it->floating_ip_.is_v4()) {
            rt.plen_ = 32;
            rt.ip_ = it->floating_ip_.to_v4();
            Insert(rt, traffic_seen);
        } else if (it->floating_ip_.is_v6()) {
            rt.plen_ = 128;
            rt.ip_ = it->floating_ip_.to_v6();
            Insert(rt, traffic_seen);
        }
    }

    //Go thru interface static routes
    const VmInterface::ServiceVlanSet &service_vlan_set =
        intf_->service_vlan_list().list_;
    VmInterface::ServiceVlanSet::const_iterator service_vlan_it =
        service_vlan_set.begin();
    for (;service_vlan_it != service_vlan_set.end(); ++service_vlan_it) {
        RouteAddrList rt;
        rt.plen_ = service_vlan_it->plen_;
        rt.ip_ = service_vlan_it->addr_;
        rt.vrf_name_ = service_vlan_it->vrf_name_;
        Insert(rt, traffic_seen);
    }

    //Go thru interface static routes
    const VmInterface::StaticRouteSet &static_rt_list =
        intf_->static_route_list().list_;
    VmInterface::StaticRouteSet::const_iterator static_rt_it =
        static_rt_list.begin();
    for (;static_rt_it != static_rt_list.end(); ++static_rt_it) {
        RouteAddrList rt;
        if (static_rt_it->vrf_ == "") {
            continue;
        }
        rt.plen_ = static_rt_it->plen_;
        rt.ip_ = static_rt_it->addr_;
        rt.vrf_name_ = static_rt_it->vrf_;
        Insert(rt, traffic_seen);
    }
    //Delete all old entries not present in new list
    DeleteOldEntries();
}

PathPreferenceState::PathPreferenceState(Agent *agent,
    InetUnicastRouteEntry *rt): agent_(agent), rt_(rt) {
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
}

void PathPreferenceState::Process() {
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
                 new PathPreferenceSM(agent_, path->peer(), rt_);
             path_preference_peer_map_.insert(
                std::pair<const Peer *, PathPreferenceSM *>
                (path->peer(), path_preference_sm));
         } else {
             path_preference_sm =
                 path_preference_peer_map_.find(path->peer())->second;
         }
         path_preference_sm->set_seen(true);
         path_preference_sm->Process();
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

PathPreferenceVrfState::PathPreferenceVrfState(Agent *agent,
    AgentRouteTable *table): agent_(agent), rt_table_(table),
    id_(DBTableBase::kInvalidId), table_delete_ref_(this, table->deleter()),
    deleted_(false) {
}

void PathPreferenceVrfState::Init() {
    id_ = rt_table_->Register(boost::bind(&PathPreferenceVrfState::Notify, this,
                                          _1, _2));
}

void PathPreferenceVrfState::Delete() {
    deleted_ = true;
    DBTableWalker *walker = agent_->db()->GetWalker();
    walker->WalkTable(rt_table_, NULL,
                      boost::bind(&PathPreferenceVrfState::DeleteState,
                                  this, _1, _2),
                      boost::bind(&PathPreferenceVrfState::Walkdone, this,
                                  _1, this));
}

void PathPreferenceVrfState::Walkdone(DBTableBase *partition,
                                       PathPreferenceVrfState *state) {
    rt_table_->Unregister(id_);
    table_delete_ref_.Reset(NULL);
    delete state;
}

bool PathPreferenceVrfState::DeleteState(DBTablePartBase *partition,
                                          DBEntryBase *e) {
    PathPreferenceState *state =
        static_cast<PathPreferenceState *>(e->GetState(rt_table_, id_));
    if (state) {
        e->ClearState(rt_table_, id_);
        delete state;
    }
    return true;
}

void PathPreferenceVrfState::Notify(DBTablePartBase *partition, 
                                     DBEntryBase *e) {
    PathPreferenceState *state =
        static_cast<PathPreferenceState *>(e->GetState(rt_table_, id_));
    if (e->IsDeleted()) {
        e->ClearState(rt_table_, id_);
        delete state;
        return;
    }

    InetUnicastRouteEntry *rt =
                static_cast<InetUnicastRouteEntry *>(e);
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

    InetUnicastRouteKey rt_key(NULL, vrf->GetName(), event.ip_, event.plen_);
    const InetUnicastRouteEntry *rt =
        static_cast<const InetUnicastRouteEntry *>(
        vrf->GetInet4UnicastRouteTable()->FindActiveEntry(&rt_key));
    if (!rt) {
        return true;
    }

    const PathPreferenceState *cpath_preference =
        static_cast<const PathPreferenceState *>(
        rt->GetState(vrf->GetInet4UnicastRouteTable(), state->id()));
    if (!cpath_preference) {
        return true;
    }

    PathPreferenceState *path_preference =
        const_cast<PathPreferenceState *>(cpath_preference);
    PathPreferenceSM *path_preference_sm =
        path_preference->GetSM(vm_intf->peer());
    if (path_preference_sm) {
        EvTrafficSeen ev;
        path_preference_sm->process_event(ev);
    }

    //Enqueue event for same on all dependent routes of interface
    const PathPreferenceIntfState *cintf_state =
        static_cast<const PathPreferenceIntfState *>(
        vm_intf->GetState(agent_->interface_table(), intf_id_));
    PathPreferenceIntfState *intf_state =
        const_cast<PathPreferenceIntfState *>(cintf_state);
    intf_state->UpdateDependentRoute(vrf->GetName(), event.ip_,
                                     event.plen_, true, this);
    return true;
}

void PathPreferenceModule::EnqueueTrafficSeen(Ip4Address ip, uint32_t plen,
                                              uint32_t interface_index,
                                              uint32_t vrf_index) {
    const Interface *intf =
        agent_->interface_table()->FindInterface(interface_index);
    if (intf == NULL || (intf->type() != Interface::VM_INTERFACE)) {
        return;
    }

    const VmInterface *vm_intf = static_cast<const VmInterface *>(intf);
    const VrfEntry *vrf = agent_->vrf_table()->FindVrfFromId(vrf_index);
    if (vrf == NULL) {
        return;
    }

    InetUnicastRouteEntry *rt = vrf->GetUcRoute(ip);
    if (!rt) {
        return;
    }

    const AgentPath *path = rt->FindPath(vm_intf->peer());
    if (!path|| path->path_preference().wait_for_traffic() == false) {
        return;
    }

    PathPreferenceEventContainer event;
    event.ip_ = rt->addr();
    event.plen_ = rt->plen();
    event.interface_index_ = interface_index;
    event.vrf_index_ = vrf_index;
    work_queue_.Enqueue(event);
}

void PathPreferenceModule::VrfNotify(DBTablePartBase *partition,
                                      DBEntryBase *e) {
   const VrfEntry *vrf = static_cast<const VrfEntry *>(e);
   PathPreferenceVrfState *vrf_state =
       static_cast<PathPreferenceVrfState *>(e->GetState(partition->parent(),
                                                          vrf_id_));

   if (vrf->IsDeleted() && vrf_state) {
       vrf_state->Delete();
       e->ClearState(partition->parent(), vrf_id_);
       return;
   }

   if (vrf_state) {
       return;
   }

   vrf_state = new PathPreferenceVrfState(agent_,
                                           vrf->GetInet4UnicastRouteTable());
   vrf_state->Init();
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
    if (intf->IsDeleted() && intf_state) {
        e->ClearState(partition->parent(), intf_id_);
        delete intf_state;
        return;
    }

    const VmInterface *vm_intf = static_cast<const VmInterface *>(intf);
    if (!intf_state) {
        intf_state = new PathPreferenceIntfState(vm_intf);
    }
    intf_state->Notify();
    e->SetState(partition->parent(), intf_id_, intf_state);
}

void PathPreferenceModule::Init() {
    vrf_id_ = agent_->vrf_table()->Register(
                  boost::bind(&PathPreferenceModule::VrfNotify, this, _1, _2));
    intf_id_ = agent_->interface_table()->Register(
                  boost::bind(&PathPreferenceModule::IntfNotify, this, _1, _2));
}

void PathPreferenceModule::Shutdown() {
    agent_->vrf_table()->Unregister(vrf_id_);
    agent_->interface_table()->Unregister(intf_id_);
}
