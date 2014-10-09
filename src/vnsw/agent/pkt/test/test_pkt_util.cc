/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <cmn/agent_cmn.h>

void MakeIpPacket(PktGen *pkt, int ifindex, const char *sip,
		  const char *dip, int proto, int hash_id, int cmd, int vrf) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, cmd, hash_id, vrf) ;
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(sip, dip, proto);
    if (proto == 1) {
        pkt->AddIcmpHdr();
    }
}

void TxIpPacket(int ifindex, const char *sip, const char *dip, 
		   int proto, int hash_id, int vrf) {
    PktGen *pkt = new PktGen();
    MakeIpPacket(pkt, ifindex, sip, dip, proto, hash_id, AgentHdr::TRAP_FLOW_MISS, 
                 vrf);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

void TxIpPacketEcmp(int ifindex, const char *sip, const char *dip,
                    int proto, int hash_id) {
    PktGen *pkt = new PktGen();
    MakeIpPacket(pkt, ifindex, sip, dip, proto, hash_id, AgentHdr::TRAP_ECMP_RESOLVE);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

void MakeUdpPacket(PktGen *pkt, int ifindex, const char *sip,
		   const char *dip, uint16_t sport, uint16_t dport,
		   int hash_id, uint32_t vrf_id) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AgentHdr::TRAP_FLOW_MISS, hash_id, vrf_id);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(sip, dip, IPPROTO_UDP);
    pkt->AddUdpHdr(sport, dport, 64);
}

void TxUdpPacket(int ifindex, const char *sip, const char *dip, 
		    uint16_t sport, uint16_t dport, int hash_id, uint32_t vrf_id) {
    PktGen *pkt = new PktGen();
    MakeUdpPacket(pkt, ifindex, sip, dip, sport, dport, hash_id, vrf_id);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

void MakeTcpPacket(PktGen *pkt, int ifindex, const char *sip,
		   const char *dip, uint16_t sport, uint16_t dport, bool ack,
		   int hash_id, uint32_t vrf_id) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AgentHdr::TRAP_FLOW_MISS, hash_id, vrf_id);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(sip, dip, IPPROTO_TCP);
    pkt->AddTcpHdr(sport, dport, false, false, ack, 64);

}

void TxTcpPacket(int ifindex, const char *sip, const char *dip, 
		    uint16_t sport, uint16_t dport, bool ack, int hash_id,
            uint32_t vrf_id) {
    PktGen *pkt = new PktGen();
    MakeTcpPacket(pkt, ifindex, sip, dip, sport, dport, ack, hash_id, vrf_id);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

void MakeIpMplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
		      const char *out_dip, uint32_t label,
		      const char *sip, const char *dip, uint8_t proto,
		      int hash_id) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AgentHdr::TRAP_FLOW_MISS, hash_id, MplsToVrfId(label),
                     label);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(out_sip, out_dip, IPPROTO_GRE);
    pkt->AddGreHdr();
    pkt->AddMplsHdr(label, true);
    pkt->AddIpHdr(sip, dip, proto);
    if (proto == 1) {
        pkt->AddIcmpHdr();
    }
}

void TxIpMplsPacket(int ifindex, const char *out_sip,
                              const char *out_dip, uint32_t label,
                              const char *sip, const char *dip, uint8_t proto,
                              int hash_id) {
    PktGen *pkt = new PktGen();
    MakeIpMplsPacket(pkt, ifindex, out_sip, out_dip, label, sip, dip, proto,
                     hash_id);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

void MakeUdpMplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
                               const char *out_dip, uint32_t label,
                               const char *sip, const char *dip, uint16_t sport,
                               uint16_t dport, int hash_id) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AgentHdr::TRAP_FLOW_MISS, hash_id, MplsToVrfId(label),
                     label);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(out_sip, out_dip, IPPROTO_GRE);
    pkt->AddGreHdr();
    pkt->AddMplsHdr(label, true);
    pkt->AddIpHdr(sip, dip, IPPROTO_UDP);
    pkt->AddUdpHdr(sport, dport, 64);
}

void TxUdpMplsPacket(int ifindex, const char *out_sip,
                               const char *out_dip, uint32_t label,
                               const char *sip, const char *dip, uint16_t sport,
                               uint16_t dport, int hash_id) {
    PktGen *pkt = new PktGen();
    MakeUdpMplsPacket(pkt, ifindex, out_sip, out_dip, label, sip, dip, sport,
                      dport, hash_id);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

void MakeTcpMplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
                               const char *out_dip, uint32_t label,
                               const char *sip, const char *dip, uint16_t sport,
                               uint16_t dport, bool ack, int hash_id) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AgentHdr::TRAP_FLOW_MISS, hash_id, MplsToVrfId(label),
                     label);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(out_sip, out_dip, IPPROTO_GRE);
    pkt->AddGreHdr();
    pkt->AddMplsHdr(label, true);
    pkt->AddIpHdr(sip, dip, IPPROTO_TCP);
    pkt->AddTcpHdr(sport, dport, false, false, ack, 64);
}

void TxTcpMplsPacket(int ifindex, const char *out_sip,
                               const char *out_dip, uint32_t label,
                               const char *sip, const char *dip, uint16_t sport,
                               uint16_t dport, bool ack, int hash_id) {
    PktGen *pkt = new PktGen();
    MakeTcpMplsPacket(pkt, ifindex, out_sip, out_dip, label, sip, dip, sport,
                      dport, ack, hash_id);

    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

void MakeIp6Packet(PktGen *pkt, int ifindex, const char *sip, const char *dip,
                   int proto, int hash_id, int cmd, int vrf) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, cmd, hash_id, vrf) ;
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", ETHERTYPE_IPV6);
    pkt->AddIp6Hdr(sip, dip, proto);
    if (proto == IPPROTO_ICMPV6) {
        pkt->AddIcmpHdr();
    }
}

