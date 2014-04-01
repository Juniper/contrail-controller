/*
 *  * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 *   */
#ifndef vnsw_agent_diag_diag_pkt_handler_hpp
#define vnsw_agent_diag_diag_pkt_handler_hpp

#include <base/logging.h>
#include <net/address.h>
#include <base/timer.h>
#include "boost/date_time/posix_time/posix_time.hpp"

struct AgentDiagPktData;

// Pseudo header for TCP checksum
struct PseudoTcpHdr {
    in_addr_t src;
    in_addr_t dest;
    uint8_t   res;
    uint8_t   prot;
    uint16_t  len;
    PseudoTcpHdr(in_addr_t s, in_addr_t d, uint16_t l) : 
        src(s), dest(d), res(0), prot(6), len(l) { }
};

class DiagPktHandler : public ProtoHandler {
public:
    DiagPktHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                   boost::asio::io_service &io):
        ProtoHandler(agent, info, io), diag_table_(agent->diag_table()) {}
    virtual bool Run();
    void SetReply();
    void SetDiagChkSum();
    void Reply();
    AgentDiagPktData* GetData() {
        return (AgentDiagPktData *)(pkt_info_->data);
    }
    void TcpHdr(in_addr_t, uint16_t, in_addr_t, uint16_t, bool , uint32_t, uint16_t);

private:
    uint16_t TcpCsum(in_addr_t, in_addr_t, uint16_t , tcphdr *);
    void Swap();
    void SwapL4();
    void SwapIpHdr();
    void SwapEthHdr();

    DiagTable *diag_table_;
};

#endif
