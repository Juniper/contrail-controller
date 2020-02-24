/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/sub_cluster.h"

#include <algorithm>

#include "base/address.h"

using std::copy;
using std::string;

SubCluster SubCluster::null_sub_cluster;

SubCluster::SubCluster() {
    data_.fill(0);
}

SubCluster::SubCluster(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

SubCluster::SubCluster(as_t asn, const uint32_t id) {
    if (asn <= AS2_MAX) {
        data_[0] = BgpExtendedCommunityType::Experimental;
        put_value(&data_[2], 2, asn);
        put_value(&data_[4], 4, id);
    } else {
        data_[0] = BgpExtendedCommunityType::Experimental4ByteAs;
        put_value(&data_[2], 4, asn);
        put_value(&data_[6], SubCluster::kSize - 6, id);
    }
    data_[1] = BgpExtendedCommunitySubType::SubCluster;
}

uint32_t SubCluster::GetId() const {
    if (data_[0] == BgpExtendedCommunityType::Experimental) {
        return get_value(&data_[4], 4);
    }

    if (data_[0] == BgpExtendedCommunityType::Experimental4ByteAs) {
        return get_value(&data_[6], 2);
    }
    return 0;
}

uint32_t SubCluster::GetAsn() const {
    if (data_[0] == BgpExtendedCommunityType::Experimental) {
        return get_value(&data_[2], 2);
    }

    if (data_[0] == BgpExtendedCommunityType::Experimental4ByteAs) {
        return get_value(&data_[2], 4);
    }
    return 0;
}

string SubCluster::ToString() const {
    uint8_t data[SubCluster::kSize];
    copy(data_.begin(), data_.end(), &data[0]);
    char temp[50];

    if (data_[0] == BgpExtendedCommunityType::Experimental) {
        uint16_t asn = get_value(data + 2, 2);
        uint32_t id = get_value(data + 4, 4);
        snprintf(temp, sizeof(temp), "subcluster:%u:%u", asn, id);
        return string(temp);
    }

    if (data_[0] == BgpExtendedCommunityType::Experimental4ByteAs) {
        uint32_t asn = get_value(data + 2, 4);
        uint16_t id = get_value(data + 6, 2);
        snprintf(temp, sizeof(temp), "subcluster:%u:%u", asn, id);
        return string(temp);
    }
    return "";
}

SubCluster SubCluster::FromString(const string &str,
    boost::system::error_code *errorp) {
    SubCluster sc;
    uint8_t data[SubCluster::kSize];

    // subcluster:1:2
    size_t pos = str.find(':');
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SubCluster::null_sub_cluster;
    }

    string first(str.substr(0, pos));
    if (first != "subcluster") {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SubCluster::null_sub_cluster;
    }

    string rest(str.substr(pos+1));

    pos = rest.find(':');
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SubCluster::null_sub_cluster;
    }

    boost::system::error_code ec;
    string second(rest.substr(0, pos));
    int offset = 6;
    char *endptr;
    // Get ASN
    uint64_t asn = strtoll(second.c_str(), &endptr, 10);
    if (asn == 0 || asn > 0xFFFFFFFF || *endptr != '\0') {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SubCluster::null_sub_cluster;
    }

    string third(rest.substr(pos+1));
    uint64_t id = strtoll(third.c_str(), &endptr, 10);
    if (*endptr != '\0') {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SubCluster::null_sub_cluster;
    }

    // Check assigned number.
    if ((asn > AS2_MAX && id > 0xFFFF) || id == 0 || id > 0xFFFFFFFF) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SubCluster::null_sub_cluster;
    }

    if (asn <= AS2_MAX) {
        data[0] = BgpExtendedCommunityType::Experimental;
        put_value(&data[2], 2, asn);
        offset = 4;
    } else {
        data[0] = BgpExtendedCommunityType::Experimental4ByteAs;
        put_value(&data[2], 4, asn);
        offset = 6;
    }
    data[1] = BgpExtendedCommunitySubType::SubCluster;
    put_value(&data[offset], SubCluster::kSize - offset, id);
    copy(&data[0], &data[SubCluster::kSize], sc.data_.begin());
    return sc;
}
