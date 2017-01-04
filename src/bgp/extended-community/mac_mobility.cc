/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/mac_mobility.h"

#include <stdio.h>

#include <algorithm>
#include <string>


using std::copy;
using std::string;

MacMobility::MacMobility(uint32_t seq, bool sticky) {
    data_[0] = BgpExtendedCommunityType::Evpn;
    data_[1] = BgpExtendedCommunityEvpnSubType::MacMobility;
    data_[2] = (sticky ? 0x01 : 0x0);  // sticky
    data_[3] = 0x00; // Reserved
    put_value(&data_[4], 4, seq);
}

MacMobility::MacMobility(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

bool MacMobility::sticky() const {
    return (data_[2] & 0x1);
}

uint32_t MacMobility::sequence_number() const {
    uint8_t data[MacMobility::kSize];
    copy(data_.begin(), data_.end(), &data[0]);
    if (data[0] == BgpExtendedCommunityType::Evpn &&
        data[1] == BgpExtendedCommunityEvpnSubType::MacMobility) {
        uint32_t num = get_value(data + 4, 4);
        return num;
    }
    return 0;
}

std::string MacMobility::ToString() {
    char temp[50];
    snprintf(temp, sizeof(temp), "mobility:%s:%d",
             (sticky() ? "sticky" : "non-sticky"), sequence_number());
    return string(temp);
}
