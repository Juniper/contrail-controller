/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "net/bgp_af.h"

#include <boost/assign/list_of.hpp>

#include "testing/gunit.h"

using namespace std;

class BgpAfTest : public ::testing::Test {
protected:
    Address::Family AfiSafiToFamily(uint16_t afi, uint8_t safi) {
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

};

TEST_F(BgpAfTest, ToString) {
    map<BgpAf::Afi, string> afis = boost::assign::map_list_of
        (BgpAf::UnknownAfi, "Afi=0")
        (BgpAf::IPv4, "IPv4")
        (BgpAf::IPv6, "IPv6")
        (BgpAf::L2Vpn, "L2Vpn")
    ;

    map<BgpAf::Safi, string> safis = boost::assign::map_list_of
        (BgpAf::UnknownSafi, "Safi=0")
        (BgpAf::Unicast, "Unicast")
        (BgpAf::Mpls, "Mpls")
        (BgpAf::MVpn, "MVpn")
        (BgpAf::EVpn, "EVpn")
        (BgpAf::Vpn, "Vpn")
        (BgpAf::RTarget, "RTarget")
        (BgpAf::Mcast, "Mcast")
        (BgpAf::Enet, "Enet")
        (BgpAf::ErmVpn, "ErmVpn")
    ;

    for (size_t afi = 0; afi <= 0xFF; afi++) {
        for (size_t safi = 0; safi <= 0xFF; safi++) {
            ostringstream out;
            map<BgpAf::Afi, string>::iterator aiter =
                afis.find(static_cast<BgpAf::Afi>(afi));
            if (aiter != afis.end()) {
                out << aiter->second;
            } else {
                out << "Afi=" << afi;
            }

            out << ":";
            map<BgpAf::Safi, string>::iterator siter =
                safis.find(static_cast<BgpAf::Safi>(safi));
            if (siter != safis.end()) {
                out << siter->second;
            } else {
                out << "Safi=" << safi;
            }
            EXPECT_EQ(out.str(), BgpAf::ToString(static_cast<BgpAf::Afi>(afi),
                                    static_cast<BgpAf::Safi>(safi)));
        }
    }
}

TEST_F(BgpAfTest, AfiSafiToFamily) {
    for (size_t afi = 0; afi <= 0xFF; afi++) {
        for (size_t safi = 0; safi <= 0xFF; safi++) {
            EXPECT_EQ(AfiSafiToFamily(afi, safi),
                      BgpAf::AfiSafiToFamily(afi, safi));
        }
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
