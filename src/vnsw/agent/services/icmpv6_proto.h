/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_icmpv6_proto_h
#define vnsw_agent_icmpv6_proto_h

#include "pkt/proto.h"
#include "services/icmpv6_handler.h"

#define ICMP_PKT_SIZE 1024
#define IPV6_ALL_NODES_ADDRESS "FF02::1"
#define IPV6_ALL_ROUTERS_ADDRESS "FF02::2"
#define PKT0_LINKLOCAL_ADDRESS "FE80::5E00:0100"

#define ICMPV6_TRACE(obj, arg)                                               \
do {                                                                         \
    std::ostringstream _str;                                                 \
    _str << arg;                                                             \
    Icmpv6##obj::TraceMsg(Icmpv6TraceBuf, __FILE__, __LINE__, _str.str());   \
} while (false)                                                              \

class Icmpv6VrfState;

class Icmpv6Proto : public Proto {
public:
    static const uint32_t kRouterAdvertTimeout = 30000; // milli seconds

    struct Icmpv6Stats {
        Icmpv6Stats() { Reset(); }
        void Reset() {
            icmpv6_router_solicit_ = icmpv6_router_advert_ = 0;
            icmpv6_ping_request_ = icmpv6_ping_response_ = icmpv6_drop_ = 0;
            icmpv6_neighbor_solicit_ = icmpv6_neighbor_advert_solicited_ = 0;
            icmpv6_neighbor_advert_unsolicited_ = 0;
        }

        uint32_t icmpv6_router_solicit_;
        uint32_t icmpv6_router_advert_;
        uint32_t icmpv6_ping_request_;
        uint32_t icmpv6_ping_response_;
        uint32_t icmpv6_drop_;
        uint32_t icmpv6_neighbor_solicit_;
        uint32_t icmpv6_neighbor_advert_solicited_;
        uint32_t icmpv6_neighbor_advert_unsolicited_;
    };

    typedef std::map<VmInterface *, Icmpv6Stats> VmInterfaceMap;
    typedef std::pair<VmInterface *, Icmpv6Stats> VmInterfacePair;

    void Shutdown();
    Icmpv6Proto(Agent *agent, boost::asio::io_service &io);
    virtual ~Icmpv6Proto();
    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);
    void VrfNotify(DBTablePartBase *part, DBEntryBase *entry);
    void VnNotify(DBEntryBase *entry);
    void InterfaceNotify(DBEntryBase *entry);

    const VmInterfaceMap &vm_interfaces() { return vm_interfaces_; }

    void IncrementStatsRouterSolicit(VmInterface *vmi);
    void IncrementStatsRouterAdvert(VmInterface *vmi);
    void IncrementStatsPingRequest(VmInterface *vmi);
    void IncrementStatsPingResponse(VmInterface *vmi);
    void IncrementStatsDrop() { stats_.icmpv6_drop_++; }
    void IncrementStatsNeighborAdvertSolicited(VmInterface *vmi);
    void IncrementStatsNeighborAdvertUnSolicited(VmInterface *vmi);
    void IncrementStatsNeighborSolicit(VmInterface *vmi);
    const Icmpv6Stats &GetStats() const { return stats_; }
    Icmpv6Stats *VmiToIcmpv6Stats(VmInterface *i);
    void ClearStats() { stats_.Reset(); }
    void ValidateAndClearVrfState(VrfEntry *vrf);
    Icmpv6VrfState *CreateAndSetVrfState(VrfEntry *vrf);

private:
    Timer *timer_;
    Icmpv6Stats stats_;
    VmInterfaceMap vm_interfaces_;
    // handler to send router advertisements and neighbor solicits
    boost::scoped_ptr<Icmpv6Handler> icmpv6_handler_;
    DBTableBase::ListenerId vn_table_listener_id_;
    DBTableBase::ListenerId vrf_table_listener_id_;
    DBTableBase::ListenerId interface_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(Icmpv6Proto);
};

class Icmpv6VrfState : public DBState {
public:
    Icmpv6VrfState(Agent *agent, Icmpv6Proto *proto, VrfEntry *vrf,
                   AgentRouteTable *table);
    ~Icmpv6VrfState();
    Agent *agent() const { return agent_; }
    Icmpv6Proto * icmp_proto() const { return icmp_proto_; }
    void set_route_table_listener_id(const DBTableBase::ListenerId &id) {
        route_table_listener_id_ = id;
    }
    bool default_routes_added() const { return default_routes_added_; }
    void set_default_routes_added(bool value) { default_routes_added_ = value; }

    void RouteUpdate(DBTablePartBase *part, DBEntryBase *entry);
    void ManagedDelete() { deleted_ = true;}
    void Delete();
    bool DeleteRouteState(DBTablePartBase *part, DBEntryBase *entry);
    void PreWalkDone(DBTableBase *partition);
    static void WalkDone(DBTableBase *partition, Icmpv6VrfState *state);
    bool deleted() const {return deleted_;}

private:
    Agent *agent_;
    Icmpv6Proto *icmp_proto_;
    VrfEntry *vrf_;
    AgentRouteTable *rt_table_;
    DBTableBase::ListenerId route_table_listener_id_;
    LifetimeRef<Icmpv6VrfState> table_delete_ref_;
    bool deleted_;
    bool default_routes_added_;
    DBTableWalker::WalkId walk_id_;
    DISALLOW_COPY_AND_ASSIGN(Icmpv6VrfState);
};

class Icmpv6RouteState : public DBState {
public:
    static const uint32_t kMaxRetry = 20;
    static const uint32_t kTimeout = 1000;
    typedef std::map<uint32_t, uint32_t> WaitForTrafficIntfMap;

    Icmpv6RouteState(Icmpv6VrfState *vrf_state, uint32_t vrf_id,
                     IpAddress vm_ip_addr, uint8_t plen);
    ~Icmpv6RouteState();
    bool SendNeighborSolicit();
    void SendNeighborSolicitForAllIntf(const InetUnicastRouteEntry *route);
    void StartTimer();
private:
    Icmpv6VrfState *vrf_state_;
    Timer *ns_req_timer_;
    uint32_t vrf_id_;
    IpAddress vm_ip_;
    uint8_t plen_;
    IpAddress gw_ip_;
    WaitForTrafficIntfMap wait_for_traffic_map_;
};
#endif // vnsw_agent_icmpv6_proto_h
