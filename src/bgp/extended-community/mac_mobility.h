/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_mac_mobility_h
#define ctrlplane_mac_mobility_h

#include <boost/array.hpp>
#include <boost/system/error_code.hpp>

#include "base/parse_object.h"

class MacMobility {
public:
    static const int kSize = 8;
    typedef boost::array<uint8_t, kSize> bytes_type;

    MacMobility(uint32_t seq);
    explicit MacMobility(const bytes_type &data);

    uint32_t sequence_number() const;

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

#endif
