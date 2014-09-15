/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_mac_address_h
#define ctrlplane_mac_address_h

#include <boost/system/error_code.hpp>

class MacAddress {
public:
    static const int kSize = 6;
    static const MacAddress kBroadcastAddress;

    MacAddress();
    explicit MacAddress(const uint8_t *data);
    bool IsBroadcast() const;

    static MacAddress FromString(const std::string &str,
        boost::system::error_code *error = NULL);

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
    uint8_t data_[kSize];
};

#endif
