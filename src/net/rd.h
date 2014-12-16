/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_rd_h
#define ctrlplane_rd_h

#include "base/util.h"

class RouteDistinguisher {
public:
    static const size_t kSize = 8;
    static RouteDistinguisher null_rd;

    enum RDType {
        Type2ByteASBased = 0,
        TypeIpAddressBased = 1,
    };

    RouteDistinguisher();

    explicit RouteDistinguisher(const uint8_t *data);
    RouteDistinguisher(uint32_t address, uint16_t vrf_id);
    RouteDistinguisher(const RouteDistinguisher &rhs) {
        memcpy(data_, rhs.data_, kSize);
    }

    std::string ToString() const;
    static RouteDistinguisher FromString(
        const std::string &str,
        boost::system::error_code *error = NULL);

    RouteDistinguisher &operator=(const RouteDistinguisher &rhs) {
        memcpy(data_, rhs.data_, kSize);
        return *this;
    }

    bool IsNull() const { return CompareTo(RouteDistinguisher::null_rd) == 0; }
    uint16_t Type() const;
    uint32_t GetAddress() const;

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

    const uint8_t *GetData() const { return data_; }

private:
    uint8_t data_[kSize];
};

#endif
