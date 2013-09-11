/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_icmp_proto_h_
#define vnsw_agent_icmp_proto_h_

#include "pkt/proto.h"

// ICMP protocol handler
class IcmpHandler : public ProtoHandler {
public:
    IcmpHandler(PktInfo *info, boost::asio::io_service &io) 
        : ProtoHandler(info, io), icmp_(pkt_info_->transp.icmp) {
        icmp_len_ = ntohs(pkt_info_->ip->tot_len) - (pkt_info_->ip->ihl * 4);
    }
    virtual ~IcmpHandler() {}

    bool Run();

private:
    bool CheckPacket();
    void SendResponse();

    icmphdr *icmp_;
    uint16_t icmp_len_;
    DISALLOW_COPY_AND_ASSIGN(IcmpHandler);
};

class IcmpProto : public Proto<IcmpHandler> {
public:
    struct IcmpStats {
        uint32_t icmp_gw_ping;
        uint32_t icmp_gw_ping_err;
        uint32_t icmp_drop;

        IcmpStats() { Reset(); }
        void Reset() { icmp_gw_ping = icmp_gw_ping_err = icmp_drop = 0; }
    };

    static void Init(boost::asio::io_service &io);
    static void Shutdown();
    virtual ~IcmpProto();

    void IncrStatsGwPing() { stats_.icmp_gw_ping++; }
    void IncrStatsGwPingErr() { stats_.icmp_gw_ping_err++; }
    void IncrStatsDrop() { stats_.icmp_drop++; }
    IcmpStats GetStats() { return stats_; }
    void ClearStats() { stats_.Reset(); }

private:
    IcmpProto(boost::asio::io_service &io);

    IcmpStats stats_;
    DISALLOW_COPY_AND_ASSIGN(IcmpProto);
};

#endif // vnsw_agent_icmp_proto_h_
