/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/esi_label.h"

#include "base/parse_object.h"
#include <stdio.h>

using std::copy;
using std::string;

EsiLabel::EsiLabel(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

string EsiLabel::flags() const {
    return (data_[2] & 0x01) ? string("sa") : string("aa");
}

int EsiLabel::label() const {
    if (data_[0] == 0x06 && data_[1] == 0x01) {
        int value = get_value(&data_[5], 3);
        return value >> 4;
    }
    return 0;
}

string EsiLabel::ToString() const {
    char temp[50];
    snprintf(temp, sizeof(temp), "esilabel:%s:%d", flags().c_str(), label());
    return string(temp);
}
