/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/router_mac.h"

#include <algorithm>
#include <string>

using std::copy;
using std::string;

RouterMac::RouterMac(const MacAddress &mac_addr) {
    data_[0] = BgpExtendedCommunityType::Evpn;
    data_[1] = BgpExtendedCommunityEvpnSubType::RouterMac;
    copy(mac_addr.GetData(), mac_addr.GetData() + 6, data_.begin() + 2);
}

RouterMac::RouterMac(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

MacAddress RouterMac::mac_address() const {
    uint8_t data[RouterMac::kSize];
    copy(data_.begin(), data_.end(), &data[0]);
    if (data[0] == BgpExtendedCommunityType::Evpn &&
        data[1] == BgpExtendedCommunityEvpnSubType::RouterMac) {
        return MacAddress(&data[2]);
    }
    return MacAddress::kZeroMac;
}

string RouterMac::ToString() {
    return string("rtrmac:") + mac_address().ToString();
}
