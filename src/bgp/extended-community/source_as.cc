/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/source_as.h"

#include <algorithm>

#include "net/address.h"

using std::copy;
using std::string;

SourceAs SourceAs::null_sas;

SourceAs::SourceAs() {
    data_.fill(0);
}

SourceAs::SourceAs(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

string SourceAs::ToString() const {
    uint8_t data[SourceAs::kSize];
    copy(data_.begin(), data_.end(), &data[0]);
    char temp[50];
    if (data[0] == BgpExtendedCommunityType::TwoOctetAS) {
        uint16_t asn = get_value(data + 2, 2);
        uint32_t num = get_value(data + 4, 4);
        snprintf(temp, sizeof(temp), "source-as:%u:%u", asn, num);
        return string(temp);
    } else if (data[0] == BgpExtendedCommunityType::FourOctetAS) {
        uint32_t asn = get_value(data + 2, 4);
        uint16_t num = get_value(data + 6, 2);
        snprintf(temp, sizeof(temp), "source-as:%u:%u", asn, num);
        return string(temp);
    }
    return "";
}

SourceAs SourceAs::FromString(const string &str,
    boost::system::error_code *errorp) {
    SourceAs sas;
    uint8_t data[SourceAs::kSize];

    // source-as:1:2
    size_t pos = str.find(':');
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SourceAs::null_sas;
    }

    string first(str.substr(0, pos));
    if (first != "source-as") {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SourceAs::null_sas;
    }

    string rest(str.substr(pos+1));

    pos = rest.find(':');
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SourceAs::null_sas;
    }

    boost::system::error_code ec;
    string second(rest.substr(0, pos));
    int offset;
    char *endptr;
    // Try ASN
    int64_t asn = strtol(second.c_str(), &endptr, 10);
    if (asn == 0 || asn >= 65535 || *endptr != '\0') {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
       }
       return SourceAs::null_sas;
    }

    data[0] = BgpExtendedCommunityType::TwoOctetAS;
    data[1] = BgpExtendedCommunitySubType::SourceAS;
    put_value(&data[2], 2, asn);
    offset = 4;

    string third(rest.substr(pos+1));
    uint64_t value = strtol(third.c_str(), &endptr, 10);
    if (*endptr != '\0') {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SourceAs::null_sas;
    }

    // Check assigned number for type 0.
    if (value > 0xFFFFFFFF) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SourceAs::null_sas;
    }

    put_value(&data[offset], SourceAs::kSize - offset, value);
    copy(&data[0], &data[SourceAs::kSize], sas.data_.begin());
    return sas;
}