void TxIp6Packet(int ifindex, const char *sip, const char *dip, int proto,
                 int hash_id, int vrf) {
    PktGen *pkt = new PktGen();
    MakeIp6Packet(pkt, ifindex, sip, dip, proto, hash_id, AGENT_TRAP_FLOW_MISS, 
                  vrf);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

void TxIp6PacketEcmp(int ifindex, const char *sip, const char *dip,
                     int proto, int hash_id) {
    PktGen *pkt = new PktGen();
    MakeIp6Packet(pkt, ifindex, sip, dip, proto, hash_id,
                  AGENT_TRAP_ECMP_RESOLVE);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

void MakeUdp6Packet(PktGen *pkt, int ifindex, const char *sip,
		   const char *dip, uint16_t sport, uint16_t dport,
		   int hash_id, uint32_t vrf_id) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AGENT_TRAP_FLOW_MISS, hash_id, vrf_id);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", ETHERTYPE_IPV6);
    pkt->AddIp6Hdr(sip, dip, IPPROTO_UDP);
    pkt->AddUdpHdr(sport, dport, 64);
}

void TxUdp6Packet(int ifindex, const char *sip, const char *dip, 
		    uint16_t sport, uint16_t dport, int hash_id, uint32_t vrf_id) {
    PktGen *pkt = new PktGen();
    MakeUdp6Packet(pkt, ifindex, sip, dip, sport, dport, hash_id, vrf_id);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

void MakeTcp6Packet(PktGen *pkt, int ifindex, const char *sip,
                    const char *dip, uint16_t sport, uint16_t dport, bool ack,
                    int hash_id, uint32_t vrf_id) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AGENT_TRAP_FLOW_MISS, hash_id, vrf_id);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", ETHERTYPE_IPV6);
    pkt->AddIp6Hdr(sip, dip, IPPROTO_TCP);
    pkt->AddTcpHdr(sport, dport, false, false, ack, 64);

}

void TxTcp6Packet(int ifindex, const char *sip, const char *dip, 
		    uint16_t sport, uint16_t dport, bool ack, int hash_id,
            uint32_t vrf_id) {
    PktGen *pkt = new PktGen();
    MakeTcp6Packet(pkt, ifindex, sip, dip, sport, dport, ack, hash_id, vrf_id);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

void MakeIp6MplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
		      const char *out_dip, uint32_t label,
		      const char *sip, const char *dip, uint8_t proto,
		      int hash_id) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AGENT_TRAP_FLOW_MISS, hash_id, MplsToVrfId(label),
                     label);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(out_sip, out_dip, IPPROTO_GRE);
    pkt->AddGreHdr();
    pkt->AddMplsHdr(label, true);
    pkt->AddIp6Hdr(sip, dip, proto);
    if (proto == IPPROTO_ICMPV6) {
        pkt->AddIcmpHdr();
    }
}

void TxIp6MplsPacket(int ifindex, const char *out_sip,
                              const char *out_dip, uint32_t label,
                              const char *sip, const char *dip, uint8_t proto,
                              int hash_id) {
    PktGen *pkt = new PktGen();
    MakeIp6MplsPacket(pkt, ifindex, out_sip, out_dip, label, sip, dip, proto,
                      hash_id);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

void MakeUdp6MplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
                               const char *out_dip, uint32_t label,
                               const char *sip, const char *dip, uint16_t sport,
                               uint16_t dport, int hash_id) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AGENT_TRAP_FLOW_MISS, hash_id, MplsToVrfId(label),
                     label);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(out_sip, out_dip, IPPROTO_GRE);
    pkt->AddGreHdr();
    pkt->AddMplsHdr(label, true);
    pkt->AddIp6Hdr(sip, dip, IPPROTO_UDP);
    pkt->AddUdpHdr(sport, dport, 64);
}

void TxUdp6MplsPacket(int ifindex, const char *out_sip,
                               const char *out_dip, uint32_t label,
                               const char *sip, const char *dip, uint16_t sport,
                               uint16_t dport, int hash_id) {
    PktGen *pkt = new PktGen();
    MakeUdp6MplsPacket(pkt, ifindex, out_sip, out_dip, label, sip, dip, sport,
                       dport, hash_id);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

void MakeTcp6MplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
                               const char *out_dip, uint32_t label,
                               const char *sip, const char *dip, uint16_t sport,
                               uint16_t dport, bool ack, int hash_id) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AGENT_TRAP_FLOW_MISS, hash_id, MplsToVrfId(label),
                     label);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(out_sip, out_dip, IPPROTO_GRE);
    pkt->AddGreHdr();
    pkt->AddMplsHdr(label, true);
    pkt->AddIp6Hdr(sip, dip, IPPROTO_TCP);
    pkt->AddTcpHdr(sport, dport, false, false, ack, 64);
}

void TxTcp6MplsPacket(int ifindex, const char *out_sip,
                               const char *out_dip, uint32_t label,
                               const char *sip, const char *dip, uint16_t sport,
                               uint16_t dport, bool ack, int hash_id) {
    PktGen *pkt = new PktGen();
    MakeTcp6MplsPacket(pkt, ifindex, out_sip, out_dip, label, sip, dip, sport,
                       dport, ack, hash_id);

    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}
