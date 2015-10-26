/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_attr_base.h"

#include <cstdio>

const size_t BgpProtoPrefix::kLabelSize = 3;

BgpProtoPrefix::BgpProtoPrefix() : prefixlen(0), type(0) {
}

//
// Extract the label from the BgpProtorefix.
// EVPN extensions for VXLAN use the label to convey a 24-bit VNI.
//
uint32_t BgpProtoPrefix::ReadLabel(size_t label_offset, bool is_vni) const {
    assert((label_offset + kLabelSize) <= prefix.size());
    if (is_vni)
        return get_value(&prefix[label_offset], kLabelSize);
    uint32_t label = (prefix[label_offset] << 16 |
        prefix[label_offset + 1] << 8 |
        prefix[label_offset + 2]) >> 4;
    return label;
}

//
// Write the label to the BgpProtorefix.
// EVPN extensions for VXLAN use the label to convey a 24-bit VNI.
//
void BgpProtoPrefix::WriteLabel(size_t label_offset, uint32_t label,
    bool is_vni) {
    assert((label_offset + kLabelSize) <= prefix.size());
    if (is_vni) {
        put_value(&prefix[label_offset], kLabelSize, label);
        return;
    }
    uint32_t tmp = (label << 4 | 0x1);
    for (size_t idx = 0; idx < kLabelSize; ++idx) {
        int offset = (kLabelSize - (idx + 1)) * 8;
        prefix[label_offset + idx] = ((tmp >> offset) & 0xff);
    }
}

int BgpAttribute::CompareTo(const BgpAttribute &rhs) const {
    KEY_COMPARE(code, rhs.code);
    KEY_COMPARE(subcode, rhs.subcode);
    KEY_COMPARE(flags & ~ExtendedLength, rhs.flags & ~ExtendedLength);
    return 0;
}

size_t BgpAttribute::EncodeLength() const {
    return 0;
}

uint8_t BgpAttribute::GetEncodeFlags() const {
    uint8_t value = flags;
    if (EncodeLength() >= sizeof(uint8_t) << 8) {
        value |= BgpAttribute::ExtendedLength;
    }
    return value;
}

std::string BgpAttribute::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "<code: %d, flags: %02x>", code, flags);
    return std::string(repr);
}
