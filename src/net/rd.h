/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_rd_h
#define ctrlplane_rd_h

#include <boost/system/error_code.hpp>
#include "base/parse_object.h"

class RouteDistinguisher {
public:
    static const size_t kSize = 8;
    static RouteDistinguisher kZeroRd;

    RouteDistinguisher();

    explicit RouteDistinguisher(const uint8_t *data);
    RouteDistinguisher(uint32_t address, uint16_t vrf_id);

    static RouteDistinguisher FromString(
        const std::string &str,
        boost::system::error_code *error = NULL);

    bool IsZero() const { return CompareTo(RouteDistinguisher::kZeroRd) == 0; }
    uint16_t Type() const { return get_value(data_, 2); }

    int CompareTo(const RouteDistinguisher &rhs) const;
    bool operator==(const RouteDistinguisher &rhs) const {
        return CompareTo(rhs) == 0;
    }
    bool operator<(const RouteDistinguisher &rhs) const {
        return CompareTo(rhs) < 0;
    }
    bool operator>(const RouteDistinguisher &rhs) const {
        return CompareTo(rhs) > 0;
    }

    std::string ToString() const;
    const uint8_t *GetData() const { return data_; }

private:
    uint8_t data_[kSize];
};

#endif
