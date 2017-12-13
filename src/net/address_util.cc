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

IpAddress PrefixToIp6Netmask(uint32_t plen) {
    if (plen == 0) {
        return IpAddress(Ip6Address());
    }

    if (plen == 128) {
        boost::system::error_code ec;
        Ip6Address all_fs = Ip6Address::from_string
            ("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", ec);
        return IpAddress(all_fs);
    }

    Ip6Address::bytes_type bytes;

    int index = (int) (plen / 8);
    int remain_mask = plen % 8;

    for (int i = 0; i < index; i++) {
        bytes.at(i) = 0xff;
    }

    switch (remain_mask) {
        case 0:
            bytes.at(index++) = 0;
            break;
        case 1:
            bytes.at(index++) = 0x80;
            break;
        case 2:
            bytes.at(index++) = 0xc0;
            break;
        case 3:
            bytes.at(index++) = 0xe0;
            break;
        case 4:
            bytes.at(index++) = 0xf0;
            break;
        case 5:
            bytes.at(index++) = 0xf8;
            break;
        case 6:
            bytes.at(index++) = 0xfc;
            break;
        case 7:
            bytes.at(index++) = 0xfe;
            break;
    }

    for (int i = index; i < 16; ++i) {
        bytes.at(i) = 0;
    }

    return IpAddress(Ip6Address(bytes));
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

    std::string result;

    iter = resolver.resolve(query, error);
    if (error) {
        return result;
    }

    for (; iter != end; ++iter) {
        const boost::asio::ip::address &addr = iter->endpoint().address();
        if (addr.is_v6()) {
            boost::asio::ip::address_v6 addr6 = addr.to_v6();
            if (addr6.is_link_local()) {
                continue;
            }
        }
        result = addr.to_string();
        if (addr.is_v4()) {
            return result;
        }
    }
    return result;
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

void IpToU64(const IpAddress &sip, const IpAddress &dip,
             uint64_t *sip_u, uint64_t *sip_l,
             uint64_t *dip_u, uint64_t *dip_l) {
    if (sip.is_v4()) {
        *sip_l = htonl(sip.to_v4().to_ulong());
        *sip_u = 0;
    } else {
        uint64_t data[2];
        Ip6AddressToU64Array(sip.to_v6(), data, 2);
        *sip_u = data[0];
        *sip_l = data[1];
    }

    if (dip.is_v4()) {
        *dip_l = htonl(dip.to_v4().to_ulong());
        *dip_u = 0;
    } else {
        uint64_t data[2];
        Ip6AddressToU64Array(dip.to_v6(), data, 2);
        *dip_u = data[0];
        *dip_l = data[1];
    }
}

void U64ToIpv6(uint64_t upper, uint64_t lower, IpAddress *ip) {
    boost::asio::ip::address_v6::bytes_type bytes;
    const unsigned char *ptr = (const unsigned char *)&lower;
    for (unsigned int i = 0; i < sizeof(uint64_t); i++) {
        bytes[i] = ptr[i];
    }
    ptr = (const unsigned char *)&upper;
    for (unsigned int i = 0; i < sizeof(uint64_t); i++) {
        bytes[8 + i] = ptr[i];
    }
    *ip = Ip6Address(bytes);
}

void U64ToIp(uint64_t sip_u, uint64_t sip_l, uint64_t dip_u, uint64_t dip_l,
             int family, IpAddress *sip, IpAddress *dip) {
    if (family == Address::INET) {
        *sip = Ip4Address(ntohl(sip_l & 0xFFFFFFFF));
        *dip = Ip4Address(ntohl(dip_l & 0xFFFFFFFF));
    } else {
        U64ToIpv6(sip_u, sip_l, sip);
        U64ToIpv6(dip_u, dip_l, dip);
    }
}

void Ip6AddressToU64Array(const Ip6Address &addr, uint64_t *arr, int size) {
    unsigned char b[16];
    uint64_t ip;
    unsigned int i, j, k;

    if (size != 2)
        return;

    memcpy(b, addr.to_bytes().data(), sizeof(b));

    for (i = 0, j = 0, k = 0; i < 4; i++, j = j+4) {
        ip = 0;

        ip = ((((uint32_t)b[j]) << 24) & 0xFF000000) |
             ((((uint32_t)b[j+1]) << 16) & 0x00FF0000) |
             ((((uint32_t)b[j+2]) << 8) & 0x0000FF00) |
             (((uint32_t)b[j+3]) & 0x000000FF);

        if (i >= 2)
            k = 1;

        if (i%2) {
            arr[k] = arr[k] | (ip & 0x00000000FFFFFFFFU);
        } else {
            arr[k] = (ip << 32) & 0xFFFFFFFF00000000U;
        }
    }


    arr[0] = htobe64(arr[0]);
    arr[1] = htobe64(arr[1]);
}

std::string VectorIpv6ToString(const std::vector<signed char> &ipv6) {

    if (ipv6.size() != 16) {
        return "0000:0000:0000:0000";
    }
    std::ostringstream strm;
    int count = 1;
    for(std::vector<signed char>::const_iterator it = ipv6.begin();
        it != ipv6.end(); ++it) {
        strm << std::hex << (int)((uint8_t) *it);
        if((count % 4) ==0)
            strm << ":";
        count++;
    }
    return strm.str();
}
