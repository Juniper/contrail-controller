/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __TEST_PKT_UTIL_H__
#define __TEST_PKT_UTIL_H__

#include "test/pkt_gen.h"

extern void MakeIpPacket(PktGen *pkt, int ifindex, const char *sip,
			 const char *dip, int proto, int hash_id, 
             int cmd = AgentHdr::TRAP_FLOW_MISS, int vrf = -1);

extern void TxIpPacket(int ifindex, const char *sip, const char *dip, 
			  int proto, int hash_id = 1, int vrf = -1);

extern void TxIpPacketEcmp(int ifindex, const char *sip, const char *dip, 
		                   int proto, int hash_id = 1);

extern void MakeUdpPacket(PktGen *pkt, int ifindex, const char *sip,
			  const char *dip, uint16_t sport, uint16_t dport,
			  int hash_id, uint32_t vrf_id);
extern void TxUdpPacket(int ifindex, const char *sip, const char *dip, 
			   uint16_t sport, uint16_t dport, int hash_id = 1, 
               uint32_t vrf_id = -1);

extern void MakeTcpPacket(PktGen *pkt, int ifindex, const char *sip,
			  const char *dip, uint16_t sport, uint16_t dport,
			  bool ack, int hash_id, uint32_t vrf_id);
extern void TxTcpPacket(int ifindex, const char *sip, const char *dip, 
			   uint16_t sport, uint16_t dport, bool ack, int hash_id = 1, 
               uint32_t vrf_id = -1);

extern void MakeIpMplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
			     const char *out_dip, uint32_t label,
			     const char *sip, const char *dip, uint8_t proto,
			     int hash_id);
extern void TxIpMplsPacket(int ifindex, const char *out_sip,
                              const char *out_dip, uint32_t label,
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
			      uint16_t dport, bool ack, int hash_id);
extern void TxTcpMplsPacket(int ifindex, const char *out_sip,
                               const char *out_dip, uint32_t label,
                               const char *sip, const char *dip, uint16_t sport,
                               uint16_t dport, bool ack, int hash_id = 1);

#endif // __TEST_PKT_UTIL_H__
