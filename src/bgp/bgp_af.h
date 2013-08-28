/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_af_h
#define ctrlplane_bgp_af_h
#include <string>
#include <inttypes.h>
class BgpAf {
public:
    enum Afi {
        IPv4 = 1,
        IPv6 = 2,
        L2Vpn = 25,
    };
    enum Safi {
        Unicast = 1,
        McastVpn = 5,
        EVpn = 70,
        Vpn = 128,
        Mcast = 8, // TBD nsheth evpn - change to 241 after cleanup on agent
        Enet = 242,
    };
    static std::string ToString(uint8_t afi, uint16_t safi);
};

#endif
