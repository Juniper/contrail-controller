/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_TUNNEL_ENCAP_TUNNEL_ENCAP_H_
#define SRC_BGP_TUNNEL_ENCAP_TUNNEL_ENCAP_H_

#include "net/tunnel_encap_type.h"

#include <array>
#include <boost/system/error_code.hpp>

#include <string>

#include "base/parse_object.h"

class TunnelEncap {
public:
    static const int kSize = 8;
    typedef std::array<uint8_t, kSize> bytes_type;

    explicit TunnelEncap(TunnelEncapType::Encap encap);
    explicit TunnelEncap(std::string encap);
    explicit TunnelEncap(const bytes_type &data);

    TunnelEncapType::Encap tunnel_encap() const;

    const bytes_type &GetExtCommunity() const {
        return data_;
    }

    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }
    std::string ToString();
    std::string ToXmppString();

private:
    bytes_type data_;
};

#endif  // SRC_BGP_TUNNEL_ENCAP_TUNNEL_ENCAP_H_
