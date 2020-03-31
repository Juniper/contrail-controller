/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_MAC_MOBILITY_H_
#define SRC_BGP_EXTENDED_COMMUNITY_MAC_MOBILITY_H_

#include <array>
#include <boost/system/error_code.hpp>

#include <string>

#include "base/parse_object.h"
#include "bgp/extended-community/types.h"

class MacMobility {
public:
    static const int kSize = 8;
    typedef std::array<uint8_t, kSize> bytes_type;

    explicit MacMobility(uint32_t seq, bool sticky=false);
    explicit MacMobility(const bytes_type &data);

    uint32_t sequence_number() const;
    bool sticky() const;

    const bytes_type &GetExtCommunity() const {
        return data_;
    }

    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }

    std::string ToString();

private:
    bytes_type data_;
};

#endif  // SRC_BGP_EXTENDED_COMMUNITY_MAC_MOBILITY_H_
