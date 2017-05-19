/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/tag.h"

#include <stdio.h>

#include <algorithm>
#include <string>

#include "base/parse_object.h"

using std::copy;
using std::string;

Tag::Tag(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

Tag::Tag(int tag) {
    data_[0] = BgpExtendedCommunityType::Experimental;
    data_[1] = BgpExtendedCommunityExperimentalSubType::Tag;
    data_[2] = 0x00;  // Reserved
    data_[3] = 0x00;  // Reserved
    put_value(&data_[4], 4, tag); // leaf label
}


int Tag::tag() const {
    if (data_[0] == BgpExtendedCommunityType::Experimental &&
        data_[1] == BgpExtendedCommunityExperimentalSubType::Tag) {
        int value = get_value(&data_[4], 4);
        return value;
    }
    return 0;
}

string Tag::ToString() const {
    char temp[50];
    snprintf(temp, sizeof(temp), "tag:%u", tag());
    return string(temp);
}
