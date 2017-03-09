/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_arp_proto_hpp
#define vnsw_agent_arp_proto_hpp

#include "pkt/proto.h"
#include "services/arp_handler.h"
#include "services/arp_entry.h"

#define ARP_TRACE(obj, ...)                                                 \
do {                                                                        \
    Arp##obj::TraceMsg(ArpTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);     \
} while (false)                                                             \

struct ArpVrfState;

class ArpProto : public Proto {
public:
    static const uint16_t kGratRetries = 2;
    static const uint32_t kGratRetryTimeout = 2000;        // milli seconds
    static const uint16_t kMaxRetries = 8;
    static const uint32_t kRetryTimeout = 2000;            // milli seconds
    static const uint32_t kAgingTimeout = (5 * 60 * 1000); // milli seconds

    typedef std::map<ArpKey, ArpEntry *> ArpCache;
    typedef std::pair<ArpKey, ArpEntry *> ArpCachePair;
    typedef std::map<ArpKey, ArpEntry *>::iterator ArpIterator;
    typedef std::set<ArpKey> ArpKeySet;
    typedef std::set<ArpEntry *> ArpEntrySet;
    typedef std::map<ArpKey, ArpEntrySet> GratuitousArpCache;
    typedef std::pair<ArpKey, ArpEntrySet> GratuitousArpCachePair;
    typedef std::map<ArpKey, ArpEntrySet>::iterator GratuitousArpIterator;

    enum ArpMsgType {
        ARP_RESOLVE,
        ARP_DELETE,
        ARP_SEND_GRATUITOUS,
        RETRY_TIMER_EXPIRED,
        AGING_TIMER_EXPIRED,
        GRATUITOUS_TIMER_EXPIRED,
    };

    struct ArpIpc : InterTaskMsg {
        ArpIpc(ArpProto::ArpMsgType msg, ArpKey &akey, InterfaceConstRef itf)
            : InterTaskMsg(msg), key(akey), interface(itf) {}
        ArpIpc(ArpProto::ArpMsgType msg, in_addr_t ip, const VrfEntry *vrf,
               InterfaceConstRef itf) :
            InterTaskMsg(msg), key(ip, vrf), interface(itf) {}

        ArpKey key;
        InterfaceConstRef interface;
    };

    struct ArpStats {
        ArpStats() { Reset(); }
        void Reset() {
            arp_req = arp_replies = arp_gratuitous = 
            resolved = max_retries_exceeded = errors = 0;
            arp_invalid_packets = arp_invalid_interface = arp_invalid_vrf =
                arp_invalid_address = vm_arp_req = 0;
        }

        uint32_t arp_req;
        uint32_t arp_replies;
        uint32_t arp_gratuitous;
        uint32_t resolved;
        uint32_t max_retries_exceeded;
        uint32_t errors;
        uint32_t arp_invalid_packets;
        uint32_t arp_invalid_interface;
        uint32_t arp_invalid_vrf;
        uint32_t arp_invalid_address;
        uint32_t vm_arp_req;
    };

    struct InterfaceArpInfo {
        InterfaceArpInfo() : arp_key_list(), stats() {}
        ArpKeySet arp_key_list;
        ArpStats stats;
    };
    typedef std::map<uint32_t, InterfaceArpInfo> InterfaceArpMap;
    typedef std::pair<uint32_t, InterfaceArpInfo> InterfaceArpPair;

