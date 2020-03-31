/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_SECURITY_GROUP_SECURITY_GROUP_H_
#define SRC_BGP_SECURITY_GROUP_SECURITY_GROUP_H_

#include <array>
#include <boost/system/error_code.hpp>

#include <string>

#include "base/parse_object.h"
#include "bgp/bgp_common.h"

class SecurityGroup {
public:
    static const int kSize = 8;
    static const uint32_t kMinGlobalId = 1;
    static const uint32_t kMaxGlobalId = 7999999;
    typedef std::array<uint8_t, kSize> bytes_type;

    SecurityGroup(as2_t asn, uint32_t id);
    explicit SecurityGroup(const bytes_type &data);

    as2_t as_number() const;
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

class SecurityGroup4ByteAs {
public:
    static const int kSize = 8;
    static const uint32_t kMinGlobalId = 1;
    static const uint32_t kMaxGlobalId = 7999999;
    typedef std::array<uint8_t, kSize> bytes_type;

    SecurityGroup4ByteAs(as_t asn, uint32_t id);
    explicit SecurityGroup4ByteAs(const bytes_type &data);

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

#endif  // SRC_BGP_SECURITY_GROUP_SECURITY_GROUP_H_
