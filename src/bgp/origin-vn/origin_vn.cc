/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/origin-vn/origin_vn.h"

#include <stdio.h>

#include "base/parse_object.h"

using namespace std;
using boost::system::error_code;

OriginVn OriginVn::null_originvn;

OriginVn::OriginVn() {
    data_.fill(0);
}

OriginVn::OriginVn(as_t asn, uint32_t vn_index) {
    data_[0] = 0x80;
    data_[1] = 0x71;
    put_value(&data_[2], 2, asn);
    put_value(&data_[4], 4, vn_index);
}

OriginVn::OriginVn(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

OriginVn OriginVn::FromString(const string &str, error_code *errorp) {
    OriginVn origin_vn;
    uint8_t data[OriginVn::kSize];
    size_t pos = str.find(':');
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return OriginVn::null_originvn;
    }

    string first(str.substr(0, pos));
    if (first != "originvn") {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return OriginVn::null_originvn;
    }

    string rest(str.substr(pos+1));

    pos = rest.find(':');
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return OriginVn::null_originvn;
    }

    string second(rest.substr(0, pos));
    char *endptr;
    long asn = strtol(second.c_str(), &endptr, 10);
    if (asn == 0 || asn >= 65535 || *endptr != '\0') {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return OriginVn::null_originvn;
    }

    string third(rest.substr(pos+1));
    uint64_t value = strtol(third.c_str(), &endptr, 10);
    if (*endptr != '\0') {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return OriginVn::null_originvn;
    }

    // Check assigned number.
    if (value > 0xFFFFFFFF) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return OriginVn::null_originvn;
    }

    data[0] = 0x80;
    data[1] = 0x71;
    put_value(&data[2], 2, asn);
    put_value(&data[4], 4, value);
    copy(&data[0], &data[OriginVn::kSize], origin_vn.data_.begin());
    return origin_vn;
}

as_t OriginVn::as_number() const {
    uint8_t data[OriginVn::kSize];
    copy(data_.begin(), data_.end(), &data[0]);
    if (data[0] == 0x80 && data[1] == 0x71) {
        as_t as_number = get_value(data + 2, 2);
        return as_number;
    }
    return 0;
}

int OriginVn::vn_index() const {
    uint8_t data[OriginVn::kSize];
    copy(data_.begin(), data_.end(), &data[0]);
    if (data[0] == 0x80 && data[1] == 0x71) {
        int vn_index = get_value(data + 4, 4);
        return vn_index;
    }
    return 0;
}

std::string OriginVn::ToString() {
    char temp[50];
    snprintf(temp, sizeof(temp), "originvn:%u:%u", as_number(), vn_index());
    return std::string(temp);
}
