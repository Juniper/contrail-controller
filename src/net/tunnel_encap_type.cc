/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <map>
#include "net/tunnel_encap_type.h"
#include <boost/assign/list_of.hpp>

using std::map;
using std::string;

TunnelEncapType::TunnelEncapType() {
}

static const map<string, TunnelEncapType::Encap>
    fromString = boost::assign::map_list_of
        ("unspecified", TunnelEncapType::UNSPEC)
        ("gre", TunnelEncapType::GRE)
        ("vxlan", TunnelEncapType::VXLAN)
        ("nvgre", TunnelEncapType::NVGRE)
        ("mpls", TunnelEncapType::MPLS)
        ("vxlan-gpe", TunnelEncapType::VXLAN_GPE)
        ("udp", TunnelEncapType::MPLS_O_UDP);

static const map<TunnelEncapType::Encap, string>
    toString = boost::assign::map_list_of
        (TunnelEncapType::UNSPEC, "unspecified")
        (TunnelEncapType::GRE, "gre")
        (TunnelEncapType::VXLAN, "vxlan")
        (TunnelEncapType::NVGRE, "nvgre")
        (TunnelEncapType::MPLS, "mpls")
        (TunnelEncapType::MPLS_O_GRE, "mpls-o-gre")
        (TunnelEncapType::VXLAN_GPE, "vxlan-gpe")
        (TunnelEncapType::MPLS_O_UDP, "udp");

static const map<TunnelEncapType::Encap, string>
    toXmppString = boost::assign::map_list_of
        (TunnelEncapType::UNSPEC, "unspecified")
        (TunnelEncapType::GRE, "gre")
        (TunnelEncapType::VXLAN, "vxlan")
        (TunnelEncapType::NVGRE, "nvgre")
        (TunnelEncapType::MPLS, "mpls")
        (TunnelEncapType::MPLS_O_GRE, "gre")
        (TunnelEncapType::VXLAN_GPE, "vxlan-gpe")
        (TunnelEncapType::MPLS_O_UDP, "udp");

bool TunnelEncapType::TunnelEncapIsValid(uint16_t value) {
    TunnelEncapType::Encap encap = static_cast<TunnelEncapType::Encap>(value);
    map<TunnelEncapType::Encap, string>::const_iterator it =
        toXmppString.find(encap);
    return (it == toXmppString.end() ? false : true);
}

TunnelEncapType::Encap TunnelEncapType::TunnelEncapFromString(
    const string &encap) {
    map<string, TunnelEncapType::Encap>::const_iterator it =
        fromString.find(encap);
    return (it == fromString.end() ? TunnelEncapType::UNSPEC : it->second);
}

const string &TunnelEncapType::TunnelEncapToString(
    TunnelEncapType::Encap encap) {
    static string unspecified("unspecified");
    map<TunnelEncapType::Encap, string>::const_iterator it =
        toString.find(encap);
    return (it == toString.end() ? unspecified : it->second);
}

const string &TunnelEncapType::TunnelEncapToXmppString(
    TunnelEncapType::Encap encap) {
    static string unspecified("unspecified");
    map<TunnelEncapType::Encap, string>::const_iterator it =
        toXmppString.find(encap);
    return (it == toXmppString.end() ? unspecified : it->second);
}
