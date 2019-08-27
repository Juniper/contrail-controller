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

OriginVn::OriginVn(as_t asn, uint32_t vn_index) {
    if (asn <= AS2_MAX) {
        data_[0] = BgpExtendedCommunityType::Experimental;
        put_value(&data_[2], 2, asn);
        put_value(&data_[4], 4, vn_index);
    } else {
        data_[0] = BgpExtendedCommunityType::Experimental4ByteAs;
        put_value(&data_[2], 4, asn);
        put_value(&data_[6], 2, vn_index);
    }
    data_[1] = BgpExtendedCommunityExperimentalSubType::OriginVn;
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
    if (asn == 0 || asn > 0xFFFFFFFF || *endptr != '\0') {
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
    if ((asn > AS2_MAX && value > 0xFFFF) || value > 0xFFFFFFFF) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return OriginVn::null_originvn;
    }

    if (asn <= AS2_MAX) {
        data[0] = BgpExtendedCommunityType::Experimental;
        put_value(&data[2], 2, asn);
        put_value(&data[4], 4, value);
    } else {
        data[0] = BgpExtendedCommunityType::Experimental4ByteAs;
        put_value(&data[2], 4, asn);
        put_value(&data[6], 2, value);
    }
    data[1] = BgpExtendedCommunityExperimentalSubType::OriginVn;
    copy(&data[0], &data[OriginVn::kSize], origin_vn.data_.begin());
    return origin_vn;
}

as_t OriginVn::as_number() const {
    if (data_[0] == BgpExtendedCommunityType::Experimental) {
        as2_t as_number = get_value(data_.data() + 2, 2);
        return as_number;
    }
    if (data_[0] == BgpExtendedCommunityType::Experimental4ByteAs) {
        as_t as_number = get_value(data_.data() + 2, 4);
        return as_number;
    }
    return 0;
}

int OriginVn::vn_index() const {
    if (data_[0] == BgpExtendedCommunityType::Experimental) {
        int vn_index = get_value(data_.data() + 4, 4);
        return vn_index;
    }
    if (data_[0] == BgpExtendedCommunityType::Experimental4ByteAs) {
        int vn_index = get_value(data_.data() + 6, 2);
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
