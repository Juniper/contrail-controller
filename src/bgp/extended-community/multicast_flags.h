/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_MULTICAST_FLAGS_H_
#define SRC_BGP_EXTENDED_COMMUNITY_MULTICAST_FLAGS_H_

#include <array>

#include <string>

#include "base/parse_object.h"
#include "bgp/extended-community/types.h"

class MulticastFlags {
public:
    static const int kSize = 8;
    typedef std::array<uint8_t, kSize> bytes_type;

    explicit MulticastFlags();
    explicit MulticastFlags(const bytes_type &data);

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

#endif  // SRC_BGP_EXTENDED_COMMUNITY_MULTICAST_FLAGS_H_
