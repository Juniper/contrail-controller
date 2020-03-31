/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_SITE_OF_ORIGIN_H_
#define SRC_BGP_EXTENDED_COMMUNITY_SITE_OF_ORIGIN_H_

#include <array>
#include <boost/system/error_code.hpp>

#include <string>

#include "base/parse_object.h"
#include "bgp/extended-community/types.h"

class SiteOfOrigin {
public:
    static const int kSize = 8;
    static SiteOfOrigin null_soo;
    typedef std::array<uint8_t, kSize> bytes_type;

    SiteOfOrigin();
    explicit SiteOfOrigin(const bytes_type &data);

    bool IsNull() const { return operator==(SiteOfOrigin::null_soo); }
    uint8_t Type() const { return data_[0]; }
    uint8_t Subtype() const { return data_[1]; }

    bool operator<(const SiteOfOrigin &rhs) const {
        return data_ < rhs.data_;
    }
    bool operator>(const SiteOfOrigin &rhs) const {
        return data_ > rhs.data_;
    }
    bool operator==(const SiteOfOrigin &rhs) const {
        return data_ == rhs.data_;
    }
    bool operator!=(const SiteOfOrigin &rhs) const {
        return data_ != rhs.data_;
    }

    const bytes_type &GetExtCommunity() const { return data_; }
    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }

    std::string ToString() const;
    static SiteOfOrigin FromString(const std::string &str,
        boost::system::error_code *error = NULL);

private:
    bytes_type data_;
};

#endif  // SRC_BGP_EXTENDED_COMMUNITY_SITE_OF_ORIGIN_H_
