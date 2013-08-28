/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_tunnel_encap_h
#define ctrlplane_tunnel_encap_h

#include "net/tunnel_encap_type.h"

#include <boost/array.hpp>
#include <boost/system/error_code.hpp>

#include "base/parse_object.h"

class TunnelEncap {
public:
    static const int kSize = 8;
    typedef boost::array<uint8_t, kSize> bytes_type;

    TunnelEncap();
    TunnelEncap(TunnelEncapType::Encap encap);
    TunnelEncap(std::string encap);
    explicit TunnelEncap(const bytes_type &data);

    TunnelEncapType::Encap tunnel_encap() const;

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
