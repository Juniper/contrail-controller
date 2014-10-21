/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/tunnel_encap/tunnel_encap.h"

#include <sstream>

#include <stdio.h>
#include "base/parse_object.h"

using std::copy;
using std::string;

TunnelEncap::TunnelEncap(string encap) {
    TunnelEncapType::Encap id = TunnelEncapType::TunnelEncapFromString(encap);
    if (id == TunnelEncapType::UNSPEC) return;
    data_[0] = 0x03;
    data_[1] = 0x0C;
    // Reserved
    put_value(&data_[2], 4, 0);
    put_value(&data_[6], 2, id);
}

TunnelEncap::TunnelEncap(TunnelEncapType::Encap tunnel_encap) {
    data_[0] = 0x03;
    data_[1] = 0x0C;
    // Reserved
    put_value(&data_[2], 4, 0);
    put_value(&data_[6], 2, tunnel_encap);
}

TunnelEncap::TunnelEncap(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

TunnelEncapType::Encap TunnelEncap::tunnel_encap() const {
    if (data_[0] != 0x03 || data_[1] != 0x0c)
        return TunnelEncapType::UNSPEC;
    uint16_t value = get_value(&data_[6], 2);
    if (TunnelEncapType::TunnelEncapIsValid(value)) {
        return static_cast<TunnelEncapType::Encap>(value);
    } else {
        return TunnelEncapType::UNSPEC;
    }
}

string TunnelEncap::ToString() {
    string str("encapsulation:");
    str += TunnelEncapType::TunnelEncapToString(tunnel_encap());
    return str;
}

string TunnelEncap::ToXmppString() {
    return TunnelEncapType::TunnelEncapToXmppString(tunnel_encap());
}
