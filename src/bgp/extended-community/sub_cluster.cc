/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
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

uint32_t SubCluster::GetAsn() const {
    if (data_[0] == BgpExtendedCommunityType::Experimental) {
        return get_value(&data_[2], 2);
    }

    if (data_[0] == BgpExtendedCommunityType::Experimental4ByteAs) {
        return get_value(&data_[2], 4);
    }

    return 0;
}

SubCluster::SubCluster(const uint32_t asn, const uint32_t ri_index) {
    data_[0] = BgpExtendedCommunityType::Experimental;
    data_[1] = BgpExtendedCommunitySubType::SubCluster;
    put_value(&data_[2], 2, asn);
    put_value(&data_[4], SubCluster::kSize - 4, ri_index);
}

string SubCluster::ToString() const {
    uint8_t data[SubCluster::kSize];
    copy(data_.begin(), data_.end(), &data[0]);
    char temp[50];
    if (data[0] == BgpExtendedCommunityType::Experimental) {
        uint16_t asn = get_value(data + 2, 2);
        uint32_t num = get_value(data + 4, 4);
        snprintf(temp, sizeof(temp), "subcluster:%u:%u", asn, num);
        return string(temp);
    } else if (data[0] == BgpExtendedCommunityType::Experimental4ByteAs) {
        uint32_t asn = get_value(data + 2, 4);
        uint16_t num = get_value(data + 6, 2);
        snprintf(temp, sizeof(temp), "subcluster:%u:%u", asn, num);
        return string(temp);
    }
    return "";
}

SubCluster SubCluster::FromString(const string &str,
    boost::system::error_code *errorp) {
    SubCluster sas;
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
    int offset;
    char *endptr;
    // Get ASN: We only support Experimental for now
    int64_t asn = strtol(second.c_str(), &endptr, 10);
    if (asn == 0 || asn >= 65535 || *endptr != '\0') {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
       }
       return SubCluster::null_sub_cluster;
    }

    data[0] = BgpExtendedCommunityType::Experimental;
    data[1] = BgpExtendedCommunitySubType::SubCluster;
    put_value(&data[2], 2, asn);
    offset = 4;

    string third(rest.substr(pos+1));
    uint64_t value = strtol(third.c_str(), &endptr, 10);
    if (*endptr != '\0') {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SubCluster::null_sub_cluster;
    }

    // Check assigned number for type 0.
    if (value > 0xFFFFFFFF) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SubCluster::null_sub_cluster;
    }

    put_value(&data[offset], SubCluster::kSize - offset, value);
    copy(&data[0], &data[SubCluster::kSize], sas.data_.begin());
    return sas;
}
