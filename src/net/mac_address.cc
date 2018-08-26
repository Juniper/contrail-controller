/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

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
const MacAddress MacAddress::kMulticastMac(0x01, 0x00, 0x5E, 0, 0, 0);

MacAddress::MacAddress() {
    addr_ = kZeroMac;
}

MacAddress::MacAddress(const uint8_t *data) {
    memcpy(&addr_, data, sizeof(addr_));
}

bool MacAddress::IsZero() const {
    return CompareTo(ZeroMac()) == 0;
}

bool MacAddress::IsBroadcast() const {
    return CompareTo(BroadcastMac()) == 0;
}

bool MacAddress::IsMulticast() const {
    return CompareTo(MulticastMac(), 3) == 0;
}

MacAddress::MacAddress(unsigned int a, unsigned int b, unsigned int c,
                       unsigned int d, unsigned int e, unsigned int f) {
    u_int8_t *p = (u_int8_t *)&addr_;

    p[0] = a; p[1] = b; p[2] = c; p[3] = d; p[4] = e; p[5] = f;
}

MacAddress::MacAddress(const std::string &s,
                       boost::system::error_code *errorp) {
    *this = FromString(s, errorp);
}

string MacAddress::ToString() const {
    static char hexchars[] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };
    char temp[32];
    const u_int8_t *addr = (u_int8_t *) &addr_;
    int tidx = 0;
    for (int bidx = 0; bidx < 6; ++bidx) {
        if (bidx != 0)
            temp[tidx++] = ':';
        temp[tidx++] = hexchars[(addr[bidx] >> 4) & 0x0F];
        temp[tidx++] = hexchars[addr[bidx] & 0x0F];
    }
    temp[tidx] = '\0';
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

int MacAddress::CompareTo(const MacAddress &rhs, int len) const {
    if (len == 0)
        return memcmp(&addr_, &rhs.addr_, sizeof(addr_));
    return memcmp(&addr_, &rhs.addr_, len);
}

bool MacAddress::ToArray(u_int8_t *p, size_t s) const {
    if (s < size())
        return false;
    memcpy(p, &addr_, size());
    return true;
}

MacAddress &MacAddress::operator=(const u_int8_t *c) {
    memcpy(&addr_, c, size());
    return *this;
}

MacAddress &MacAddress::operator=(const struct sockaddr *sa) {
    memcpy(&addr_, sa->sa_data, size());
    return *this;
}
