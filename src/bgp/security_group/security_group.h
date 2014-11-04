/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_security_group_h
#define ctrlplane_security_group_h

#include <boost/array.hpp>
#include <boost/system/error_code.hpp>

#include "base/parse_object.h"
#include "bgp/bgp_common.h"

class SecurityGroup {
public:
    static const int kSize = 8;
    static const uint32_t kMinGlobalId = 1000000;
    static const uint32_t kMaxGlobalId = 1999999;
    typedef boost::array<uint8_t, kSize> bytes_type;

    SecurityGroup(as_t asn, uint32_t id);
    explicit SecurityGroup(const bytes_type &data);

    as_t as_number() const;
    uint32_t security_group_id() const;
    bool IsGlobal() const;

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
