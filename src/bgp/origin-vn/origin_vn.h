/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ORIGIN_VN_ORIGIN_VN_H_
#define SRC_BGP_ORIGIN_VN_ORIGIN_VN_H_

#include <array>
#include <boost/system/error_code.hpp>

#include <string>

#include "base/parse_object.h"
#include "bgp/bgp_common.h"

class OriginVn {
public:
    static const int kSize = 8;
    static const int kMinGlobalId = 8000000;
    static OriginVn null_originvn;
    typedef std::array<uint8_t, kSize> bytes_type;

    OriginVn();
    OriginVn(as_t asn, uint32_t vn_idx);
    explicit OriginVn(const bytes_type &data);

    bool IsNull() { return operator==(OriginVn::null_originvn); }
    bool IsGlobal() const;

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

class OriginVn4ByteAs {
public:
    static const int kSize = 8;
    static const int kMinGlobalId = 8000000;
    static OriginVn4ByteAs null_originvn;
    typedef std::array<uint8_t, kSize> bytes_type;

    OriginVn4ByteAs();
    OriginVn4ByteAs(as_t asn, uint32_t vn_idx);
    explicit OriginVn4ByteAs(const bytes_type &data);

    bool IsNull() { return operator==(OriginVn4ByteAs::null_originvn); }
    bool IsGlobal() const;

    as_t as_number() const;
    int vn_index() const;

    const bytes_type &GetExtCommunity() const {
        return data_;
    }

    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }

    bool operator<(const OriginVn4ByteAs &rhs) const {
        return data_ < rhs.data_;
    }
    bool operator==(const OriginVn4ByteAs &rhs) const {
        return data_ == rhs.data_;
    }

    std::string ToString();
    static OriginVn4ByteAs FromString(const std::string &str,
        boost::system::error_code *error = NULL);

private:
    bytes_type data_;
};

#endif  // SRC_BGP_ORIGIN_VN_ORIGIN_VN_H_
