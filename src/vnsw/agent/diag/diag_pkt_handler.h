/*
 *  * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 *   */
#ifndef vnsw_agent_diag_diag_pkt_handler_hpp
#define vnsw_agent_diag_diag_pkt_handler_hpp

#include <boost/date_time/posix_time/posix_time.hpp>
#include <base/logging.h>
#include <base/address.h>
#include <base/timer.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>

struct AgentDiagPktData;
struct OverlayOamPktData;
struct PktInfo;

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
    static const uint8_t icmp_payload_len = 128;
    DiagPktHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                   boost::asio::io_service &io) :
        ProtoHandler(agent, info, io), done_(false),
        diag_table_(agent->diag_table()) {}
    virtual bool Run();
    void SetReply();
    void SetDiagChkSum();
    void SetDiagChkSumV6();
    void Reply();
    void SendOverlayResponse();
    const std::string &GetAddress() const { return address_; }
    uint8_t* GetData() {
        return (pkt_info_->data);
    }
    bool IsDone() const { return done_; }
    void set_done(bool done) { done_ = done; }
    void TcpHdr(in_addr_t, uint16_t, in_addr_t, uint16_t, bool , uint32_t, uint16_t);
    void TcpHdr(uint16_t len, const uint8_t *src, uint16_t sport,
                const uint8_t *dest, uint16_t dport, bool is_syn,
                uint32_t seq_no, uint8_t next_hdr);
private:
    bool IsTraceRoutePacket();
    bool IsOverlayPingPacket();
    void SetReturnCode(OverlayOamPktData *oamdata);
    bool HandleTraceRoutePacket();
    void SendTimeExceededPacket();
    void SendTimeExceededV6Packet();
    bool HandleTraceRouteResponse();
    bool ParseIcmpData(const uint8_t *data, uint16_t data_len,
                       uint16_t *key, bool is_v4);
    uint16_t TcpCsum(in_addr_t, in_addr_t, uint16_t , tcphdr *);
    void Swap();
    void TunnelHdrSwap();
    void SwapL4();
    void Swapv6L4();
    void SwapIpHdr();
    void SwapIp6Hdr();
    void SwapEthHdr();
    void BuildReply();
    void SegmentHealthCheckReply();

    bool done_;
    DiagTable *diag_table_;
    std::string address_;
};

#endif
