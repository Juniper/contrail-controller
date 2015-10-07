/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/es_import.h"

#include <algorithm>
#include <string>

#include "net/mac_address.h"

using std::copy;
using std::string;

EsImport::EsImport(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

MacAddress EsImport::mac_address() const {
    if (data_[0] != BgpExtendedCommunityType::Evpn ||
        data_[1] != BgpExtendedCommunityEvpnSubType::EsImport)
        return MacAddress();
    uint8_t mac_bytes[6];
    copy(data_.begin() + 2, data_.end(), mac_bytes);
    return MacAddress(mac_bytes);
}

string EsImport::ToString() const {
    string temp("esimport:");
    return temp + mac_address().ToString();
}
