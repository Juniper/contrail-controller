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
    switch (static_cast<Afi>(afi)) {
        case IPv4:
            out << "IPv4:";
            break;
        case IPv6:
            out << "IPv6:";
            break;
        case L2Vpn:
            out << "L2Vpn:";
            break;
        case UnknownAfi:
            out << "Afi=" << afi << ":";
            break;
    }

    if (out.str().empty())
        out << "Afi=" << afi << ":";

    switch (static_cast<Safi>(safi)) {
        case Unicast:
            out << "Unicast";
            return out.str();
        case EVpn:
            out << "EVpn";
            return out.str();
        case Vpn:
            out << "Vpn";
            return out.str();
        case Enet:
            out << "Enet";
            return out.str();
        case ErmVpn:
            out << "ErmVpn";
            return out.str();
        case MVpn:
            out << "MVpn";
            return out.str();
        case Mcast:
            out << "Mcast";
            return out.str();
        case RTarget:
            out << "RTarget";
            return out.str();
        case Mpls:
            out << "Mpls";
            return out.str();
        case UnknownSafi:
            out << "Safi=" << int(safi);
            return out.str();
    }

    out << "Safi=" << int(safi);
    return out.str();
}

Address::Family BgpAf::AfiSafiToFamily(uint16_t afi, uint8_t safi) {
    switch (afi) {
    case UnknownSafi:
        return Address::UNSPEC;
    case IPv4:
        switch (safi) {
        case Unicast:
            return Address::INET;
        case Mpls:
            return Address::INETMPLS;
        case MVpn:
            return Address::MVPN;
        case Vpn:
            return Address::INETVPN;
        case RTarget:
            return Address::RTARGET;
        case ErmVpn:
            return Address::ERMVPN;
        case UnknownSafi:
        case EVpn:
        case Mcast:
        case Enet:
            return Address::UNSPEC;
        }
    case IPv6:
        switch (safi) {
        case Unicast:
            return Address::INET6;
        case Vpn:
            return Address::INET6VPN;
        case UnknownSafi:
        case Mpls:
        case MVpn:
        case RTarget:
        case ErmVpn:
        case EVpn:
        case Mcast:
        case Enet:
            return Address::UNSPEC;
        }
    case L2Vpn:
        switch (safi) {
        case EVpn:
            return Address::EVPN;
        case Unicast:
        case Vpn:
        case UnknownSafi:
        case Mpls:
        case MVpn:
        case RTarget:
        case ErmVpn:
        case Mcast:
        case Enet:
            return Address::UNSPEC;
        }
    }
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
    case Address::NUM_FAMILIES:
    case Address::UNSPEC:
        return make_pair(BgpAf::UnknownAfi, BgpAf::UnknownSafi);
    }

    assert(false);
    return make_pair(BgpAf::UnknownAfi, BgpAf::UnknownSafi);
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
    case Address::NUM_FAMILIES:
    case Address::UNSPEC:
        return BgpAf::UnknownAfi;
    }

    assert(false);
    return BgpAf::UnknownAfi;
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
    case Address::NUM_FAMILIES:
    case Address::UNSPEC:
        return BgpAf::UnknownSafi;
    }

    assert(false);
    return BgpAf::UnknownSafi;
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
