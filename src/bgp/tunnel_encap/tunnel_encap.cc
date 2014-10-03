/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include "bgp/tunnel_encap/tunnel_encap.h"

#include "base/parse_object.h"
#include <stdio.h>

using namespace std;

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
    uint8_t data[TunnelEncap::kSize];
    string encap = "unspecified";

    copy(data_.begin(), data_.end(), &data[0]);
    if (data[0] == 0x03 && data[1] == 0x0c) {
        uint16_t num = get_value(data + 6, 2);
        encap = 
            TunnelEncapType::TunnelEncapToString((TunnelEncapType::Encap) num);
    }
    return TunnelEncapType::TunnelEncapFromString(encap);
}

string TunnelEncap::ToString() {
    string str("encapsulation:");
    str += TunnelEncapType::TunnelEncapToString(tunnel_encap());
    return str;
}
