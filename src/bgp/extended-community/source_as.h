/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_SOURCE_AS_H_
#define SRC_BGP_EXTENDED_COMMUNITY_SOURCE_AS_H_

#include <array>
#include <boost/system/error_code.hpp>

#include <string>

#include "base/parse_object.h"
#include "bgp/extended-community/types.h"

class SourceAs {
public:
    static const int kSize = 8;
    static SourceAs null_sas;
    typedef std::array<uint8_t, kSize> bytes_type;

    SourceAs();
    explicit SourceAs(const bytes_type &data);
    SourceAs(const uint32_t asn, const uint32_t ri_index);

    uint32_t GetAsn() const;
    bool IsNull() const { return operator==(SourceAs::null_sas); }
    uint8_t Type() const { return data_[0]; }
    uint8_t Subtype() const { return data_[1]; }

    bool operator<(const SourceAs &rhs) const {
        return data_ < rhs.data_;
    }
    bool operator>(const SourceAs &rhs) const {
        return data_ > rhs.data_;
    }
    bool operator==(const SourceAs &rhs) const {
        return data_ == rhs.data_;
    }
    bool operator!=(const SourceAs &rhs) const {
        return data_ != rhs.data_;
    }

    const bytes_type &GetExtCommunity() const { return data_; }
    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }

    std::string ToString() const;
    static SourceAs FromString(const std::string &str,
        boost::system::error_code *error = NULL);

private:
    bytes_type data_;
};

#endif  // SRC_BGP_EXTENDED_COMMUNITY_SOURCE_AS_H_
