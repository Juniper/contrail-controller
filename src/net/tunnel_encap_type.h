/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_NET_TUNNEL_ENCAP_TYPE_H_
#define SRC_NET_TUNNEL_ENCAP_TYPE_H_

#include <stdint.h>
#include <string>

class TunnelEncapType {
public:
    enum Encap {
        UNSPEC = 0,
        GRE = 2,
        VXLAN = 8,
        NVGRE = 9,
        MPLS = 10,
        MPLS_O_GRE = 11,
        VXLAN_GPE = 12,
        MPLS_O_UDP = 13,
        NATIVE = 16
    };

    TunnelEncapType();

    static bool TunnelEncapIsValid(uint16_t value);
    static TunnelEncapType::Encap TunnelEncapFromString(
        const std::string &encap);
    static const std::string &TunnelEncapToString(TunnelEncapType::Encap encap);
    static const std::string &TunnelEncapToXmppString(
        TunnelEncapType::Encap encap);
};

#endif  // SRC_NET_TUNNEL_ENCAP_TYPE_H_
