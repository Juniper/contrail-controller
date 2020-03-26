/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __ROUTE_PREFERENCE_H__
#define __ROUTE_PREFERENCE_H__

#include <boost/statechart/custom_reaction.hpp>
#include <boost/statechart/event.hpp>
#include <boost/statechart/simple_state.hpp>
#include <boost/statechart/state.hpp>
#include <boost/statechart/state_machine.hpp>

namespace sc = boost::statechart;
struct Init;
struct WaitForTraffic;
struct TrafficSeen;
struct ActiveActiveState;
class PathPreferenceModule;

#define PATH_PREFERENCE_TRACE(...)                                   \
do {                                                                      \
   PathPreferenceTrace::TraceMsg(PathPreferenceTraceBuf, __FILE__, __LINE__,\
                            ##__VA_ARGS__);                               \
} while (false) \

//Per Path state machine to determine preference of a path based on
//traffic(GARP or flow from VM)
class PathPreferenceSM:
    public sc::state_machine<PathPreferenceSM, Init> {
    typedef DependencyList<PathPreferenceSM, PathPreferenceSM> PathDependencyList;
public:
    static const uint32_t kMinInterval = 4 * 1000;
    static const uint32_t kMaxInterval = 32 * 1000;
    static const uint32_t kMaxFlapCount = 5;
    PathPreferenceSM(Agent *agent, const Peer *peer,
                     AgentRoute *rt, bool dependent_rt,
                     const PathPreference &pref);
    ~PathPreferenceSM();
    uint32_t sequence() const {return path_preference_.sequence();}
    uint32_t preference() const {return path_preference_.preference();}
    bool wait_for_traffic() const {return path_preference_.wait_for_traffic();}
    bool ecmp() const {return path_preference_.ecmp();}
    uint32_t timeout() const { return timeout_;}

    uint64_t last_stable_high_priority_change_at() const {
        return last_stable_high_priority_change_at_;
    }
    uint32_t flap_count() const { return flap_count_;}

    bool is_dependent_rt() const { return is_dependent_rt_;}
    void set_sequence(uint32_t seq_no) {
        path_preference_.set_sequence(seq_no);
    }

    void set_preference(uint32_t preference) {
        path_preference_.set_preference(preference);
    }

    void set_wait_for_traffic(bool wait_for_traffic) {
        path_preference_.set_wait_for_traffic(wait_for_traffic);
    }

    void set_ecmp(bool ecmp) {
        path_preference_.set_ecmp(ecmp);
    }

    void set_seen(bool seen) {
        seen_ = seen;
    }

    void set_max_sequence(uint32_t seq) {
        max_sequence_ = seq;
    }

    void set_timeout(uint32_t timeout) {
        timeout_ = timeout;
    }

    void set_last_stable_high_priority_change_at(uint64_t timestamp) {
        last_stable_high_priority_change_at_ = timestamp;
    }

    void set_dependent_rt(PathPreferenceSM *sm) {
        dependent_rt_.reset(sm);
    }

    void set_is_dependent_rt(bool dependent_path) {
        is_dependent_rt_ = dependent_path;
    }

    void set_dependent_ip(const IpAddress &ip) {
        path_preference_.set_dependent_ip(ip);
    }

    IpAddress dependent_ip() {
        return path_preference_.dependent_ip();
    }

    bool IsFlap() const;
    bool seen() { return seen_; }
    uint32_t max_sequence() const { return max_sequence_;}
    void Process();
    void Delete();
    void Log(std::string state);
    void EnqueuePathChange();
    bool Retry();
    void StartRetryTimer();
    void CancelRetryTimer();
    bool RetryTimerRunning();
    void IncreaseRetryTimeout();
    void DecreaseRetryTimeout();
    bool IsPathFlapping() const;
    bool IsPathStable() const;
    void UpdateFlapTime();
    void UpdateDependentRoute();
private:
    Agent *agent_;
    const Peer *peer_;
    AgentRoute *rt_;
    PathPreference path_preference_;
    uint32_t max_sequence_;
    bool seen_;
    Timer *timer_;
    uint32_t timeout_;
    uint64_t last_stable_high_priority_change_at_;
    uint64_t backoff_timer_fired_time_;
    uint32_t flap_count_;
    bool is_dependent_rt_;
    DependencyRef<PathPreferenceSM, PathPreferenceSM> dependent_rt_;
    DEPENDENCY_LIST(PathPreferenceSM, PathPreferenceSM, dependent_routes_);
};

//Per Route state machine containing a map for all
//local path state machine data
class PathPreferenceState: public DBState {
public:
    typedef std::map<const Peer *, PathPreferenceSM *> PeerPathPreferenceMap;
    PathPreferenceState(Agent *agent, AgentRoute *rt_);
    ~PathPreferenceState();
    void Process(bool &should_resolve);
    PathPreferenceSM *GetSM(const Peer *);
    PathPreferenceSM* GetDependentPath(const AgentPath *path) const;
private:
    bool GetRouteListenerId(const VrfEntry *vrf,
                            const Agent::RouteTableType &table,
                            DBTableBase::ListenerId &rt_id) const;
    Agent *agent_;
    AgentRoute *rt_;
    PeerPathPreferenceMap path_preference_peer_map_;
};

//Per VM interface state, containing floating IP
//and static route a interface contains
struct PathPreferenceIntfState : public DBState {
    PathPreferenceIntfState(const VmInterface *intf);
    struct RouteAddrList {
        RouteAddrList();
        RouteAddrList(const Address::Family &family, const IpAddress &ip,
                      uint32_t plen, const std::string &vrf);
        bool operator<(const RouteAddrList &rhs) const;
        bool operator==(const RouteAddrList &rhs) const;

        Address::Family family_;
        IpAddress ip_;
        uint32_t plen_;
        std::string vrf_name_;
        mutable bool seen_;
    };
    uint32_t DependentRouteListSize() const { return dependent_rt_.size(); }
private:
    const VmInterface *intf_;
    RouteAddrList instance_ip_;
    std::set<RouteAddrList> dependent_rt_;
};

struct PathPreferenceVrfState: public DBState {
    PathPreferenceVrfState(DBTableBase::ListenerId uc_rt_id,
                           DBTableBase::ListenerId evpn_rt_id,
                           DBTableBase::ListenerId uc6_rt_id,
                           DBTableBase::ListenerId mpls_rt_id):
        uc_rt_id_(uc_rt_id), evpn_rt_id_(evpn_rt_id),
        uc6_rt_id_(uc6_rt_id),mpls_rt_id_(mpls_rt_id) {}
    DBTableBase::ListenerId uc_rt_id_;
    DBTableBase::ListenerId evpn_rt_id_;
    DBTableBase::ListenerId uc6_rt_id_;
    DBTableBase::ListenerId mpls_rt_id_;
};

struct PathPreferenceRouteListener : public DBState {
    PathPreferenceRouteListener(Agent *agent, AgentRouteTable *table);
    virtual void Delete();

    void Notify(DBTablePartBase *partition, DBEntryBase *e);
    void Init();
    bool DeleteState(DBTablePartBase *partition, DBEntryBase *e);
    void Walkdone(DBTable::DBTableWalkRef walk_ref, DBTableBase *partition,
                  PathPreferenceRouteListener *state);
    DBTableBase::ListenerId id() const { return id_;}
    void ManagedDelete();
    void set_deleted() {deleted_ = true;}
    bool deleted() const {return deleted_;}
private:
    Agent *agent_;
    AgentRouteTable *rt_table_;
    DBTableBase::ListenerId id_;
    LifetimeRef<PathPreferenceRouteListener> table_delete_ref_;
    bool deleted_;
    DBTable::DBTableWalkRef managed_delete_walk_ref_;
};

class PathPreferenceModule {
public:
    struct PathPreferenceEventContainer {
        IpAddress ip_;
        uint32_t plen_;
        MacAddress mac_;
        uint32_t interface_index_;
        uint32_t vrf_index_;
        uint32_t vxlan_id_;
        boost::intrusive_ptr<const sc::event_base> event;
    };

    PathPreferenceModule(Agent *agent);
    void Init();
    void Shutdown();
    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);
    void EnqueueTrafficSeen(IpAddress ip, uint32_t plen,
                            uint32_t interface_index, uint32_t vrf_index,
                            const MacAddress &mac);
    bool DequeueEvent(PathPreferenceEventContainer e);
    Agent *agent() { return agent_;}
    DBTableBase::ListenerId vrf_id() const { return vrf_id_;}
    DBTableBase::ListenerId intf_id() const { return intf_id_;}
    void AddUnresolvedPath(PathPreferenceState *sm);
    void DeleteUnresolvedPath(PathPreferenceState *sm);
    void Resolve();
private:
    Agent *agent_;
    DBTableBase::ListenerId vrf_id_;
    DBTableBase::ListenerId intf_id_;
    WorkQueue<PathPreferenceEventContainer> work_queue_;
    std::set<PathPreferenceState *> unresolved_paths_;
};
#endif
