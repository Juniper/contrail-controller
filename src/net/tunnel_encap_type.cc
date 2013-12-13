/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <map>
#include "net/tunnel_encap_type.h"
#include <boost/assign/list_of.hpp>

using namespace std;

TunnelEncapType::TunnelEncapType() {
}

static const std::map<string, TunnelEncapType::Encap>  
    fromString = boost::assign::map_list_of
        ("unspecified", TunnelEncapType::UNSPEC) 
        ("gre", TunnelEncapType::MPLS_O_GRE) 
        ("udp", TunnelEncapType::MPLS_O_UDP) 
        ("vxlan", TunnelEncapType::VXLAN); 

static const std::map<TunnelEncapType::Encap, string>  
    toString = boost::assign::map_list_of
        (TunnelEncapType::UNSPEC, "unspecified") 
        (TunnelEncapType::MPLS_O_GRE, "gre") 
        (TunnelEncapType::MPLS_O_UDP, "udp") 
        (TunnelEncapType::VXLAN, "vxlan");

TunnelEncapType::Encap 
TunnelEncapType::TunnelEncapFromString(const std::string &encap) {
    std::map<string, TunnelEncapType::Encap>::const_iterator it = fromString.find(encap);
    if (it == fromString.end()) {
        return TunnelEncapType::UNSPEC;
    } else {
        return it->second;
    }
}

const std::string &TunnelEncapType::TunnelEncapToString(TunnelEncapType::Encap encap) {
    std::map<TunnelEncapType::Encap, string>::const_iterator it = toString.find(encap);
    static string unspecified("unspecified");
    if (it == toString.end()) {
        return unspecified;
    } else {
        return it->second;
    }
}
