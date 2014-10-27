/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_default_gateway_h
#define ctrlplane_default_gateway_h

#include <boost/array.hpp>
#include <stdint.h>

class DefaultGateway {
public:
    static const int kSize = 8;
    typedef boost::array<uint8_t, kSize> bytes_type;

    explicit DefaultGateway(const bytes_type &data);
    std::string ToString() const;

private:
    bytes_type data_;
};

#endif
