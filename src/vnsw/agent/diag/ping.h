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
    static const uint32_t KPingUdpHdr = sizeof(struct ether_header) +
                                        sizeof(struct ip) + sizeof(udphdr);
    static const uint32_t KPingTcpHdr = sizeof(struct ether_header) +
                                        sizeof(struct ip) + sizeof(tcphdr);
    Ping(const PingReq *pr,DiagTable *diag_table);
    virtual ~Ping();
    virtual void SendRequest();
    virtual void HandleReply(DiagPktHandler *handler);
    virtual void RequestTimedOut(uint32_t seq_no);
    virtual void SendSummary();
    void FillAgentHeader(AgentDiagPktData *pkt);
    DiagPktHandler* CreateTcpPkt(Agent *agent);
    DiagPktHandler* CreateUdpPkt(Agent *agent);

    static void HandleRequest(DiagPktHandler *);

private:
    uint16_t   data_len_;
    uint16_t   len_;   //Length including tcp, ip, agent headers + outer eth
    std::string context_;
    boost::posix_time::time_duration avg_rtt_;
    uint32_t  pkt_lost_count_;
};

#endif
