/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_attr_base.h"

#include "base/util.h"

const size_t BgpProtoPrefix::kLabelSize = 3;

BgpProtoPrefix::BgpProtoPrefix() : prefixlen(0), type(0) {
}

uint32_t BgpProtoPrefix::ReadLabel(size_t label_offset) const {
    assert((label_offset + kLabelSize) <= prefix.size());
    uint32_t label = (prefix[label_offset] << 16 |
        prefix[label_offset + 1] << 8 |
        prefix[label_offset + 2]) >> 4;
    return label;
}

void BgpProtoPrefix::WriteLabel(size_t label_offset, uint32_t label) {
    assert((label_offset + kLabelSize) <= prefix.size());
    assert(label <= 0xFFFFF);
    uint32_t tmp = (label << 4 | 0x1);
    for (size_t idx = 0; idx < kLabelSize; ++idx) {
        int offset = (kLabelSize - (idx + 1)) * 8;
        prefix[label_offset + idx] = ((tmp >> offset) & 0xff);
    }
}

int BgpAttribute::CompareTo(const BgpAttribute &rhs) const {
    KEY_COMPARE(code, rhs.code);
    KEY_COMPARE(subcode, rhs.subcode);
    KEY_COMPARE(flags, rhs.flags);
    return 0;
}

std::string BgpAttribute::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "<code: %d, flags: %02x>", code, flags);
    return std::string(repr);
}
