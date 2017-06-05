/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/tag.h"

#include <stdio.h>

#include <algorithm>
#include <string>

#include "base/parse_object.h"
#include "bgp/extended-community/types.h"

using std::copy;
using std::string;

Tag::Tag(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

Tag::Tag(as_t asn, int tag) {
    data_[0] = BgpExtendedCommunityType::Experimental;
    data_[1] = BgpExtendedCommunityExperimentalSubType::Tag;
    put_value(&data_[2], 2, asn); // ASN
    put_value(&data_[4], 4, tag); // Tag value
}

as_t Tag::as_number() const {
    if (data_[0] == BgpExtendedCommunityType::Experimental &&
        data_[1] == BgpExtendedCommunityExperimentalSubType::Tag) {
        as_t as_number = get_value(data_.data() + 2, 2);
        return as_number;
    }
    return 0;
}

int Tag::tag() const {
    if (data_[0] == BgpExtendedCommunityType::Experimental &&
        data_[1] == BgpExtendedCommunityExperimentalSubType::Tag) {
        int value = get_value(&data_[4], 4);
        return value;
    }
    return 0;
}

bool Tag::IsGlobal() const {
    return (tag() >= kMinGlobalId);
}

string Tag::ToString() const {
    char temp[50];
    snprintf(temp, sizeof(temp), "tag:%u:%u", as_number(), tag());
    return string(temp);
}