    void Shutdown();
    ArpProto(Agent *agent, boost::asio::io_service &io, bool run_with_vrouter);
    virtual ~ArpProto();

    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);
    bool TimerExpiry(ArpKey &key, uint32_t timer_type, const Interface *itf);

    bool AddArpEntry(ArpEntry *entry);
    bool DeleteArpEntry(ArpEntry *entry);
    ArpEntry *FindArpEntry(const ArpKey &key);
    std::size_t GetArpCacheSize() { return arp_cache_.size(); }
    const ArpCache& arp_cache() { return arp_cache_; }
    const GratuitousArpCache& gratuitous_arp_cache() { return gratuitous_arp_cache_; }
    const InterfaceArpMap& interface_arp_map() { return interface_arp_map_; }

    Interface *ip_fabric_interface() const { return ip_fabric_interface_; }
    uint32_t ip_fabric_interface_index() const {
        return ip_fabric_interface_index_;
    }
    const MacAddress &ip_fabric_interface_mac() const {
        return ip_fabric_interface_mac_;
    }
    void set_ip_fabric_interface(Interface *itf) { ip_fabric_interface_ = itf; }
    void set_ip_fabric_interface_index(uint32_t ind) {
        ip_fabric_interface_index_ = ind;
    }
    void set_ip_fabric_interface_mac(const MacAddress &mac) {
        ip_fabric_interface_mac_ = mac;
    }

    void  AddGratuitousArpEntry(ArpKey &key);
    void DeleteGratuitousArpEntry(ArpEntry *entry);
    ArpEntry* GratuitousArpEntry (const ArpKey &key, const Interface *intf);
    ArpProto::GratuitousArpIterator
        GratuitousArpEntryIterator(const ArpKey &key, bool *key_valid);
    void IncrementStatsArpReq() { arp_stats_.arp_req++; }
    void IncrementStatsArpReplies() { arp_stats_.arp_replies++; }
    void IncrementStatsGratuitous() { arp_stats_.arp_gratuitous++; }
    void IncrementStatsResolved() { arp_stats_.resolved++; }
    void IncrementStatsMaxRetries() { arp_stats_.max_retries_exceeded++; }
    void IncrementStatsErrors() { arp_stats_.errors++; }
    void IncrementStatsVmArpReq() { arp_stats_.vm_arp_req++; }
    void IncrementStatsInvalidPackets() {
        IncrementStatsErrors();
        arp_stats_.arp_invalid_packets++;
    }
    void IncrementStatsInvalidInterface() {
        IncrementStatsErrors();
        arp_stats_.arp_invalid_interface++;
    }
    void IncrementStatsInvalidVrf() {
        IncrementStatsErrors();
        arp_stats_.arp_invalid_vrf++;
    }
    void IncrementStatsInvalidAddress() {
        IncrementStatsErrors();
        arp_stats_.arp_invalid_address++;
    }
    const ArpStats &GetStats() const { return arp_stats_; }
    void ClearStats() { arp_stats_.Reset(); }

    void IncrementStatsArpRequest(uint32_t idx);
    void IncrementStatsArpReply(uint32_t idx);
    void IncrementStatsResolved(uint32_t idx);
    InterfaceArpInfo& ArpMapIndexToEntry(uint32_t idx);
    uint32_t ArpRequestStatsCounter(uint32_t idx);
    uint32_t ArpReplyStatsCounter(uint32_t idx);
    uint32_t ArpResolvedStatsCounter(uint32_t idx);
    void ClearInterfaceArpStats(uint32_t idx);

    uint16_t max_retries() const { return max_retries_; }
    uint32_t retry_timeout() const { return retry_timeout_; }
    uint32_t aging_timeout() const { return aging_timeout_; }
    void set_max_retries(uint16_t retries) { max_retries_ = retries; }
    void set_retry_timeout(uint32_t timeout) { retry_timeout_ = timeout; }
    void set_aging_timeout(uint32_t timeout) { aging_timeout_ = timeout; }
    void SendArpIpc(ArpProto::ArpMsgType type, in_addr_t ip,
                    const VrfEntry *vrf, InterfaceConstRef itf);
    bool ValidateAndClearVrfState(VrfEntry *vrf, const ArpVrfState *vrf_state);
    ArpIterator FindUpperBoundArpEntry(const ArpKey &key);
    ArpIterator FindLowerBoundArpEntry(const ArpKey &key);

    DBTableBase::ListenerId vrf_table_listener_id() const {
        return vrf_table_listener_id_;
    }
private:
    void VrfNotify(DBTablePartBase *part, DBEntryBase *entry);
    void NextHopNotify(DBEntryBase *entry);
    void InterfaceNotify(DBEntryBase *entry);
    void SendArpIpc(ArpProto::ArpMsgType type, ArpKey &key,
                    InterfaceConstRef itf);
    ArpProto::ArpIterator DeleteArpEntry(ArpProto::ArpIterator iter);

    ArpCache arp_cache_;
    ArpStats arp_stats_;
    GratuitousArpCache gratuitous_arp_cache_;
    bool run_with_vrouter_;
    uint32_t ip_fabric_interface_index_;
    MacAddress ip_fabric_interface_mac_;
    Interface *ip_fabric_interface_;
    DBTableBase::ListenerId vrf_table_listener_id_;
    DBTableBase::ListenerId interface_table_listener_id_;
    DBTableBase::ListenerId nexthop_table_listener_id_;
    InterfaceArpMap interface_arp_map_;

    uint16_t max_retries_;
    uint32_t retry_timeout_;   // milli seconds
    uint32_t aging_timeout_;   // milli seconds

    DISALLOW_COPY_AND_ASSIGN(ArpProto);
};

//Stucture used to retry ARP queries when a particular route is in
//backup state.
class ArpPathPreferenceState {
public:
    static const uint32_t kMaxRetry = 30 * 5; //retries upto 5 minutes,
                                              //30 tries/per minutes
    static const uint32_t kTimeout = 2000;
    typedef std::map<uint32_t, uint32_t> WaitForTrafficIntfMap;
    typedef std::set<uint32_t> ArpTransmittedIntfMap;

    ArpPathPreferenceState(ArpVrfState *state, uint32_t vrf_id,
                           const IpAddress &vm_ip, uint8_t plen);
    ~ArpPathPreferenceState();

    bool SendArpRequest();
    bool SendArpRequest(WaitForTrafficIntfMap &wait_for_traffic_map,
                        ArpTransmittedIntfMap &arp_transmitted_intf_map);
    void SendArpRequestForAllIntf(const AgentRoute *route);
    void StartTimer();

