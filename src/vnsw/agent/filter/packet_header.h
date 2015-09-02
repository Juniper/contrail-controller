/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_PACKET_HEADER_H__
#define __AGENT_PACKET_HEADER_H__

#include <boost/asio/detail/socket_types.hpp>
#include <boost/asio/ip/address.hpp>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>

struct PacketHeader {
    //typedef std::vector<uint32_t> sgl;
  PacketHeader() : vrf(-1), src_ip(), src_policy_id(NULL),
        dst_ip(), dst_policy_id(NULL),
        protocol(0), src_port(0), dst_port(0) {};
    uint32_t vrf;
    IpAddress src_ip;
    const std::string *src_policy_id;
    const SecurityGroupList *src_sg_id_l;
    uint32_t src_sg_id;
    IpAddress dst_ip;
    const std::string *dst_policy_id;
    const SecurityGroupList *dst_sg_id_l;
    uint8_t protocol;
    uint16_t src_port;
    uint16_t dst_port;
};

#endif
