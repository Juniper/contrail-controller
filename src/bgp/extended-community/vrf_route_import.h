/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_VRF_ROUTE_IMPORT_H_
#define SRC_BGP_EXTENDED_COMMUNITY_VRF_ROUTE_IMPORT_H_

#include <array>
#include <boost/system/error_code.hpp>

#include <string>

#include "base/parse_object.h"
#include "bgp/extended-community/types.h"
#include "base/address.h"

class VrfRouteImport {
public:
    static const int kSize = 8;
    static VrfRouteImport null_rt_import;
    typedef std::array<uint8_t, kSize> bytes_type;

    VrfRouteImport();
    explicit VrfRouteImport(const bytes_type &data);
    VrfRouteImport(const uint32_t bgp_id, const uint32_t ri_index);

    bool IsNull() const { return operator==(VrfRouteImport::null_rt_import); }
    uint8_t Type() const { return data_[0]; }
    uint8_t Subtype() const { return data_[1]; }
    Ip4Address GetIPv4Address() const;
    uint16_t GetNumber() const;

    bool operator<(const VrfRouteImport &rhs) const {
        return data_ < rhs.data_;
    }
    bool operator>(const VrfRouteImport &rhs) const {
        return data_ > rhs.data_;
    }
    bool operator==(const VrfRouteImport &rhs) const {
        return data_ == rhs.data_;
    }
    bool operator!=(const VrfRouteImport &rhs) const {
        return data_ != rhs.data_;
    }

    const bytes_type &GetExtCommunity() const { return data_; }
    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }

    std::string ToString() const;
    static VrfRouteImport FromString(const std::string &str,
        boost::system::error_code *error = NULL);

private:
    bytes_type data_;
};

#endif  // SRC_BGP_EXTENDED_COMMUNITY_VRF_ROUTE_IMPORT_H_
