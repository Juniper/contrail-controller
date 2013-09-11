/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_rtarget_address_h
#define ctrlplane_rtarget_address_h

#include <string>
#include <boost/array.hpp>
#include <boost/system/error_code.hpp>

#include "base/parse_object.h"

class RouteTarget {
public:
    static const int kSize = 8;
    static RouteTarget null_rtarget;
    typedef boost::array<uint8_t, kSize> bytes_type;

    RouteTarget();
    explicit RouteTarget(const bytes_type &data);

    std::string ToString() const;

    bool IsNull() { return operator==(RouteTarget::null_rtarget); }
    uint8_t Type() { return data_[0]; }
    uint8_t Subtype() { return data_[1]; }

    bool operator<(const RouteTarget &rhs) const {
        return data_ < rhs.data_;
    }
    bool operator==(const RouteTarget &rhs) const {
        return data_ == rhs.data_;
    }

    static RouteTarget FromString(const std::string &str, 
                                  boost::system::error_code *error = NULL);

    const bytes_type &GetExtCommunity() const {
        return data_;
    }

    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }

private:
    bytes_type data_;
};

#endif
