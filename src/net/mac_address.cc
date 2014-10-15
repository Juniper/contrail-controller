/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdint.h>
#include "net/mac_address.h"

#include <cstring>
#include <cstdio>
#if defined(__linux__)
# include <netinet/ether.h>
#endif

#include "base/parse_object.h"

using namespace std;

const MacAddress MacAddress::kZeroMac;
const MacAddress MacAddress::kBroadcastMac(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);

MacAddress::MacAddress() {
    addr_ = kZeroMac;
}

MacAddress::MacAddress(const uint8_t *data) {
    memcpy(&addr_, data, sizeof(addr_));
}

bool MacAddress::IsBroadcast() const {
    return CompareTo(BroadcastMac()) == 0;
}

MacAddress::MacAddress(unsigned int a, unsigned int b, unsigned int c,
                       unsigned int d, unsigned int e, unsigned int f) {
    u_int8_t *p = (u_int8_t *)&addr_;

    p[0] = a; p[1] = b; p[2] = c; p[3] = d; p[4] = e; p[5] = f;
}

MacAddress::MacAddress(const std::string &s,
                       boost::system::error_code *errorp) {
    this->addr_ = FromString(s, errorp);
}

string MacAddress::ToString() const {
    char temp[32];
    const u_int8_t *a = (u_int8_t *)&addr_;
    snprintf(temp, sizeof(temp), "%02x:%02x:%02x:%02x:%02x:%02x",
        a[0], a[1], a[2], a[3], a[4], a[5]);
    return temp;
}

MacAddress MacAddress::FromString(const std::string &str,
                                  boost::system::error_code *errorp) {
    struct ether_addr a;
    u_int8_t *p = (u_int8_t*)&a;
    char extra;

    int ret = sscanf(str.c_str(), "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx%c",
        &p[0], &p[1], &p[2], &p[3], &p[4], &p[5], &extra);
    if ((size_t)ret != size() || strchr(str.c_str(), 'x') || strchr(str.c_str(), 'X')) {
        if (errorp != NULL)
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        return MacAddress();
    }
    return MacAddress(a);
}

int MacAddress::CompareTo(const MacAddress &rhs) const {
    return memcmp(&addr_, &rhs.addr_, sizeof(addr_));
}
