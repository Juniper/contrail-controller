/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/multicast_flags.h"

#include <algorithm>


using std::copy;

MulticastFlags::MulticastFlags() {
    data_[0] = BgpExtendedCommunityType::Evpn;
    data_[1] = BgpExtendedCommunityEvpnSubType::MulticastFlags;
    put_value(&data_[2], 6, 0);
}

MulticastFlags::MulticastFlags(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

std::string MulticastFlags::ToString() {
    return "multicast-flags:0:0";
}
