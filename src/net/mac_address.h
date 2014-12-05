/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_mac_address_h
#define ctrlplane_mac_address_h

#include <string>
#include "base/util.h"

class MacAddress {
public:
    static const int kSize = 6;

    MacAddress();
    explicit MacAddress(const uint8_t *data);

    MacAddress(const MacAddress &rhs) {
        memcpy(data_, rhs.data_, kSize);
    }

    static MacAddress FromString(const std::string &str,
        boost::system::error_code *error = NULL);

    MacAddress &operator=(const MacAddress &rhs) {
        memcpy(data_, rhs.data_, kSize);
        return *this;
    }

    int CompareTo(const MacAddress &rhs) const;
    bool operator==(const MacAddress &rhs) const {
        return CompareTo(rhs) == 0;
    }
    bool operator<(const MacAddress &rhs) const {
        return CompareTo(rhs) < 0;
    }
    bool operator>(const MacAddress &rhs) const {
        return CompareTo(rhs) > 0;
    }

    std::string ToString() const;
    const uint8_t *GetData() const { return data_; }

private:
    // Delete non-const copy constructor
    MacAddress(MacAddress &);

    uint8_t data_[kSize];
};

#endif
