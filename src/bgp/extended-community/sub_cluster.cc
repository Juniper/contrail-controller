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

SubCluster::SubCluster(as_t asn, uint8_t type, const uint16_t sub_cluster_id) {
    if (type == BgpExtendedCommunityType::Experimental) {
        data_[0] = BgpExtendedCommunityType::Experimental;
        put_value(&data_[2], 2, asn);
    } else if (type == BgpExtendedCommunityType::Experimental4ByteAs) {
        data_[0] = BgpExtendedCommunityType::Experimental4ByteAs;
        put_value(&data_[2], 4, asn);
    }
    data_[1] = BgpExtendedCommunitySubType::SubCluster;
    put_value(&data_[6], SubCluster::kSize - 6, sub_cluster_id);
}

uint16_t SubCluster::GetSubClusterId() const {
    return get_value(&data_[6], 2);
}

string SubCluster::ToString() const {
    uint8_t data[SubCluster::kSize];
    copy(data_.begin(), data_.end(), &data[0]);
    char temp[50];
    if (data[0] == BgpExtendedCommunityType::Experimental) {
        uint16_t asn = get_value(data + 2, 2);
        uint32_t sub_cluster_id = get_value(data + 6, 2);
        snprintf(temp, sizeof(temp), "subcluster:%u:%u", asn, sub_cluster_id);
        return string(temp);
    } else if (data[0] == BgpExtendedCommunityType::Experimental4ByteAs) {
        uint32_t asn = get_value(data + 2, 4);
        uint16_t sub_cluster_id = get_value(data + 6, 2);
        snprintf(temp, sizeof(temp), "subcluster:%uL:%u", asn, sub_cluster_id);
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
    bool is_as4 = false;
    if (second.c_str()[pos - 1] == 'L') {
        is_as4 = true;
        second = second.substr(0, pos - 1);
    }
    int64_t asn = strtol(second.c_str(), &endptr, 10);
    if (!is_as4) {
        if (asn == 0 || asn >= 0xFFFF || *endptr != '\0') {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return SubCluster::null_sub_cluster;
        }
    } else {
        if (asn == 0 || asn >= 0xFFFFFFFF || *endptr != '\0') {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return SubCluster::null_sub_cluster;
        }
    }
    if (asn > AS2_MAX || is_as4) {
        data[0] = BgpExtendedCommunityType::Experimental4ByteAs;
        put_value(&data[2], 4, asn);
    } else {
        data[0] = BgpExtendedCommunityType::Experimental;
        put_value(&data[2], 2, asn);
        offset = 4;
    }
    data[1] = BgpExtendedCommunitySubType::SubCluster;

    string third(rest.substr(pos+1));
    uint64_t value = strtol(third.c_str(), &endptr, 10);
    if (*endptr != '\0') {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SubCluster::null_sub_cluster;
    }

    // Check assigned number for type 0x82.
    if (offset == 4 && value > 0xFFFFFFFF) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SubCluster::null_sub_cluster;
    }

    // Check assigned number for type 0x80
    if (offset == 6 && value > 0xFFFF) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return SubCluster::null_sub_cluster;
    }

    put_value(&data[offset], SubCluster::kSize - offset, value);
    copy(&data[0], &data[SubCluster::kSize], sc.data_.begin());
    return sc;
}
