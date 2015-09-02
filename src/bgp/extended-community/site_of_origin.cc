/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/site_of_origin.h"

#include <algorithm>

#include "net/address.h"

using std::copy;
using std::string;

SiteOfOrigin SiteOfOrigin::null_soo;

SiteOfOrigin::SiteOfOrigin() {
    data_.fill(0);
}

SiteOfOrigin::SiteOfOrigin(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

string SiteOfOrigin::ToString() const {
    uint8_t data[SiteOfOrigin::kSize];
    copy(data_.begin(), data_.end(), &data[0]);
    if (data[0] == 0) {
        uint16_t asn = get_value(data + 2, 2);
        uint32_t num = get_value(data + 4, 4);
        char temp[50];
        snprintf(temp, sizeof(temp), "soo:%u:%u", asn, num);
        return string(temp);
    } else {
        Ip4Address addr(get_value(data + 2, 4));
        uint16_t num = get_value(data + 6, 2);
        char temp[50];
        snprintf(temp, sizeof(temp), "soo:%s:%u",
                 addr.to_string().c_str(), num);
        return string(temp);
    }
}

SiteOfOrigin SiteOfOrigin::FromString(const string &str,
    boost::system::error_code *errorp) {
    SiteOfOrigin soo;
    uint8_t data[SiteOfOrigin::kSize];

    // soo:1:2 OR soo:1.2.3.4:3
    size_t pos = str.find(':');
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SiteOfOrigin::null_soo;
    }

    string first(str.substr(0, pos));
    if (first != "soo") {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SiteOfOrigin::null_soo;
    }

    string rest(str.substr(pos+1));

    pos = rest.find(':');
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SiteOfOrigin::null_soo;
    }

    boost::system::error_code ec;
    string second(rest.substr(0, pos));
    Ip4Address addr = Ip4Address::from_string(second, ec);
    int offset;
    char *endptr;
    if (ec.value() != 0) {
        // Not an IP address. Try ASN
        int64_t asn = strtol(second.c_str(), &endptr, 10);
        if (asn == 0 || asn >= 65535 || *endptr != '\0') {
            if (errorp != NULL) {
                *errorp =
                    make_error_code(boost::system::errc::invalid_argument);
            }
            return SiteOfOrigin::null_soo;
        }

        data[0] = 0x00;
        data[1] = 0x03;
        put_value(&data[2], 2, asn);
        offset = 4;
    } else {
        data[0] = 0x01;
        data[1] = 0x03;
        uint32_t l_addr = addr.to_ulong();
        put_value(&data[2], 4, l_addr);
        offset = 6;
    }

    string third(rest.substr(pos+1));
    uint64_t value = strtol(third.c_str(), &endptr, 10);
    if (*endptr != '\0') {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SiteOfOrigin::null_soo;
    }

    // Check assigned number for type 0.
    if (offset == 4 && value > 0xFFFFFFFF) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SiteOfOrigin::null_soo;
    }

    // Check assigned number for type 1.
    if (offset == 6 && value > 0xFFFF) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SiteOfOrigin::null_soo;
    }

    put_value(&data[offset], SiteOfOrigin::kSize - offset, value);
    copy(&data[0], &data[SiteOfOrigin::kSize], soo.data_.begin());
    return soo;
}

