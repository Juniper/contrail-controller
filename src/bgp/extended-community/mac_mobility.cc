/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/mac_mobility.h"

#include "base/parse_object.h"
#include <stdio.h>
using namespace std;

MacMobility::MacMobility(uint32_t seq) {
    data_[0] = 0x06; // Type 0x6
    data_[1] = 0x00; // Sub Type 0x0
    data_[2] = 0x01; // Flags
    data_[3] = 0x00; // Rsvd
    put_value(&data_[4], 4, seq);
}

MacMobility::MacMobility(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

uint32_t MacMobility::sequence_number() const {
    uint8_t data[MacMobility::kSize];
    copy(data_.begin(), data_.end(), &data[0]);
    if (data[0] == 0x06 && data[1] == 0x00) {
        uint32_t num = get_value(data + 4, 4);
        return num;
    }
    return 0;
}

std::string MacMobility::ToString() {
    char temp[50];
    snprintf(temp, sizeof(temp), "mobility:%d", sequence_number());
    return std::string(temp);
}
