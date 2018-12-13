/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/multicast_flags.h"

#include <algorithm>
#include <stdio.h>


using std::copy;
#define IGMP_PROXY_FLAG 1

MulticastFlags::MulticastFlags() {
    data_[0] = BgpExtendedCommunityType::Evpn;
    data_[1] = BgpExtendedCommunityEvpnSubType::MulticastFlags;
    put_value(&data_[2], 2, IGMP_PROXY_FLAG);
    put_value(&data_[4], 4, 0);
}

MulticastFlags::MulticastFlags(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

std::string MulticastFlags::ToString() {
    uint16_t flag = get_value(&data_[2], 2);
    char temp[50];
    snprintf(temp, sizeof(temp), "evpn-mcast-flags:%u", flag);
    return std::string(temp);
}
