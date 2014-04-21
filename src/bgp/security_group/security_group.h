/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_security_group_h
#define ctrlplane_security_group_h

#include <boost/array.hpp>
#include <boost/system/error_code.hpp>

#include "base/parse_object.h"

class SecurityGroup {
public:
    static const int kSize = 8;
    typedef boost::array<uint8_t, kSize> bytes_type;

    SecurityGroup(int asn, uint32_t id);
    explicit SecurityGroup(const bytes_type &data);

    uint32_t security_group_id() const;

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
