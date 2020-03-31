/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_ESI_LABEL_H_
#define SRC_BGP_EXTENDED_COMMUNITY_ESI_LABEL_H_

#include <array>
#include <stdint.h>

#include <string>

#include "base/parse_object.h"
#include "bgp/extended-community/types.h"

class EsiLabel {
public:
    static const int kSize = 8;
    typedef std::array<uint8_t, kSize> bytes_type;

    explicit EsiLabel(const bytes_type &data);
    explicit EsiLabel(bool single_active);
    std::string ToString() const;
    bool single_active() const { return (data_[2] & 0x01); }

    const bytes_type &GetExtCommunity() const {
        return data_;
    }
    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }

private:
    std::string flags() const;
    int label() const;

    bytes_type data_;
};

#endif  // SRC_BGP_EXTENDED_COMMUNITY_ESI_LABEL_H_
