/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_icmpv6_proto_h
#define vnsw_agent_icmpv6_proto_h

#include "pkt/proto.h"
#include "services/icmpv6_handler.h"

#define ICMP_PKT_SIZE 1024
#define IPV6_ALL_NODES_ADDRESS "FF01::1"
#define IPV6_ALL_ROUTERS_ADDRESS "FF01::2"
#define PKT0_LINKLOCAL_ADDRESS "FE80::5E00:0100"

#define ICMPV6_TRACE(obj, arg)                                               \
do {                                                                         \
    std::ostringstream _str;                                                 \
    _str << arg;                                                             \
    Icmpv6##obj::TraceMsg(Icmpv6TraceBuf, __FILE__, __LINE__, _str.str());   \
} while (false)                                                              \

class Icmpv6Proto : public Proto {
public:
    static const uint32_t kRouterAdvertTimeout = 30000; // milli seconds
    typedef std::set<VmInterface *> VmInterfaceSet;

    struct Icmpv6Stats {
        Icmpv6Stats() { Reset(); }
        void Reset() {
            icmpv6_router_solicit_ = icmpv6_router_advert_ =
                icmpv6_ping_request_ = icmpv6_ping_response_ = icmpv6_drop_ = 0;
        }

        uint32_t icmpv6_router_solicit_;
        uint32_t icmpv6_router_advert_;
        uint32_t icmpv6_ping_request_;
        uint32_t icmpv6_ping_response_;
        uint32_t icmpv6_drop_;
    };

    void Shutdown() {}
    Icmpv6Proto(Agent *agent, boost::asio::io_service &io);
    virtual ~Icmpv6Proto();
    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);
    void VrfNotify(DBEntryBase *entry);
    void VnNotify(DBEntryBase *entry);
    void InterfaceNotify(DBEntryBase *entry);

    const VmInterfaceSet &vm_interfaces() { return vm_interfaces_; }

    void IncrementStatsRouterSolicit() { stats_.icmpv6_router_solicit_++; }
    void IncrementStatsRouterAdvert() { stats_.icmpv6_router_advert_++; }
    void IncrementStatsPingRequest() { stats_.icmpv6_ping_request_++; }
    void IncrementStatsPingResponse() { stats_.icmpv6_ping_response_++; }
    void IncrementStatsDrop() { stats_.icmpv6_drop_++; }
    const Icmpv6Stats &GetStats() const { return stats_; }
    void ClearStats() { stats_.Reset(); }

private:
    Timer *timer_;
    Icmpv6Stats stats_;
    VmInterfaceSet vm_interfaces_;
    // handler to send router advertisement upon timer expiry
    boost::scoped_ptr<Icmpv6Handler> routing_advert_handler_;
    DBTableBase::ListenerId vn_table_listener_id_;
    DBTableBase::ListenerId vrf_table_listener_id_;
    DBTableBase::ListenerId interface_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(Icmpv6Proto);
};

#endif // vnsw_agent_icmpv6_proto_h
