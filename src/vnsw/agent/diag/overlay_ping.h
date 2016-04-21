/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_diag_overlay_ping_hpp
#define vnsw_agent_diag_overlay_ping_hpp

#include "diag/diag.h"
#include "diag/diag_types.h"
#include "pkt/control_interface.h"
#include <netinet/udp.h> 
#include <oper/tunnel_nh.h> 

class DiagTable;
class OverlayPing : public DiagEntry{
public:
   static const uint32_t KOverlayPingHdr = 2 *(sizeof(struct ether_header) +
                                               sizeof(struct ip) + sizeof(udphdr))
                                               + sizeof(VxlanHdr);
   static const uint32_t KVxlanRABit = 0x01000000;
   static const uint32_t KVxlanIBit = 0x08000000;
   OverlayPing(const OverlayPingReq *req, DiagTable *diag_table);
    virtual ~OverlayPing();
    virtual void SendRequest();
    virtual void HandleReply(DiagPktHandler *handler);
    virtual void RequestTimedOut(uint32_t seq_no);
    virtual void SendSummary();
    void FillOamPktHeader(OverlayOamPktData *pkt);
    uint32_t HashValUdpSourcePort();
private:
    uuid vn_uuid_;
    MacAddress remote_vm_mac_;
    uint16_t   data_len_;
    uint16_t   len_;   //Length including tcp, ip, agent headers + outer eth
    std::string context_;
    boost::posix_time::time_duration avg_rtt_;
    uint32_t  pkt_lost_count_;
};

#endif
