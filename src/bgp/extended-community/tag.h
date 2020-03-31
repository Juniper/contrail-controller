/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_TAG_H_
#define SRC_BGP_EXTENDED_COMMUNITY_TAG_H_

#include <array>
#include <stdint.h>
#include <string>

#include "base/parse_object.h"
#include "bgp/bgp_common.h"

class Tag {
public:
    static const int kSize = 8;
    static const int kMinGlobalId = 8000000;
    typedef std::array<uint8_t, kSize> bytes_type;

    explicit Tag(const bytes_type &data);
    explicit Tag(as2_t asn, int tag);
    std::string ToString() const;

    bool IsGlobal() const;
    as2_t as_number() const;
    int tag() const;

    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }

private:
    bytes_type data_;
};

class Tag4ByteAs {
public:
    static const int kSize = 8;
    static const int kMinGlobalId = 8000000;
    typedef std::array<uint8_t, kSize> bytes_type;

    explicit Tag4ByteAs(const bytes_type &data);
    explicit Tag4ByteAs(as_t asn, int tag);
    std::string ToString() const;

    bool IsGlobal() const;
    as_t as_number() const;
    int tag() const;

    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }

private:
    bytes_type data_;
};

#endif  // SRC_BGP_EXTENDED_COMMUNITY_TAG_H_
