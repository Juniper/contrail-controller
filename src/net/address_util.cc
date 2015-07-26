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

    boost::array<uint8_t, 16> bytes;

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

/* The sandesh data-structure used for communicating flows between agent and
 * vrouter represents source and destination IP (both v4 and v6) as a vector
 * of bytes. The below API is used to convert source and destination IP from
 * vector to either Ip4Address or Ip6Address based on family.
 */
void VectorToIp(const std::vector<int8_t> &ip, int family, IpAddress *sip,
                IpAddress *dip) {
    if (family == Address::INET) {
        assert(ip.size() >= 8);
        boost::array<unsigned char, 4> sbytes;
        boost::array<unsigned char, 4> dbytes;
        for (int i = 0; i < 4; i++) {
            sbytes[i] = ip.at(i);
            dbytes[i] = ip.at((i + 4));
        }
        *sip = Ip4Address(sbytes);
        *dip = Ip4Address(dbytes);
    } else {
        boost::array<unsigned char, 16> sbytes;
        boost::array<unsigned char, 16> dbytes;
        assert(ip.size() >= 32);
        for (int i = 0; i < 16; i++) {
            sbytes[i] = ip.at(i);
            dbytes[i] = ip.at((i + 16));
        }
        *sip = Ip6Address(sbytes);
        *dip = Ip6Address(dbytes);
    }
}

std::vector<int8_t> IpToVector(const IpAddress &sip, const IpAddress &dip,
                               Address::Family family) {
    if (family == Address::INET) {
        boost::array<unsigned char, 4> sbytes = sip.to_v4().to_bytes();
        boost::array<unsigned char, 4> dbytes = dip.to_v4().to_bytes();
        std::vector<int8_t> ip_vect(sbytes.begin(), sbytes.end());
        std::vector<int8_t>::iterator it = ip_vect.begin();

        ip_vect.insert(it + 4, dbytes.begin(), dbytes.end());
        assert(ip_vect.size() == 8);
        return ip_vect;
    } else {
        boost::array<unsigned char, 16> sbytes = sip.to_v6().to_bytes();
        boost::array<unsigned char, 16> dbytes = dip.to_v6().to_bytes();
        std::vector<int8_t> ip_vect(sbytes.begin(), sbytes.end());
        std::vector<int8_t>::iterator it = ip_vect.begin();

        ip_vect.insert(it + 16, dbytes.begin(), dbytes.end());
        assert(ip_vect.size() == 32);
        return ip_vect;
    }
}

/* The flow data-structure represented by vrouter has source and destination IP
 * address as single char array for both V4 and V6. The below API is used to
 * convert Source IP and destination IP from char array to either Ip4Address
 * or Ip6Address based on family.
 */
void  CharArrayToIp(const unsigned char *ip, int size, int family,
                    IpAddress *sip, IpAddress *dip) {
    if (family == Address::INET) {
        assert(size >= 8);
        boost::array<unsigned char, 4> sbytes;
        boost::array<unsigned char, 4> dbytes;
        for (int i = 0; i < 4; i++) {
            sbytes[i] = ip[i];
            dbytes[i] = ip[i + 4];
        }
        *sip = Ip4Address(sbytes);
        *dip = Ip4Address(dbytes);
    } else {
        assert(size >= 32);
        boost::array<unsigned char, 16> sbytes;
        boost::array<unsigned char, 16> dbytes;
        for (int i = 0; i < 16; i++) {
            sbytes[i] = ip[i];
            dbytes[i] = ip[i + 16];
        }
        *sip = Ip6Address(sbytes);
        *dip = Ip6Address(dbytes);
    }
}

void Ip6AddressToU64Array(const Ip6Address &addr, uint64_t *arr, int size) {
    uint32_t *words;
    if (size != 2)
        return;
    words = (uint32_t *) (addr.to_bytes().c_array());
    arr[0] = (((uint64_t)words[0] << 32) & 0xFFFFFFFF00000000U) |
             ((uint64_t)words[1] & 0x00000000FFFFFFFFU);
    arr[1] = (((uint64_t)words[2] << 32) & 0xFFFFFFFF00000000U) |
             ((uint64_t)words[3] & 0x00000000FFFFFFFFU);
}
