/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <map>
#include "net/address.h"
#include <boost/assign/list_of.hpp>

using namespace std;

Address::Address() {
}

static const std::map<string, Address::Family>  
    fromString = boost::assign::map_list_of
        ("unspecified", Address::UNSPEC) 
        ("inet", Address::INET) 
        ("inet6", Address::INET6) 
        ("inet-vpn", Address::INETVPN) 
        ("inet6-vpn", Address::INET6VPN) 
        ("route-target", Address::RTARGET) 
        ("inet-flow", Address::INETFLOW) 
        ("inet-vpn-flow", Address::INETVPNFLOW)
        ("inetmcast", Address::INETMCAST)
        ("inet6mcast", Address::INET6MCAST)
        ("enet", Address::ENET)
        ("e-vpn", Address::EVPN)
        ("erm-vpn", Address::ERMVPN);

static const std::map<Address::Family, string>  
    toString = boost::assign::map_list_of
        (Address::UNSPEC, "unspecified") 
        (Address::INET, "inet") 
        (Address::INET6, "inet6") 
        (Address::INETVPN, "inet-vpn") 
        (Address::INET6VPN, "inet6-vpn") 
        (Address::RTARGET, "route-target") 
        (Address::INETFLOW, "inet-flow") 
        (Address::INETVPNFLOW, "inet-vpn-flow")
        (Address::INETMCAST, "inetmcast")
        (Address::INET6MCAST, "inet6mcast")
        (Address::ENET, "enet")
        (Address::EVPN, "e-vpn")
        (Address::ERMVPN, "erm-vpn");

static const std::map<string, Address::Family>
    fromTableName = boost::assign::map_list_of
        ("unspecified", Address::UNSPEC)
        ("inet", Address::INET)
        ("inet6", Address::INET6)
        ("l3vpn", Address::INETVPN)
        ("l3vpn-inet6", Address::INET6VPN)
        ("rtarget", Address::RTARGET)
        ("inetflow", Address::INETFLOW)
        ("invpnflow", Address::INETVPNFLOW)
        ("inetmcast", Address::INETMCAST)
        ("inet6mcast", Address::INET6MCAST)
        ("enet", Address::ENET)
        ("evpn", Address::EVPN)
        ("ermvpn", Address::ERMVPN);

static const std::map<Address::Family, string>
    toTableName = boost::assign::map_list_of
        (Address::UNSPEC, "unspecified")
        (Address::INET, "inet")
        (Address::INET6, "inet6")
        (Address::INETVPN, "l3vpn")
        (Address::INET6VPN, "l3vpn-inet6")
        (Address::RTARGET, "rtarget")
        (Address::INETFLOW, "inetflow")
        (Address::INETVPNFLOW, "invpnflow")
        (Address::INETMCAST, "inetmcast")
        (Address::INET6MCAST, "inet6mcast")
        (Address::ENET, "enet")
        (Address::EVPN, "evpn")
        (Address::ERMVPN, "ermvpn");

Address::Family Address::FamilyFromString(const std::string &family) {
    std::map<string, Address::Family>::const_iterator loc =
            fromString.find(family);
    if (loc != fromString.end()) {
        return loc->second;
    }
    return Address::UNSPEC;
}

std::string Address::FamilyToString(Address::Family family) {
    return toString.find(family)->second;
}

Address::Family Address::FamilyFromRoutingTableName(const std::string &name) {
    size_t pos1 = name.rfind('.');
    if (pos1 == string::npos) return Address::UNSPEC;
    size_t pos2 = name.rfind('.', pos1);
    if (pos2 == string::npos) pos2 = 0;

    std::map<string, Address::Family>::const_iterator loc =
            fromTableName.find(name.substr(pos2, pos1 - pos2));
    if (loc != fromTableName.end()) {
        return loc->second;
    }
    return Address::UNSPEC;
}

std::string Address::FamilyToTableString(Address::Family family) {
    return toTableName.find(family)->second;
}

Address::Family Address::VpnFamilyFromFamily(Address::Family family) {
    switch (family) {
    case Address::INET:
    case Address::INETVPN:
        return Address::INETVPN;
    case Address::INET6:
    case Address::INET6VPN:
        return Address::INET6VPN;
    case Address::EVPN:
        return Address::EVPN;
    case Address::ERMVPN:
        return Address::ERMVPN;
    default:
        return Address::UNSPEC;
    }
    return Address::UNSPEC;
}

static int CountDots(const string &str) {
    int count = 0;
    size_t pos = 0;
    while (pos < str.size()) {
        pos = str.find('.', pos);
        if (pos == string::npos) {
            break;
        }
        count++;
        pos++;
    }
    return count;
}

// Return the IP address part as it is i.e. 1.2.3.4/24 will return 1.2.3.4 and
// 24.
boost::system::error_code Ip4PrefixParse(const string &str, Ip4Address *addr,
                                         int *plen) {
    size_t pos = str.find('/');
    if (pos == string::npos) {
        return make_error_code(boost::system::errc::invalid_argument);
    }
    *plen = atoi(str.c_str() + pos + 1);
    if ((*plen < 0) || (*plen > Address::kMaxV4PrefixLen)) {
        return make_error_code(boost::system::errc::invalid_argument);
    }
    
    string addrstr = str.substr(0, pos);
    int dots = CountDots(addrstr);
    while (dots < 3) {
        addrstr.append(".0");
        dots++;
    }

    boost::system::error_code err;
    *addr = Ip4Address::from_string(addrstr, err);
    return err;
}

// Return the bitwise and of IP and mask.
boost::system::error_code Ip4SubnetParse(const string &str, Ip4Address *addr,
                                         int *plen) {
    Ip4Address address;
    boost::system::error_code err;
    err = Ip4PrefixParse(str, &address, plen);
    if (!err) {
        *addr = Address::GetIp4SubnetAddress(address, *plen);
    }
    return err;
}

