/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/security_group/security_group.h"

#include <stdio.h>



using std::copy;
using std::string;

SecurityGroup::SecurityGroup(as_t asn, uint32_t sgid) {
    data_[0] = 0x80;
    data_[1] = 0x04;
    put_value(&data_[2], 2, asn);
    put_value(&data_[4], 4, sgid);
}

SecurityGroup::SecurityGroup(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

as_t SecurityGroup::as_number() const {
    if (data_[0] == 0x80 && data_[1] == 0x04) {
        as_t as_number = get_value(&data_[2], 2);
        return as_number;
    }
    return 0;
}

uint32_t SecurityGroup::security_group_id() const {
    if (data_[0] == 0x80 && data_[1] == 0x04) {
        uint32_t num = get_value(&data_[4], 4);
        return num;
    }
    return 0;
}

bool SecurityGroup::IsGlobal() const {
    uint32_t sgid = security_group_id();
    return (sgid >= kMinGlobalId && sgid <= kMaxGlobalId);
}

string SecurityGroup::ToString() {
    char temp[50];
    snprintf(temp, sizeof(temp), "secgroup:%u:%u",
        as_number(), security_group_id());
    return string(temp);
}
