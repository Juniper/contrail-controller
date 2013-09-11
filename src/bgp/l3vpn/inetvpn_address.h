/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_inetvpn_address_h
#define ctrlplane_inetvpn_address_h

#include <boost/system/error_code.hpp>

#include "bgp/bgp_attr_base.h"
#include "net/address.h"
#include "net/rd.h"

class IpVpnAddress {
public:
    IpVpnAddress();
    
    static IpVpnAddress FromString(const std::string &str,
                                   boost::system::error_code *errorp = NULL);
    
    std::string ToString() const;
    
    RouteDistinguisher route_distinguisher() const { return rd_; }
    
private:
    RouteDistinguisher rd_;
    IpAddress addr_;
};

class InetVpnPrefix {
public:
    InetVpnPrefix();
    explicit InetVpnPrefix(const BgpProtoPrefix &prefix);
    InetVpnPrefix(const RouteDistinguisher &rd, Ip4Address ip, int prefixlen) 
        : rd_(rd), addr_(ip), prefixlen_(prefixlen) {
    }
    static InetVpnPrefix FromString(const std::string &str,
                                    boost::system::error_code *errorp = NULL);
    std::string ToString() const;
    bool IsMoreSpecific(const InetVpnPrefix &rhs) const;

    RouteDistinguisher route_distinguisher() const { return rd_; }
    Ip4Address addr() const { return addr_; }
    int prefixlen() const { return prefixlen_; }
    void BuildProtoPrefix(uint32_t label, BgpProtoPrefix *prefix) const;

private:
    RouteDistinguisher rd_;
    Ip4Address addr_;
    int prefixlen_;
};


#endif
