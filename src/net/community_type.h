/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_communitytype_h
#define ctrlplane_communitytype_h

#include <vector>

#include <boost/array.hpp>
#include <boost/system/error_code.hpp>
#include "base/util.h"

class CommunityType {
public:
    enum WellKnownCommunity {
        AcceptOwn = 0xFFFF0001,
        LlgrStale = 0xFFFF0006,
        NoLlgr = 0xFFFF0007,
        NoReOriginate = 0xFFFFF000,
        NoExport = 0xFFFFFF01,
        NoAdvertise = 0xFFFFFF02,
        NoExportSubconfed = 0xFFFFFF03,
        AcceptOwnNexthop = 0xFFFF0008,
    };

    CommunityType();

    static uint32_t CommunityFromString(const std::string &comm,
                       boost::system::error_code *perr = NULL);

    static const std::string CommunityToString(uint32_t comm);
};

class ExtCommunityType {
public:
    typedef boost::array<uint8_t, 8> ExtCommunityValue;
    typedef std::vector<ExtCommunityValue> ExtCommunityList;
    ExtCommunityType();

    static ExtCommunityList GetWellKnownExtCommunity(const std::string &comm);
    static ExtCommunityList ExtCommunityFromString(const std::string &comm);
};

#endif
