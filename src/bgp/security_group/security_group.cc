/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/security_group/security_group.h"

#include "base/parse_object.h"
#include <stdio.h>
using namespace std;

SecurityGroup::SecurityGroup(int asn, uint32_t sgid) {
    data_[0] = 0x80;
    data_[1] = 0x04;
    put_value(&data_[2], 2, asn);
    put_value(&data_[4], 4, sgid);
}

SecurityGroup::SecurityGroup(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

uint32_t SecurityGroup::security_group_id() const {
    uint8_t data[SecurityGroup::kSize];
    copy(data_.begin(), data_.end(), &data[0]);
    if (data[0] == 0x80 && data[1] == 0x04) {
        uint32_t num = get_value(data + 4, 4);
        return num;
    }
    return 0;
}

std::string SecurityGroup::ToString() {
    char temp[50];
    snprintf(temp, sizeof(temp), "security group: %d", security_group_id());
    return std::string(temp);
}
