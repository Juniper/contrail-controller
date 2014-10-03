/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "net/bgp_af.h"

#include <sstream>

std::string BgpAf::ToString(uint16_t afi, uint8_t safi) {
    std::ostringstream out;
    switch (afi) {
        case IPv4:
            out << "IPv4:";
            break;
        case IPv6:
            out << "IPv6:";
            break;
        case L2Vpn:
            out << "L2Vpn:";
            break;
        default:
            out << "Afi=" << afi << ":";
            break;
    }
    switch (safi) {
        case Unicast:
            out << "Unicast";
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
        case ErmVpn:
            out << "ErmVpn";
            break;
        case RTarget:
            out << "RTarget";
            break;
        default:
            out << "Safi=" << int(safi);
            break;
    }
    return out.str();
}

Address::Family BgpAf::AfiSafiToFamily(uint16_t afi, uint8_t safi) {
    if (afi == BgpAf::IPv4 && safi == BgpAf::Unicast)
        return Address::INET;
    if (afi == BgpAf::IPv4 && safi == BgpAf::Vpn)
        return Address::INETVPN;
    if (afi == BgpAf::IPv6 && safi == BgpAf::Vpn)
        return Address::INET6VPN;
    if (afi == BgpAf::L2Vpn && safi == BgpAf::EVpn)
        return Address::EVPN;
    if (afi == BgpAf::IPv4 && safi == BgpAf::ErmVpn)
        return Address::ERMVPN;
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
    } else if (fmly == Address::ERMVPN) {
        afi = BgpAf::IPv4;
        safi = BgpAf::ErmVpn;
    } else if (fmly == Address::EVPN) {
        afi = BgpAf::L2Vpn;
        safi = BgpAf::EVpn;
    } else if (fmly == Address::RTARGET) {
        afi = BgpAf::IPv4; 
        safi = BgpAf::RTarget;
    } else if (fmly == Address::INET6VPN) {
        afi = BgpAf::IPv6;
        safi = BgpAf::Vpn;
    } else {
        assert(0);
    }
}
