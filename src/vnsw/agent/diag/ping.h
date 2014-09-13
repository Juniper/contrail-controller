/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_diag_ping_hpp
#define vnsw_agent_diag_ping_hpp

#include "diag/diag.h"
#include "diag/diag_types.h"
#include "pkt/control_interface.h"

class DiagTable;
class Ping: public DiagEntry {
public:
    static const uint32_t KPingUdpHdr = sizeof(ethhdr) + sizeof(iphdr) +
        sizeof(udphdr);
    static const uint32_t KPingTcpHdr = sizeof(ethhdr) + sizeof(iphdr) +
        sizeof(tcphdr);
    Ping(const PingReq *pr,DiagTable *diag_table);
    virtual ~Ping();
    virtual void SendRequest();
    virtual void HandleReply(DiagPktHandler *handler);
    virtual void RequestTimedOut(uint32_t seq_no);
    virtual void SendSummary();
    void FillAgentHeader(AgentDiagPktData *pkt);
    DiagPktHandler* CreateTcpPkt(Agent *agent);
    DiagPktHandler* CreateUdpPkt(Agent *agent);
    static void PingInit();
    static void HandleRequest(DiagPktHandler *);
private:
    Ip4Address sip_;
    Ip4Address dip_;
    uint8_t    proto_;
    uint16_t   sport_;
    uint16_t   dport_;
    uint16_t   data_len_;
    uint16_t   len_;   //Length including tcp, ip, agent headers + outer eth
    std::string vrf_name_;
    boost::system::error_code ec_;
    std::string context_;
    boost::posix_time::time_duration avg_rtt_;
    uint32_t  pkt_lost_count_;
};
#endif
