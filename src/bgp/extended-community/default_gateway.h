/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_DEFAULT_GATEWAY_H_
#define SRC_BGP_EXTENDED_COMMUNITY_DEFAULT_GATEWAY_H_

#include "bgp/extended-community/types.h"

#include <array>
#include <stdint.h>

#include <string>

class DefaultGateway {
public:
    static const int kSize = 8;
    typedef std::array<uint8_t, kSize> bytes_type;

    explicit DefaultGateway(const bytes_type &data);
    std::string ToString() const;

private:
    bytes_type data_;
};

#endif  // SRC_BGP_EXTENDED_COMMUNITY_DEFAULT_GATEWAY_H_
