/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/etree.h"

#include <stdio.h>

#include <algorithm>
#include <string>


using std::copy;
using std::string;

ETree::ETree(bool leaf, int label) {
    data_[0] = BgpExtendedCommunityType::Evpn;
    data_[1] = BgpExtendedCommunityEvpnSubType::ETree;
    data_[2] = leaf ? 0x01 : 0x0;  // Leaf Indication
    data_[3] = 0x00;  // Reserved
    data_[4] = 0x00;  // Reserved
    put_value(&data_[5], 3, (label<<4)); // leaf label
}

ETree::ETree(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

bool ETree::leaf() const {
    return (data_[2] & 0x1);
}

int ETree::label() const {
    uint8_t data[ETree::kSize];
    copy(data_.begin(), data_.end(), &data[0]);
    if (data[0] == BgpExtendedCommunityType::Evpn &&
        data[1] == BgpExtendedCommunityEvpnSubType::ETree) {
        uint32_t value = get_value(data + 5, 3);
        return value >> 4;
    }
    return false;
}

std::string ETree::ToString() {
    char temp[50];
    snprintf(temp, sizeof(temp), "etree:%s:%d",
             (leaf() ? "leaf":"root"), label());
    return string(temp);
}