// Return the IP address part as it is i.e. 2001:db8:85a3:aaaa::b:c:d/64 will
// return 2001:db8:85a3:aaaa::b:c:d and 64.
boost::system::error_code Inet6PrefixParse(const string &str, Ip6Address *addr,
                                           int *plen) {
    size_t pos = str.find('/');
    if (pos == string::npos) {
        return make_error_code(boost::system::errc::invalid_argument);
    }
    *plen = atoi(str.c_str() + pos + 1);
    if ((*plen < 0) || (*plen > Address::kMaxV6PrefixLen)) {
        return make_error_code(boost::system::errc::invalid_argument);
    }

    string addrstr = str.substr(0, pos);
    boost::system::error_code err;
    *addr = Ip6Address::from_string(addrstr, err);
    return err;
}

// Return the bitwise and of IP and mask.
boost::system::error_code Inet6SubnetParse(const string &str, Ip6Address *addr,
                                           int *plen) {
    Ip6Address address;
    boost::system::error_code err;
    err = Inet6PrefixParse(str, &address, plen);
    if (!err) {
        *addr = Address::GetIp6SubnetAddress(address, *plen);
    }
    return err;
}

/* Returns IPv4 subnet address for a given IPv4 address and prefix length. The
 * IPv4 address and prefix lengths are converted to u32 numbers and then bitwise
 * AND operation is performed between those 2 numbers and the resulting u32
 * number is converted to IPv4Address object and returned. If prefix length is 0
 * then 0 is converted to IPv4Address object and returned.
 */
Ip4Address Address::GetIp4SubnetAddress(const Ip4Address &prefix, uint16_t plen) {
    if (plen == 0) {
        return boost::asio::ip::address_v4(0);
    }

    Ip4Address subnet(prefix.to_ulong() & (0xFFFFFFFF << (32 - plen)));
    return subnet;
}

/* Returns IPv6 subnet address for a given IPv6 address and prefix length. The
 * input IPv6 address is first converted into an array of 8 two byte integers.
 * Based on prefix length we first figure out the array index position from
 * where the masking of bits needs to start. Once the array index is found we
 * have 16 possible bit positions from where masking has to start. For each of
 * the 16 positions we have a unique 16 byte constant that we use to mask the
 * two byte integer. Once the array position is appropriately masked we mask
 * the rest of two byte integers till the array ends. The resulting array is
 * converted to IPv6 object and returned. If the prefix length is 0 then we
 * return an IPv6 object constructed using its default constructors which
 * assumes all bits as 0s.
 */
Ip6Address Address::GetIp6SubnetAddress(const Ip6Address &prefix, uint16_t plen) {
    if (plen == 0) {
        return boost::asio::ip::address_v6();
    }

    if (plen == 128) {
        return prefix;
    }

    uint16_t ip6[8], in_ip6[8];
    unsigned char bytes[16];

    inet_pton(AF_INET6, prefix.to_string().c_str(), ip6);

    for (int i = 0; i < 8; ++i) {
        in_ip6[i] = ntohs(ip6[i]);
    }

    int index = (int) (plen / 16);
    int remain_mask = plen % 16;

    switch (remain_mask) {
        case 0:
            in_ip6[index++] = 0;
            break;
        case 1:
            in_ip6[index++] &= 0x8000;
            break;
        case 2:
            in_ip6[index++] &= 0xc000;
            break;
        case 3:
            in_ip6[index++] &= 0xe000;
            break;
        case 4:
            in_ip6[index++] &= 0xf000;
            break;
        case 5:
            in_ip6[index++] &= 0xf800;
            break;
        case 6:
            in_ip6[index++] &= 0xfc00;
            break;
        case 7:
            in_ip6[index++] &= 0xfe00;
            break;
        case 8:
            in_ip6[index++] &= 0xff00;
            break;
        case  9:
            in_ip6[index++] &= 0xff80;
            break;
        case 10:
            in_ip6[index++] &= 0xffc0;
            break;
        case 11:
            in_ip6[index++] &= 0xffe0;
            break;
        case 12:
            in_ip6[index++] &= 0xfff0;
            break;
        case 13:
            in_ip6[index++] &= 0xfff8;
            break;
        case 14:
            in_ip6[index++] &= 0xfffc;
            break;
        case 15:
            in_ip6[index++] &= 0xfffe;
            break;
    }

    for (int i = index; i < 8; ++i) {
        in_ip6[i] = 0;
    }

    for (int i = 0; i < 8; ++i) {
        ip6[i] = htons(in_ip6[i]);
    }
    memcpy(bytes, ip6, sizeof(ip6));
    boost::array<uint8_t, 16> to_bytes;
    for (int i = 0; i < 16; ++i) {
        to_bytes.at(i) = bytes[i];
    }
    return boost::asio::ip::address_v6(to_bytes);
}

// Ip6Address.to_v4() has exceptions. Plus, we dont have a to_v4() version
// without exceptions that takes an boost::error_code.
// If the v6-address is v4_mapped, return the ipv4 equivalent address. Else
// return a 'zero' ipv4 address.
Ip4Address Address::V4FromV4MappedV6(const Ip6Address &v6_address) {
    Ip4Address v4_address;
    if (v6_address.is_v4_mapped()) {
        Ip6Address::bytes_type v6_bt = v6_address.to_bytes();
        Ip4Address::bytes_type v4_bt = 
            { { v6_bt[12], v6_bt[13], v6_bt[14], v6_bt[15] } };
        v4_address = Ip4Address(v4_bt);
    }
    return v4_address;
}
