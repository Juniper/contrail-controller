/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "net/bgp_af.h"

#include <sstream>

using std::make_pair;
using std::pair;
using std::ostringstream;
using std::string;

string BgpAf::ToString(uint16_t afi, uint8_t safi) {
    ostringstream out;
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
    if (afi == BgpAf::IPv4 && safi == BgpAf::RTarget)
        return Address::RTARGET;
    if (afi == BgpAf::IPv4 && safi == BgpAf::ErmVpn)
        return Address::ERMVPN;
    if (afi == BgpAf::IPv6 && safi == BgpAf::Unicast)
        return Address::INET6;
    if (afi == BgpAf::IPv6 && safi == BgpAf::Vpn)
        return Address::INET6VPN;
    if (afi == BgpAf::L2Vpn && safi == BgpAf::EVpn)
        return Address::EVPN;

    return Address::UNSPEC;
}

pair<uint16_t, uint8_t> BgpAf::FamilyToAfiSafi(Address::Family family) {
    switch (family) {
    case Address::INET:
        return make_pair(BgpAf::IPv4, BgpAf::Unicast);
    case Address::INETVPN:
        return make_pair(BgpAf::IPv4, BgpAf::Vpn);
    case Address::RTARGET:
        return make_pair(BgpAf::IPv4, BgpAf::RTarget);
    case Address::ERMVPN:
        return make_pair(BgpAf::IPv4, BgpAf::ErmVpn);
    case Address::INET6:
        return make_pair(BgpAf::IPv6, BgpAf::Unicast);
    case Address::INET6VPN:
        return make_pair(BgpAf::IPv6, BgpAf::Vpn);
    case Address::EVPN:
        return make_pair(BgpAf::L2Vpn, BgpAf::EVpn);
    default:
        assert(false);
        return make_pair(BgpAf::UnknownAfi, BgpAf::UnknownSafi);
    }
}
