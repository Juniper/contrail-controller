/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_icmp_proto_h_
#define vnsw_agent_icmp_proto_h_

#include "pkt/proto.h"

// ICMP protocol handler
class IcmpHandler : public ProtoHandler {
public:
    struct IcmpStats {
        uint32_t icmp_gw_ping;
        uint32_t icmp_gw_ping_err;
        uint32_t icmp_drop;

        void Reset() { icmp_gw_ping = icmp_gw_ping_err = icmp_drop = 0; }
        IcmpStats() { Reset(); }
        void IncrStatsGwPing() { icmp_gw_ping++; }
        void IncrStatsGwPingErr() { icmp_gw_ping_err++; }
        void IncrStatsDrop() { icmp_drop++; }
    };

    IcmpHandler(PktInfo *info, boost::asio::io_service &io) 
        : ProtoHandler(info, io), icmp_(pkt_info_->transp.icmp) {
        icmp_len_ = ntohs(pkt_info_->ip->tot_len) - (pkt_info_->ip->ihl * 4);
    }
    virtual ~IcmpHandler() {}

    bool Run();

    static IcmpStats GetStats() { return stats_; }
    static void ClearStats() { stats_.Reset(); }

private:
    bool CheckPacket();
    void SendResponse();

    icmphdr *icmp_;
    uint16_t icmp_len_;
    static IcmpStats stats_;
    DISALLOW_COPY_AND_ASSIGN(IcmpHandler);
};

#endif // vnsw_agent_icmp_proto_h_
