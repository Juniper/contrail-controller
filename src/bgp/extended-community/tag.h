/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_TAG_H_
#define SRC_BGP_EXTENDED_COMMUNITY_TAG_H_

#include <boost/array.hpp>
#include <stdint.h>
#include <string>

#include "base/parse_object.h"
#include "bgp/extended-community/types.h"

class Tag {
public:
    static const int kSize = 8;
    typedef boost::array<uint8_t, kSize> bytes_type;

    explicit Tag(const bytes_type &data);
    explicit Tag(int tag);
    std::string ToString() const;
    int tag() const;
    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }

private:
    bytes_type data_;
};

#endif  // SRC_BGP_EXTENDED_COMMUNITY_TAG_H_
