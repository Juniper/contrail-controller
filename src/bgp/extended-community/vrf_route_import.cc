/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/vrf_route_import.h"

#include <algorithm>

#include "base/address.h"

using std::copy;
using std::string;

VrfRouteImport VrfRouteImport::null_rt_import;

VrfRouteImport::VrfRouteImport() {
    data_.fill(0);
}

VrfRouteImport::VrfRouteImport(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

Ip4Address VrfRouteImport::GetIPv4Address() const {
    return Ip4Address(get_value(&data_[2], 4));
}

uint16_t VrfRouteImport::GetNumber() const {
    return get_value(&data_[6], 2);
}

VrfRouteImport::VrfRouteImport(const uint32_t bgp_id, const uint32_t ri_index) {
    data_[0] = BgpExtendedCommunityType::IPv4Address;
    data_[1] = BgpExtendedCommunitySubType::VrfRouteImport;
    put_value(&data_[2], 4, bgp_id);
    put_value(&data_[6], VrfRouteImport::kSize - 6, ri_index);
}

string VrfRouteImport::ToString() const {
    uint8_t data[VrfRouteImport::kSize];
    copy(data_.begin(), data_.end(), &data[0]);
    if (data[0] == BgpExtendedCommunityType::IPv4Address) {
        Ip4Address addr(get_value(data + 2, 4));
        uint16_t num = get_value(data + 6, 2);
        char temp[50];
        snprintf(temp, sizeof(temp), "rt-import:%s:%u",
                 addr.to_string().c_str(), num);
        return string(temp);
    }
    return "";
}

VrfRouteImport VrfRouteImport::FromString(const string &str,
    boost::system::error_code *errorp) {
    VrfRouteImport rt_import;
    uint8_t data[VrfRouteImport::kSize];

    // rt-import:1.2.3.4:3
    size_t pos = str.find(':');
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return VrfRouteImport::null_rt_import;
    }

    string first(str.substr(0, pos));
    if (first != "rt-import") {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return VrfRouteImport::null_rt_import;
    }

    string rest(str.substr(pos+1));

    pos = rest.find(':');
    if (pos == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return VrfRouteImport::null_rt_import;
    }

    boost::system::error_code ec;
    string second(rest.substr(0, pos));
    Ip4Address addr = Ip4Address::from_string(second, ec);
    char *endptr;
    if (ec.value() != 0) {
        // Not an IP address.
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return VrfRouteImport::null_rt_import;
    }
    data[0] = BgpExtendedCommunityType::IPv4Address;
    data[1] = BgpExtendedCommunitySubType::VrfRouteImport;
    uint32_t l_addr = addr.to_ulong();
    put_value(&data[2], 4, l_addr);
    int offset = 6;

    string third(rest.substr(pos+1));
    uint64_t value = strtol(third.c_str(), &endptr, 10);
    if (*endptr != '\0') {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return VrfRouteImport::null_rt_import;
    }

    // Check assigned number.
    if (value > 0xFFFF) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return VrfRouteImport::null_rt_import;
    }

    put_value(&data[offset], VrfRouteImport::kSize - offset, value);
    copy(&data[0], &data[VrfRouteImport::kSize], rt_import.data_.begin());
    return rt_import;
}
