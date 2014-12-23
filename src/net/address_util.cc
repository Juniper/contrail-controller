/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include "net/address_util.h"

#include <boost/algorithm/string.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/foreach.hpp>

/* Returns true if the given IPv4 address is member of the IPv4 subnet
 * indicated by IPv4 address and prefix length. Otherwise returns false.
 * We convert both the input IPv4 addresses to their subnet addresses and
 * then compare with each other to conclude whether the IPv4 address is
 * member of the subnet or not.
 */
bool IsIp4SubnetMember(
    const Ip4Address &ip, const Ip4Address &prefix_ip, uint16_t plen) {
    Ip4Address prefix = Address::GetIp4SubnetAddress(prefix_ip, plen);
    return ((prefix.to_ulong() | ~(0xFFFFFFFF << (32 - plen))) ==
            (ip.to_ulong() | ~(0xFFFFFFFF << (32 - plen))));
}

/* Returns true if the given IPv6 address is member of the IPv6 subnet
 * indicated by IPv6 address and prefix length. Otherwise returns false.
 * We do byte by byte comparison of IPv6 subnet address with bitwise AND of
 * IPv6 address and subnet address to decide whether an IPv6 address belongs to
 * its subnet.
 */
bool IsIp6SubnetMember(
    const Ip6Address &ip, const Ip6Address &subnet, uint8_t plen) {
    Ip6Address mask = Address::GetIp6SubnetAddress(subnet, plen);
    for (int i = 0; i < 16; ++i) {
        if ((ip.to_bytes()[i] & mask.to_bytes()[i]) != mask.to_bytes()[i])
            return false;
    }
    return true;
}

Ip4Address GetIp4SubnetBroadcastAddress(
              const Ip4Address &ip_prefix, uint16_t plen) {
    Ip4Address subnet(ip_prefix.to_ulong() | ~(0xFFFFFFFF << (32 - plen)));
    return subnet;
}

// Validate IPv4/IPv6 address string.
bool ValidateIPAddressString(std::string ip_address_str,
                             std::string *error_msg) {
    boost::system::error_code error;
    boost::asio::ip::address::from_string(ip_address_str, error);
    if (error) {
        std::ostringstream out;
        out << "Invalid IP address: " << ip_address_str << std::endl;
        *error_msg = out.str();
        return false;
    }

    return true;
}

IpAddress PrefixToIpNetmask(uint32_t prefix_len) {
    uint32_t mask;

    if (prefix_len == 0) {
        mask = 0;
    } else {
        mask = (~((1 << (32 - prefix_len)) - 1));
    }
    return IpAddress(Ip4Address(mask));
}

uint32_t NetmaskToPrefix(uint32_t netmask) {
    uint32_t count = 0;

    while (netmask) {
        count++;
        netmask = (netmask - 1) & netmask;
    }
    return count;
}

// Validate a list of <ip-address>:<port> endpoints.
bool ValidateServerEndpoints(std::vector<std::string> list,
                             std::string *error_msg) {
    std::ostringstream out;

    BOOST_FOREACH(std::string endpoint, list) {
        std::vector<std::string> tokens;
        boost::split(tokens, endpoint, boost::is_any_of(":"));
        if (tokens.size() != 2) {
            out << "Invalid endpoint " << endpoint << std::endl;
            *error_msg = out.str();
            return false;
        }

        boost::system::error_code error;
        boost::asio::ip::address::from_string(tokens[0], error);

        if (error) {
            out << "Invalid IP address: " << tokens[0] << std::endl;
            *error_msg = out.str();
            return false;
        }

        unsigned long port = strtoul(tokens[1].c_str(), NULL, 0);
        if (errno || port > 0xffFF) {
            out << "Invalid port : " << tokens[1];
            if (errno) {
                out << " " << strerror(errno) << std::endl;
            }
            *error_msg = out.str();
            return false;
        }
    }

    return true;
}

// Return IP address string for a host if it is resolvable, empty string
// otherwise.
std::string GetHostIp(boost::asio::io_service *io_service,
                      const std::string &hostname) {
    boost::asio::ip::tcp::resolver::iterator iter, end;
    boost::system::error_code error;
    boost::asio::ip::tcp::resolver resolver(*io_service);
    boost::asio::ip::tcp::resolver::query query(hostname, "");

    iter = resolver.resolve(query, error);
    return iter != end ? iter->endpoint().address().to_string() : "";
}

//
// Get VN name from routing instance
//
std::string GetVNFromRoutingInstance(const std::string &vn) {
    std::vector<std::string> tokens;
    boost::split(tokens, vn, boost::is_any_of(":"), boost::token_compress_on);
    if (tokens.size() < 3) return "";
    return tokens[0] + ":" + tokens[1] + ":" + tokens[2];
}