    ArpVrfState* vrf_state() {
        return vrf_state_;
    }

    const IpAddress& ip() const {
        return vm_ip_;
    }

    bool IntfPresentInIpMap(uint32_t id) {
        if (l3_wait_for_traffic_map_.find(id) ==
                l3_wait_for_traffic_map_.end()) {
            return false;
        }
        return true;
    }

    bool IntfPresentInEvpnMap(uint32_t id) {
        if (evpn_wait_for_traffic_map_.find(id) ==
                evpn_wait_for_traffic_map_.end()) {
            return false;
        }
        return true;
    }

    uint32_t IntfRetryCountInIpMap(uint32_t id) {
        return l3_wait_for_traffic_map_[id];
    }

    uint32_t IntfRetryCountInEvpnMap(uint32_t id) {
        return evpn_wait_for_traffic_map_[id];
    }

private:
    friend void intrusive_ptr_add_ref(ArpPathPreferenceState *aps);
    friend void intrusive_ptr_release(ArpPathPreferenceState *aps);
    ArpVrfState *vrf_state_;
    Timer *arp_req_timer_;
    uint32_t vrf_id_;
    IpAddress vm_ip_;
    uint8_t plen_;
    IpAddress gw_ip_;
    WaitForTrafficIntfMap l3_wait_for_traffic_map_;
    WaitForTrafficIntfMap evpn_wait_for_traffic_map_;
    tbb::atomic<int> refcount_;
};

typedef boost::intrusive_ptr<ArpPathPreferenceState> ArpPathPreferenceStatePtr;

void intrusive_ptr_add_ref(ArpPathPreferenceState *aps);
void intrusive_ptr_release(ArpPathPreferenceState *aps);

struct ArpVrfState : public DBState {
public:
    typedef std::map<const IpAddress,
                     ArpPathPreferenceState*> ArpPathPreferenceStateMap;
    typedef std::pair<const IpAddress,
                      ArpPathPreferenceState*> ArpPathPreferenceStatePair;
    ArpVrfState(Agent *agent, ArpProto *proto, VrfEntry *vrf,
                AgentRouteTable *table, AgentRouteTable *evpn_table);
    ~ArpVrfState();
    void RouteUpdate(DBTablePartBase *part, DBEntryBase *entry);
    void EvpnRouteUpdate(DBTablePartBase *part, DBEntryBase *entry);
    void ManagedDelete() { deleted = true;}
    void Delete();
    bool DeleteRouteState(DBTablePartBase *part, DBEntryBase *entry);
    bool DeleteEvpnRouteState(DBTablePartBase *part, DBEntryBase *entry);
    bool PreWalkDone(DBTableBase *partition);
    static void WalkDone(DBTableBase *partition, ArpVrfState *state);

    ArpPathPreferenceState* Locate(const IpAddress &ip);
    void Erase(const IpAddress &ip);
    ArpPathPreferenceState* Get(const IpAddress ip) {
        return arp_path_preference_map_[ip];
    }

    bool l3_walk_completed() const {
        return l3_walk_completed_;
    }

    bool evpn_walk_completed() const {
        return evpn_walk_completed_;
    }

    Agent *agent;
    ArpProto *arp_proto;
    VrfEntry *vrf;
    AgentRouteTable *rt_table;
    AgentRouteTable *evpn_rt_table;
    DBTableBase::ListenerId route_table_listener_id;
    DBTableBase::ListenerId evpn_route_table_listener_id;
    LifetimeRef<ArpVrfState> table_delete_ref;
    LifetimeRef<ArpVrfState> evpn_table_delete_ref;
    bool deleted;
    DBTableWalker::WalkId walk_id_;
    DBTableWalker::WalkId evpn_walk_id_;
    ArpPathPreferenceStateMap arp_path_preference_map_;
    bool l3_walk_completed_;
    bool evpn_walk_completed_;
    friend class ArpProto;
};

class ArpDBState : public DBState {
public:
    static const uint32_t kMaxRetry = 30 * 5; //retries upto 5 minutes,
                                              //30 tries/per minutes
    static const uint32_t kTimeout = 2000;
    typedef std::map<uint32_t, uint32_t> WaitForTrafficIntfMap;

    ArpDBState(ArpVrfState *vrf_state, uint32_t vrf_id,
               IpAddress vm_ip_addr, uint8_t plen);
    ~ArpDBState();
    void Update(const AgentRoute *route);
    void UpdateArpRoutes(const InetUnicastRouteEntry *route);
    void Delete(const InetUnicastRouteEntry *rt);
private:
    ArpVrfState *vrf_state_;
    SecurityGroupList sg_list_;
    bool policy_;
    bool resolve_route_;
    VnListType vn_list_;
    ArpPathPreferenceStatePtr arp_path_preference_state_;
};
#endif // vnsw_agent_arp_proto_hpp
