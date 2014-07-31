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
public:
    PathPreferenceSM(Agent *agent, const Peer *peer,
                      Inet4UnicastRouteEntry *rt);
    uint32_t sequence() const {return path_preference_.sequence();}
    uint32_t preference() const {return path_preference_.preference();}
    bool wait_for_traffic() const {return path_preference_.wait_for_traffic();}
    bool ecmp() const {return path_preference_.ecmp();}

    void set_sequence(uint32_t seq_no) {
        path_preference_.set_sequence(seq_no);
    }

    void set_preference(PathPreference::Preference preference) {
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

    bool seen() { return seen_; }
    uint32_t max_sequence() const { return max_sequence_;}
    void Process();
    void Delete();
    void Log(std::string state);
    void EnqueuePathChange();
private:
    Agent *agent_;
    const Peer *peer_;
    Inet4UnicastRouteEntry *rt_;
    PathPreference path_preference_;
    uint32_t max_sequence_;
    bool seen_;
};

//Per Route state machine containing a map for all
//local path state machine data
class PathPreferenceState: public DBState {
public:
    typedef std::map<const Peer *, PathPreferenceSM *> PeerPathPreferenceMap;
    PathPreferenceState(Agent *agent, Inet4UnicastRouteEntry *rt_);
    ~PathPreferenceState();
    void Process();
    PathPreferenceSM *GetSM(const Peer *);
private:
    Agent *agent_;
    Inet4UnicastRouteEntry *rt_;
    PeerPathPreferenceMap path_preference_peer_map_;
};

//Per VM interface state, containing floating IP
//and static route a interface contains
struct PathPreferenceIntfState : public DBState {
    PathPreferenceIntfState(const VmInterface *intf);
    struct RouteAddrList {
        bool operator<(const RouteAddrList &rhs) const;
        bool operator==(const RouteAddrList &rhs) const;
        Ip4Address ip_;
        uint32_t plen_;
        std::string vrf_name_;
        mutable bool seen_;
    };
    void Notify();
    void Insert(RouteAddrList &rt, bool traffic_seen);
    void DeleteOldEntries();
    void UpdateDependentRoute(std::string vrf_name, Ip4Address ip,
                              uint32_t plen, bool traffic_seen,
                              PathPreferenceModule *path_preference_module);
private:
    const VmInterface *intf_;
    RouteAddrList instance_ip_;
    std::set<RouteAddrList> dependent_rt_;
};

struct PathPreferenceVrfState : public DBState {
    PathPreferenceVrfState(Agent *agent, AgentRouteTable *table);
    void Notify(DBTablePartBase *partition, DBEntryBase *e);
    void Init();
    void Delete();
    bool DeleteState(DBTablePartBase *partition, DBEntryBase *e);
    void Walkdone(DBTableBase *partition, PathPreferenceVrfState *state);
    DBTableBase::ListenerId id() const { return id_;}
    void ManagedDelete() { deleted_ = true;}
private:
    Agent *agent_;
    AgentRouteTable *rt_table_;
    DBTableBase::ListenerId id_;
    LifetimeRef<PathPreferenceVrfState> table_delete_ref_;
    bool deleted_;
};

class PathPreferenceModule {
public:
    struct PathPreferenceEventContainer {
        Ip4Address ip_;
        uint32_t plen_;
        uint32_t interface_index_;
        uint32_t vrf_index_;
        boost::intrusive_ptr<const sc::event_base> event;
    };

    PathPreferenceModule(Agent *agent);
    void Init();
    void Shutdown();
    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);
    void IntfNotify(DBTablePartBase *partition, DBEntryBase *e);
    void EnqueueTrafficSeen(Ip4Address ip_, uint32_t plen,
                            uint32_t interface_index, uint32_t vrf_index);
    bool DequeueEvent(PathPreferenceEventContainer e);
    Agent *agent() { return agent_;}
    DBTableBase::ListenerId vrf_id() const { return vrf_id_;}
private:
    Agent *agent_;
    DBTableBase::ListenerId vrf_id_;
    DBTableBase::ListenerId intf_id_;
    WorkQueue<PathPreferenceEventContainer> work_queue_;
};
#endif
