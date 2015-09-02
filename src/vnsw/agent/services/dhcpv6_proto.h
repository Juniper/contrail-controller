/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_dhcpv6_proto_hpp
#define vnsw_agent_dhcpv6_proto_hpp

#include "pkt/proto.h"
#include "services/dhcpv6_handler.h"

#define DHCPV6_TRACE(obj, arg)                                               \
do {                                                                         \
    std::ostringstream _str;                                                 \
    _str << arg;                                                             \
    Dhcpv6##obj::TraceMsg(Dhcpv6TraceBuf, __FILE__, __LINE__, _str.str());   \
} while (false)                                                              \

// DHCPv6 DUID types
#define DHCPV6_DUID_TYPE_LLT  1
#define DHCPV6_DUID_TYPE_EN   2
#define DHCPV6_DUID_TYPE_LL   3

class Dhcpv6Proto : public Proto {
public:
    static const uint32_t kDhcpMaxPacketSize = 1024;
    struct Duid {
        uint16_t type;
        uint16_t hw_type;
        uint8_t mac[ETHER_ADDR_LEN];
    };

    struct DhcpStats {
        DhcpStats() { Reset(); }
        void Reset() {
            solicit = advertise = request = confirm = renew = 
            rebind = reply = release = decline = reconfigure =
            information_request = error = 0;
        }

        uint32_t solicit;
        uint32_t advertise;
        uint32_t request;
        uint32_t confirm;
        uint32_t renew;
        uint32_t rebind;
        uint32_t reply;
        uint32_t release;
        uint32_t decline;
        uint32_t reconfigure;
        uint32_t information_request;
        uint32_t error;
    };

    Dhcpv6Proto(Agent *agent, boost::asio::io_service &io,
                bool run_with_vrouter);
    virtual ~Dhcpv6Proto();
    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);
    void Shutdown();

    const Duid *server_duid() const { return &server_duid_; }

    void IncrStatsSolicit() { stats_.solicit++; }
    void IncrStatsAdvertise() { stats_.advertise++; }
    void IncrStatsRequest() { stats_.request++; }
    void IncrStatsConfirm() { stats_.confirm++; }
    void IncrStatsRenew() { stats_.renew++; }
    void IncrStatsRebind() { stats_.rebind++; }
    void IncrStatsReply() { stats_.reply++; }
    void IncrStatsRelease() { stats_.release++; }
    void IncrStatsDecline() { stats_.decline++; }
    void IncrStatsReconfigure() { stats_.reconfigure++; }
    void IncrStatsInformationRequest() { stats_.information_request++; }
    void IncrStatsError() { stats_.error++; }
    const DhcpStats &GetStats() const { return stats_; }
    void ClearStats() { stats_.Reset(); }

private:
    bool run_with_vrouter_;
    DhcpStats stats_;
    Duid server_duid_;

    DISALLOW_COPY_AND_ASSIGN(Dhcpv6Proto);
};

#endif // vnsw_agent_dhcpv6_proto_hpp
