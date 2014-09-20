/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "net/rd.h"

#include "base/parse_object.h"
#include "net/address.h"

using namespace std;

RouteDistinguisher RouteDistinguisher::kZeroRd;

RouteDistinguisher::RouteDistinguisher() {
    memset(data_, 0, kSize);
}

RouteDistinguisher::RouteDistinguisher(const uint8_t *data) {
    memcpy(data_, data, kSize);
}

RouteDistinguisher::RouteDistinguisher(uint32_t address, uint16_t vrf_id) {
    data_[0] = 0;
    data_[1] = 0x1; // Type 1
    put_value(data_ + 2, 4, address);
    put_value(data_ + 6, 2, vrf_id);
}

std::string RouteDistinguisher::ToString() const {
    uint16_t rd_type = get_value(data_, 2);
    if (rd_type == 0) {
        char temp[50];
        uint16_t asn = get_value(data_ + 2, 2);
        uint32_t value = get_value(data_ + 4, 4);
        snprintf(temp, sizeof(temp), "%u:%u", asn, value);
        return std::string(temp);
    } else if (rd_type == 1) {
        Ip4Address ip(get_value(data_ + 2, 4));
        uint16_t value = get_value(data_ + 6, 2);
        char temp[20];
        snprintf(temp, sizeof(temp), ":%u", value);
        return ip.to_string() + temp;
    }

    return "";
}

RouteDistinguisher RouteDistinguisher::FromString(
    const string &str, boost::system::error_code *errorp) {
    RouteDistinguisher rd;
    size_t pos = str.rfind(':');
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return RouteDistinguisher::kZeroRd;
    }

    boost::system::error_code ec;
    string first(str.substr(0, pos));
    Ip4Address addr = Ip4Address::from_string(first, ec);
    int offset;
    char *endptr;
    long asn = -1;
    if (ec.value() != 0) {
        //Not an IP address. Try ASN
        asn = strtol(first.c_str(), &endptr, 10);
        if (asn >= 65535 || *endptr != '\0') {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return RouteDistinguisher::kZeroRd;
        }

        put_value(rd.data_, 2, 0);
        put_value(rd.data_ + 2, 2, asn);
        offset = 4;
    } else {
        put_value(rd.data_, 2, 1);
        put_value(rd.data_ + 2, 4, addr.to_ulong());
        offset = 6;
    }

    string second(str, pos + 1);
    uint64_t value = strtol(second.c_str(), &endptr, 10);
    if (*endptr != '\0') {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return RouteDistinguisher::kZeroRd;
    }

    // ASN 0 is not allowed if the assigned number is not 0.
    if (asn == 0 && value != 0) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return RouteDistinguisher::kZeroRd;
    }

    // Check assigned number for type 0.
    if (offset == 4 && value > 0xFFFFFFFF) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return RouteDistinguisher::kZeroRd;
    }

    // Check assigned number for type 1.
    if (offset == 6 && value > 0xFFFF) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return RouteDistinguisher::kZeroRd;
    }

    put_value(rd.data_ + offset, 8 - offset, value);
    return rd;
}

int RouteDistinguisher::CompareTo(const RouteDistinguisher &rhs) const {
    return memcmp(data_, rhs.data_, kSize);
}
