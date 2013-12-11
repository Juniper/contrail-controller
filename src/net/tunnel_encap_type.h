/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_tunnelencaptype_h
#define ctrlplane_tunnelencaptype_h

#include <string>

class TunnelEncapType {
public:
    enum Encap {
        UNSPEC = 0,
        MPLS_O_GRE = 2,
        MPLS_O_UDP = 37001,
        VXLAN = 37002,
    };

    TunnelEncapType();

    static TunnelEncapType::Encap TunnelEncapFromString(const std::string &encap);
    static const std::string &TunnelEncapToString(TunnelEncapType::Encap encap);

private:
};
#endif
