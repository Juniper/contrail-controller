/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_af_h
#define ctrlplane_bgp_af_h

#include <string>
#include <inttypes.h>

#include "net/address.h"

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
        RTFilter = 132,
        Mcast = 241,
        Enet = 242,
    };

    static std::string ToString(uint8_t afi, uint16_t safi);
    static Address::Family AfiSafiToFamily(uint8_t afi, uint8_t safi);
};

#endif
