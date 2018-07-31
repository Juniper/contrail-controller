/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/types.h"
#include "bgp/origin-vn/origin_vn.h"

#include <stdio.h>

#include <algorithm>


using std::copy;
using std::string;

OriginVn OriginVn::null_originvn;

OriginVn::OriginVn() {
    data_.fill(0);
}

OriginVn::OriginVn(as2_t asn, uint32_t vn_index) {
    data_[0] = BgpExtendedCommunityType::Experimental;
    data_[1] = BgpExtendedCommunityExperimentalSubType::OriginVn;
    put_value(&data_[2], 2, asn);
    put_value(&data_[4], 4, vn_index);
}

OriginVn::OriginVn(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

OriginVn OriginVn::FromString(const string &str,
    boost::system::error_code *errorp) {
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
    int64_t asn = strtol(second.c_str(), &endptr, 10);
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

    data[0] = BgpExtendedCommunityType::Experimental;
    data[1] = BgpExtendedCommunityExperimentalSubType::OriginVn;
    put_value(&data[2], 2, asn);
    put_value(&data[4], 4, value);
    copy(&data[0], &data[OriginVn::kSize], origin_vn.data_.begin());
    return origin_vn;
}

as2_t OriginVn::as_number() const {
    if (data_[0] == BgpExtendedCommunityType::Experimental &&
            data_[1] == BgpExtendedCommunityExperimentalSubType::OriginVn) {
        as2_t as_number = get_value(data_.data() + 2, 2);
        return as_number;
    }
    return 0;
}

int OriginVn::vn_index() const {
    if (data_[0] == BgpExtendedCommunityType::Experimental &&
            data_[1] == BgpExtendedCommunityExperimentalSubType::OriginVn) {
        int vn_index = get_value(data_.data() + 4, 4);
        return vn_index;
    }
    return 0;
}

bool OriginVn::IsGlobal() const {
    return (vn_index() >= kMinGlobalId);
}

string OriginVn::ToString() {
    char temp[50];
    snprintf(temp, sizeof(temp), "originvn:%u:%u", as_number(), vn_index());
    return string(temp);
}

OriginVn4ByteAs OriginVn4ByteAs::null_originvn;

OriginVn4ByteAs::OriginVn4ByteAs() {
    data_.fill(0);
}

OriginVn4ByteAs::OriginVn4ByteAs(as4_t asn, uint32_t vn_index) {
    data_[0] = BgpExtendedCommunityType::Experimental4ByteAs;
    data_[1] = BgpExtendedCommunityExperimentalSubType::OriginVn;
    put_value(&data_[2], 4, asn);
    put_value(&data_[4], 2, vn_index);
}

OriginVn4ByteAs::OriginVn4ByteAs(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

OriginVn4ByteAs OriginVn4ByteAs::FromString(const string &str,
    boost::system::error_code *errorp) {
    OriginVn4ByteAs origin_vn;
    uint8_t data[OriginVn4ByteAs::kSize];
    size_t pos = str.find(':');
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return OriginVn4ByteAs::null_originvn;
    }

    string first(str.substr(0, pos));
    if (first != "originvn") {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return OriginVn4ByteAs::null_originvn;
    }

    string rest(str.substr(pos+1));

    pos = rest.find(':');
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return OriginVn4ByteAs::null_originvn;
    }

    string second(rest.substr(0, pos));
    char *endptr;
    int64_t asn = strtol(second.c_str(), &endptr, 10);
    if (asn == 0 || asn >= 65535 || *endptr != '\0') {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return OriginVn4ByteAs::null_originvn;
    }

    string third(rest.substr(pos+1));
    uint64_t value = strtol(third.c_str(), &endptr, 10);
    if (*endptr != '\0') {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return OriginVn4ByteAs::null_originvn;
    }

    // Check assigned number.
    if (value > 0xFFFFFFFF) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return OriginVn4ByteAs::null_originvn;
    }

    data[0] = BgpExtendedCommunityType::Experimental4ByteAs;
    data[1] = BgpExtendedCommunityExperimentalSubType::OriginVn;
    put_value(&data[2], 4, asn);
    put_value(&data[4], 2, value);
    copy(&data[0], &data[OriginVn4ByteAs::kSize], origin_vn.data_.begin());
    return origin_vn;
}

as4_t OriginVn4ByteAs::as_number() const {
    if (data_[0] == BgpExtendedCommunityType::Experimental4ByteAs &&
            data_[1] == BgpExtendedCommunityExperimentalSubType::OriginVn) {
        as4_t as_number = get_value(data_.data() + 2, 4);
        return as_number;
    }
    return 0;
}

int OriginVn4ByteAs::vn_index() const {
    if (data_[0] == BgpExtendedCommunityType::Experimental &&
            data_[1] == BgpExtendedCommunityExperimentalSubType::OriginVn) {
        int vn_index = get_value(data_.data() + 6, 2);
        return vn_index;
    }
    return 0;
}

bool OriginVn4ByteAs::IsGlobal() const {
    return (vn_index() >= kMinGlobalId);
}

string OriginVn4ByteAs::ToString() {
    char temp[50];
    snprintf(temp, sizeof(temp), "originvn:%u:%u", as_number(), vn_index());
    return string(temp);
}
