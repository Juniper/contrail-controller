/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_address_h
#define ctrlplane_address_h

#include <boost/asio/ip/address.hpp>

//
// address types are POD types
//
typedef boost::asio::ip::address IpAddress;
typedef boost::asio::ip::address_v4 Ip4Address;
typedef boost::asio::ip::address_v6 Ip6Address;

class Address {
public:
    static const uint8_t kMaxV4Bytes = 4;
    static const uint8_t kMaxV4PrefixLen = 32;
    static const uint8_t kMaxV6Bytes = 16;
    static const uint8_t kMaxV6PrefixLen = 128;

    enum Family {
        UNSPEC = 0,
        INET = 1,
        INET6 = 2,
        INETVPN = 3,
        INET6VPN = 4,
        RTARGET = 5,
        INETFLOW = 6,
        INETVPNFLOW = 7,
        INETMCAST = 8,
        INET6MCAST = 9,
        ENET = 10,
        EVPN = 11,
        ERMVPN = 12,
    };

    Address();

    static Family FamilyFromString(const std::string &family);
    static std::string FamilyToString(Family fmly);
    static Family FamilyFromRoutingTableName(const std::string &name);
    static std::string FamilyToTableString(Family family);
    static Ip4Address V4FromV4MappedV6(const Ip6Address &v6_address);
    static Ip4Address GetIp4SubnetAddress(const Ip4Address &prefix,
                                          uint16_t plen);
    static Ip6Address GetIp6SubnetAddress(const Ip6Address &prefix,
                                          uint16_t plen);

private:
    IpAddress addr_;
};

boost::system::error_code Ip4PrefixParse(const std::string &str,
                                         Ip4Address *addr, int *plen);
boost::system::error_code Ip4SubnetParse(const std::string &str,
                                         Ip4Address *addr, int *plen);
boost::system::error_code Inet6PrefixParse(const std::string &str,
                                           Ip6Address *addr, int *plen);
boost::system::error_code Inet6SubnetParse(const std::string &str,
                                           Ip6Address *addr, int *plen);

#endif
