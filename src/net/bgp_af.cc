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
        case RTarget:
            out << "RTarget";
            break;
        default:
            out << "unknown";
            break;
    }
    return out.str();
}

Address::Family BgpAf::AfiSafiToFamily(uint8_t afi, uint8_t safi) {
    if (afi == BgpAf::IPv4 && safi == BgpAf::Unicast)
        return Address::INET;
    if (afi == BgpAf::IPv4 && safi == BgpAf::Vpn)
        return Address::INETVPN;
    if (afi == BgpAf::L2Vpn && safi == BgpAf::EVpn)
        return Address::EVPN;
    if (afi == BgpAf::IPv4 && safi == BgpAf::RTarget)
        return Address::RTARGET;

    return Address::UNSPEC;
}

void BgpAf::FamilyToAfiSafi(Address::Family fmly, uint16_t &afi, uint8_t &safi) {
    if (fmly == Address::INET) {
        afi = BgpAf::IPv4;
        safi = BgpAf::Unicast;
    } else if (fmly == Address::INETVPN) {
        afi = BgpAf::IPv4;
        safi = BgpAf::Vpn;
    } else if (fmly == Address::EVPN) {
        afi = BgpAf::L2Vpn; 
        safi = BgpAf::EVpn;
    } else if (fmly == Address::RTARGET) {
        afi = BgpAf::IPv4; 
        safi = BgpAf::RTarget;
    } else {
        assert(0);
    }
}
