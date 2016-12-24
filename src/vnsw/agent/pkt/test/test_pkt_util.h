/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __TEST_PKT_UTIL_H__
#define __TEST_PKT_UTIL_H__

#include "test/pkt_gen.h"

extern void MakeIpPacket(PktGen *pkt, int ifindex, const char *sip,
			 const char *dip, int proto, int hash_id, 
             int cmd = AgentHdr::TRAP_FLOW_MISS, int vrf = -1,
             bool fragment = false);

extern void TxIpPacket(int ifindex, const char *sip, const char *dip,
			  int proto, int hash_id = 1, int vrf = -1);

extern void TxL2Packet(int ifindex, const char *smac, const char *dmac,
                       const char *sip, const char *dip, int proto,
                       int hash_id = 1, int vrf = -1, uint16_t sport = 0,
                       uint16_t dport = 0);

extern void MakeUdpPacket(PktGen *pkt, int ifindex, const char *sip,
			  const char *dip, uint16_t sport, uint16_t dport,
			  int hash_id, uint32_t vrf_id);
extern void TxUdpPacket(int ifindex, const char *sip, const char *dip, 
			   uint16_t sport, uint16_t dport, int hash_id = 1, 
               uint32_t vrf_id = -1);

extern void MakeTcpPacket(PktGen *pkt, int ifindex, const char *sip,
			  const char *dip, uint16_t sport, uint16_t dport,
			  bool ack, int hash_id, uint32_t vrf_id, int ttl = 0);

extern void MakeSctpPacket(PktGen *pkt, int ifindex, const char *sip,
                   const char *dip, uint16_t sport, uint16_t dport,
                   int hash_id, uint32_t vrf_id);

extern void TxTcpPacket(int ifindex, const char *sip, const char *dip, 
			   uint16_t sport, uint16_t dport, bool ack, int hash_id = 1, 
               uint32_t vrf_id = -1, int ttl = 0);

extern void MakeIpMplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
			     const char *out_dip, uint32_t label,
			     const char *sip, const char *dip, uint8_t proto,
			     int hash_id);
extern void TxIpMplsPacket(int ifindex, const char *out_sip,
                              const char *out_dip, uint32_t label,
                              const char *sip, const char *dip, uint8_t proto,
                              int hash_id = 1);
extern void TxL2IpMplsPacket(int ifindex, const char *out_sip,
                             const char *out_dip, uint32_t label,
                             const char *smac, const char *dmac,
                             const char *sip, const char *dip, uint8_t proto,
                             int hash_id = 1);
extern void MakeUdpMplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
			      const char *out_dip, uint32_t label,
			      const char *sip, const char *dip, uint16_t sport,
			      uint16_t dport, int hash_id);
extern void TxUdpMplsPacket(int ifindex, const char *out_sip,
			       const char *out_dip, uint32_t label,
			       const char *sip, const char *dip, uint16_t sport,
			       uint16_t dport, int hash_id = 1);
extern void MakeTcpMplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
			      const char *out_dip, uint32_t label,
			      const char *sip, const char *dip, uint16_t sport,
			      uint16_t dport, bool ack, int hash_id, uint8_t gen_id = 0);
extern void TxTcpMplsPacket(int ifindex, const char *out_sip,
                               const char *out_dip, uint32_t label,
                               const char *sip, const char *dip, uint16_t sport,
                               uint16_t dport, bool ack, int hash_id = 1,
                               uint8_t gen_id = 0);

extern void MakeIp6Packet(PktGen *pkt, int ifindex, const char *sip,
                          const char *dip, int proto, int hash_id,
                          int cmd = AGENT_TRAP_FLOW_MISS, int vrf = -1);

extern void TxIp6Packet(int ifindex, const char *sip, const char *dip,
                        int proto, int hash_id = 1, int vrf = -1);

extern void MakeUdp6Packet(PktGen *pkt, int ifindex, const char *sip,
                           const char *dip, uint16_t sport, uint16_t dport,
                           int hash_id, uint32_t vrf_id);
extern void TxUdp6Packet(int ifindex, const char *sip, const char *dip,
                         uint16_t sport, uint16_t dport, int hash_id = 1,
                         uint32_t vrf_id = -1);

extern void MakeTcp6Packet(PktGen *pkt, int ifindex, const char *sip,
                           const char *dip, uint16_t sport, uint16_t dport,
                           bool ack, int hash_id, uint32_t vrf_id);
extern void TxTcp6Packet(int ifindex, const char *sip, const char *dip,
                         uint16_t sport, uint16_t dport, bool ack,
                         int hash_id = 1, uint32_t vrf_id = -1);

extern void MakeIp6MplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
                              const char *out_dip, uint32_t label,
                              const char *sip, const char *dip, uint8_t proto,
                              int hash_id);
extern void TxIp6MplsPacket(int ifindex, const char *out_sip,
                            const char *out_dip, uint32_t label,
                            const char *sip, const char *dip, uint8_t proto,
                            int hash_id = 1);
extern void MakeUdp6MplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
                               const char *out_dip, uint32_t label,
                               const char *sip, const char *dip, uint16_t sport,
                               uint16_t dport, int hash_id);
extern void TxUdp6MplsPacket(int ifindex, const char *out_sip,
                             const char *out_dip, uint32_t label,
                             const char *sip, const char *dip, uint16_t sport,
                             uint16_t dport, int hash_id = 1);
extern void MakeTcp6MplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
                               const char *out_dip, uint32_t label,
                               const char *sip, const char *dip, uint16_t sport,
                               uint16_t dport, bool ack, int hash_id);
extern void TxTcp6MplsPacket(int ifindex, const char *out_sip,
                             const char *out_dip, uint32_t label,
                             const char *sip, const char *dip, uint16_t sport,
                             uint16_t dport, bool ack, int hash_id = 1);
extern void TxL2Ip6Packet(int ifindex, const char *smac, const char *dmac,
                          const char *sip, const char *dip, int proto,
                          int hash_id = 1, int vrf = -1, uint16_t sport = 0,
                          uint16_t dport = 0);
extern void TxIpPBBPacket(int ifindex, const char *out_sip, const char *out_dip,
                          uint32_t label, uint32_t vrf, const MacAddress &b_smac,
                          const MacAddress &b_dmac, uint32_t isid,
                          const MacAddress &c_smac, MacAddress &c_dmac,
                          const char *sip, const char *dip, int hash_id = 1);

void FlowStatsTimerStartStop(Agent *agent, bool stop);

#endif // __TEST_PKT_UTIL_H__
