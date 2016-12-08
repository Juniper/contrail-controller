/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/etree_leaf.h"

#include <stdio.h>

#include <algorithm>
#include <string>


using std::copy;
using std::string;

ETreeLeaf::ETreeLeaf(bool leaf) {
    data_[0] = BgpExtendedCommunityType::Evpn;
    data_[1] = BgpExtendedCommunityEvpnSubType::ETreeLeaf;
    data_[2] = leaf ? 0x01 : 0x0;  // Leaf Indication
    data_[3] = 0x00;  // Reserved
    data_[4] = 0x00;  // Reserved
    put_value(&data_[5], 3, 0); // leaf label = 0
}

ETreeLeaf::ETreeLeaf(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

bool ETreeLeaf::is_leaf() const {
    uint8_t data[ETreeLeaf::kSize];
    copy(data_.begin(), data_.end(), &data[0]);
    if (data[0] == BgpExtendedCommunityType::Evpn &&
        data[1] == BgpExtendedCommunityEvpnSubType::ETreeLeaf) {
        return (data[2] & 0x1);
    }
    return false;
}

std::string ETreeLeaf::ToString() {
    char temp[50];
    snprintf(temp, sizeof(temp), "etree-leaf:%s", (is_leaf() ? "true":"false"));
    return string(temp);
}
