/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_TYPES_H_
#define SRC_BGP_EXTENDED_COMMUNITY_TYPES_H_

struct BgpExtendedCommunityType {
    enum type {
        TwoOctetAS = 0x00,
        IPv4Address = 0x01,
        FourOctetAS = 0x02,
        Opaque = 0x03,
        Evpn = 0x06,
        Experimental = 0x80,
        Experimental4ByteAs = 0x82,
    };
};

struct BgpExtendedCommunitySubType {
    enum SubType {
        RouteTarget = 0x02,
        RouteOrigin = 0x03,
        SourceAS = 0x09,
        VrfRouteImport = 0x0B,
        SubCluster = 0x85,
    };
};

struct BgpExtendedCommunityOpaqueSubType {
    enum SubType {
        TunnelEncap = 0x0C,
        DefaultGateway = 0x0D,
        LoadBalance = 0xAA, // TBA
    };
};

struct BgpExtendedCommunityEvpnSubType {
    enum SubType {
        MacMobility = 0x00,
        EsiMplsLabel = 0x01,
        EsImport = 0x02,
        RouterMac = 0x03,
        ETree = 0x05,
        MulticastFlags = 0x09,
    };
};

struct BgpExtendedCommunityExperimentalSubType {
    enum SubType {
        OriginVn = 0x71,
        SgId = 0x04,
        Tag = 0x84,
    };
};

#endif // SRC_BGP_EXTENDED_COMMUNITY_TYPES_H_
