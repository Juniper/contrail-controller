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
        (Address::INETVPN, "inetvpn")
        (Address::INET6VPN, "inet6vpn")
        (Address::RTARGET, "rtarget")
        (Address::INETFLOW, "inetflow")
        (Address::INETVPNFLOW, "invpnflow")
        (Address::INETMCAST, "inetmcast")
        (Address::INET6MCAST, "inet6mcast")
        (Address::ENET, "enet")
        (Address::EVPN, "evpn")
        (Address::ERMVPN, "ermvpn");

Address::Family Address::FamilyFromString(std::string family) {
    return fromString.find(family)->second;
}

std::string Address::FamilyToString(Address::Family family) {
    return toString.find(family)->second;
}

Address::Family Address::FamilyFromRoutingTableName(std::string name) {
    size_t pos1 = name.rfind('.');
    if (pos1 == string::npos) return Address::UNSPEC;
    size_t pos2 = name.rfind('.', pos1);
    if (pos2 == string::npos) pos2 = 0;

    return fromTableName.find(name.substr(pos2, pos1 - pos2))->second;
}

std::string Address::FamilyToTableString(Address::Family family) {
    return toTableName.find(family)->second;
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

boost::system::error_code Ip4PrefixParse(const string &str, Ip4Address *addr, int *plen) {
    size_t pos = str.find('/');
    if (pos == string::npos) {
        return make_error_code(boost::system::errc::invalid_argument);
    }
    *plen = atoi(str.c_str() + pos + 1);
    
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
