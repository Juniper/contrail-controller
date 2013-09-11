/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_origin_vn_h
#define ctrlplane_origin_vn_h

#include <string>
#include <boost/array.hpp>
#include <boost/system/error_code.hpp>

#include "base/parse_object.h"
#include "bgp/bgp_common.h"

class OriginVn {
public:
    static const int kSize = 8;
    static OriginVn null_originvn;
    typedef boost::array<uint8_t, kSize> bytes_type;

    OriginVn();
    OriginVn(as_t asn, uint32_t vn_idx);
    explicit OriginVn(const bytes_type &data);

    bool IsNull() { return operator==(OriginVn::null_originvn); }

    as_t as_number() const;
    int vn_index() const;

    const bytes_type &GetExtCommunity() const {
        return data_;
    }

    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }

    bool operator<(const OriginVn &rhs) const {
        return data_ < rhs.data_;
    }
    bool operator==(const OriginVn &rhs) const {
        return data_ == rhs.data_;
    }

    std::string ToString();
    static OriginVn FromString(const std::string &str,
        boost::system::error_code *error = NULL);

private:
    bytes_type data_;
};

#endif
