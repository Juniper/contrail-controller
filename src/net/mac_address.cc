/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdint.h>
#include "net/mac_address.h"

#include <cstring>
#include <cstdio>

#include "base/parse_object.h"

using namespace std;

MacAddress::MacAddress() {
    memset(data_, 0, kSize);
}

MacAddress::MacAddress(const uint8_t *data) {
    memcpy(data_, data, kSize);
}

string MacAddress::ToString() const {
    char temp[32];
    snprintf(temp, sizeof(temp), "%02x:%02x:%02x:%02x:%02x:%02x",
        data_[0], data_[1], data_[2], data_[3], data_[4], data_[5]);
    return temp;
}

MacAddress MacAddress::FromString(const string &str, boost::system::error_code *errorp) {
    uint8_t data[kSize];
    char extra;
    int ret = sscanf(str.c_str(), "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx%c",
        &data[0], &data[1], &data[2], &data[3], &data[4], &data[5], &extra);
    if (ret != kSize || strchr(str.c_str(), 'x') || strchr(str.c_str(), 'X')) {
        if (errorp != NULL)
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        return MacAddress();
    }
    return MacAddress(data);
}

int MacAddress::CompareTo(const MacAddress &rhs) const {
    return memcmp(data_, rhs.data_, kSize);
}
