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
        case MVpn:
            out << "Mvpn";
            break;
        case RTarget:
            out << "RTarget";
            break;
        case Mpls:
            out << "Mpls";
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
    if (afi == BgpAf::IPv4 && safi == BgpAf::Mpls)
        return Address::INETMPLS;
    if (afi == BgpAf::IPv4 && safi == BgpAf::Vpn)
        return Address::INETVPN;
    if (afi == BgpAf::IPv4 && safi == BgpAf::RTarget)
        return Address::RTARGET;
    if (afi == BgpAf::IPv4 && safi == BgpAf::ErmVpn)
        return Address::ERMVPN;
    if (afi == BgpAf::IPv4 && safi == BgpAf::MVpn)
        return Address::MVPN;
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
    case Address::INETMPLS:
        return make_pair(BgpAf::IPv4, BgpAf::Mpls);
    case Address::INETVPN:
        return make_pair(BgpAf::IPv4, BgpAf::Vpn);
    case Address::RTARGET:
        return make_pair(BgpAf::IPv4, BgpAf::RTarget);
    case Address::ERMVPN:
        return make_pair(BgpAf::IPv4, BgpAf::ErmVpn);
    case Address::MVPN:
        return make_pair(BgpAf::IPv4, BgpAf::MVpn);
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

BgpAf::Afi BgpAf::FamilyToAfi(Address::Family family) {
    switch (family) {
    case Address::INET:
        return BgpAf::IPv4;
    case Address::INETMPLS:
        return BgpAf::IPv4;
    case Address::INETVPN:
        return BgpAf::IPv4;
    case Address::RTARGET:
        return BgpAf::IPv4;
    case Address::ERMVPN:
        return BgpAf::IPv4;
    case Address::MVPN:
        return BgpAf::IPv4;
    case Address::INET6:
        return BgpAf::IPv6;
    case Address::INET6VPN:
        return BgpAf::IPv6;
    case Address::EVPN:
        return BgpAf::L2Vpn;
    default:
        assert(false);
        return BgpAf::UnknownAfi;
    }
}

BgpAf::Safi BgpAf::FamilyToSafi(Address::Family family) {
    switch (family) {
    case Address::INET:
        return BgpAf::Unicast;
    case Address::INETMPLS:
        return BgpAf::Mpls;
    case Address::INETVPN:
        return BgpAf::Vpn;
    case Address::RTARGET:
        return BgpAf::RTarget;
    case Address::ERMVPN:
        return BgpAf::ErmVpn;
    case Address::MVPN:
        return BgpAf::MVpn;
    case Address::INET6:
        return BgpAf::Unicast;
    case Address::INET6VPN:
        return BgpAf::Vpn;
    case Address::EVPN:
        return BgpAf::EVpn;
    default:
        assert(false);
        return BgpAf::UnknownSafi;
    }
}

uint8_t BgpAf::FamilyToXmppSafi(Address::Family family) {
    switch (family) {
    case Address::ERMVPN:
        return BgpAf::Mcast;
    case Address::EVPN:
        return BgpAf::Enet;
    default:
        return static_cast<uint8_t>(BgpAf::FamilyToSafi(family));
    }
}


