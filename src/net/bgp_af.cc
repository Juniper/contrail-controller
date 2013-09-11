/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "net/bgp_af.h"

#include <sstream>

std::string BgpAf::ToString(uint8_t afi, uint16_t safi) {
    std::ostringstream out;
    switch (afi) {
        case IPv4:
            out << "IPv4:";
            break;
        case IPv6:
            out << "IPv4:";
            break;
        case L2Vpn:
            out << "L2Vpn:";
            break;
        default:
            out << "unknown:";
            break;
    }
    switch (safi) {
        case IPv4:
            out << "Unicast";
            break;
        case McastVpn:
            out << "McastVpn";
            break;
        case EVpn:
            out << "EVpn";
            break;
        case Vpn:
            out << "Vpn";
            break;
        case Enet:
            out << "Enet";
            break;
        default:
            out << "unknown";
            break;
    }
    return out.str();
}

