/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_RTARGET_RTARGET_ADDRESS_H_
#define SRC_BGP_RTARGET_RTARGET_ADDRESS_H_

#include <array>
#include <boost/system/error_code.hpp>

#include <set>
#include <string>

#include <base/address.h>

class RouteTarget {
public:
    static const int kSize = 8;
    static RouteTarget null_rtarget;
    typedef std::array<uint8_t, kSize> bytes_type;
    typedef std::set<RouteTarget> List;

    RouteTarget();
    explicit RouteTarget(const bytes_type &data);
    RouteTarget(const Ip4Address &address, uint16_t num);

    std::string ToString() const;

    bool IsNull() const { return operator==(RouteTarget::null_rtarget); }
    uint8_t Type() { return data_[0]; }
    uint8_t Subtype() { return data_[1]; }

    bool operator<(const RouteTarget &rhs) const {
        return data_ < rhs.data_;
    }
    bool operator>(const RouteTarget &rhs) const {
        return data_ > rhs.data_;
    }
    bool operator==(const RouteTarget &rhs) const {
        return data_ == rhs.data_;
    }

    static RouteTarget FromString(const std::string &str,
                                  boost::system::error_code *error = NULL);

    const bytes_type &GetExtCommunity() const {
        return data_;
    }

    const uint64_t GetExtCommunityValue() const;

private:
    bytes_type data_;
};

#endif  // SRC_BGP_RTARGET_RTARGET_ADDRESS_H_
