/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef NET_ADDRESS_UTIL_H__
#define NET_ADDRESS_UTIL_H__

#include <string>
#include <vector>

#include "net/address.h"

namespace boost {
namespace asio {
class io_service;
}
}

/* 
 * Returns true if the given IPv4 address is member of the IPv4 subnet
 * indicated by IPv4 address and prefix length. Otherwise returns false.
 */
bool IsIp4SubnetMember(const Ip4Address &ip, const Ip4Address &prefix_ip,
                       uint16_t plen);
/* 
 * Returns true if the given IPv6 address is member of the IPv6 subnet
 * indicated by IPv6 address and prefix length. Otherwise returns false.
 */
bool IsIp6SubnetMember(const Ip6Address &ip, const Ip6Address &subnet,
                       uint8_t plen);

Ip4Address GetIp4SubnetBroadcastAddress(const Ip4Address &ip_prefix,
                                        uint16_t plen);

bool ValidateIPAddressString(std::string ip_address_str,
                             std::string *error_msg);

IpAddress PrefixToIpNetmask(uint32_t prefix_len);
uint32_t NetmaskToPrefix(uint32_t netmask);

bool ValidateServerEndpoints(std::vector<std::string> list,
                             std::string *error_msg);

/*
 * Return IP address string for a host if it is resolvable, empty string
 * otherwise.
 */
std::string GetHostIp(boost::asio::io_service *io_service,
                      const std::string &hostname);

/*
 * Get VN name from routing instance
 */
std::string GetVNFromRoutingInstance(const std::string &vn);

#endif
