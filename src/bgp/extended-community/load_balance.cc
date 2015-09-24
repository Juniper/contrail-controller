/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
#include <arpa/inet.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "base/parse_object.h"
#include "bgp/extended-community/load_balance.h"

using namespace std;

LoadBalance::LoadBalanceAttribute::LoadBalanceAttribute() {
    value1 = 0; // reset all fields
    value2 = 0; // reset all fields
    type = BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE;
    sub_type = BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE;

    l2_source_address = false;
    l2_destination_address = false;
    l3_source_address = true;
    l3_destination_address = true;
    l4_protocol = true;
    l4_source_port = true;
    l4_destination_port = true;
    reserved1 = false;

    reserved2 = 0;

    source_bias = false;

    reserved3 = 0;
    reserved4 = 0;
    reserved5 = 0;
}

LoadBalance::LoadBalanceAttribute::LoadBalanceAttribute(
        uint32_t value1, uint32_t value2) : value1(value1), value2(value2) {
}

void LoadBalance::LoadBalanceAttribute::encode(
        autogen::LoadBalanceType &item) const {

    item.load_balance_fields.load_balance_field_list.clear();
    if (l2_source_address)
        item.load_balance_fields.load_balance_field_list.push_back(
                string("l2-source-address"));
    if (l2_destination_address)
        item.load_balance_fields.load_balance_field_list.push_back(
                string("l2-destination-address"));
    if (l3_source_address)
        item.load_balance_fields.load_balance_field_list.push_back(
                string("l3-source-address"));
    if (l3_destination_address)
        item.load_balance_fields.load_balance_field_list.push_back(
                string("l3-destination-address"));
    if (l4_protocol)
        item.load_balance_fields.load_balance_field_list.push_back(
                string("l4-protocol"));
    if (l4_source_port)
        item.load_balance_fields.load_balance_field_list.push_back(
                string("l4-source-port"));
    if (l4_destination_port)
        item.load_balance_fields.load_balance_field_list.push_back(
                string("l4-destination-port"));
    item.load_balance_decision.source_bias = source_bias;
}

bool LoadBalance::LoadBalanceAttribute::operator==(
        const LoadBalance::LoadBalanceAttribute &other) const {
    return value1 == other.value1 && value2 == other.value2;
}

LoadBalance::LoadBalance() {
    LoadBalance::LoadBalanceAttribute attr;
    put_value(&data_[0], 4, attr.value1);
    put_value(&data_[4], 4, attr.value2);
}

LoadBalance::LoadBalance(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

LoadBalance::LoadBalance(const LoadBalance::LoadBalanceAttribute &attr) {
    put_value(&data_[0], 4, attr.value1);
    put_value(&data_[4], 4, attr.value2);
}

LoadBalance::LoadBalance(const autogen::LoadBalanceType &item) {
    LoadBalance::LoadBalanceAttribute attr;

    attr.l2_source_address = false;
    attr.l2_destination_address = false;
    attr.l3_source_address = false;
    attr.l3_destination_address = false;
    attr.l4_protocol = false;
    attr.l4_source_port = false;
    attr.l4_destination_port = false;

    for (autogen::LoadBalanceFieldListType::const_iterator it =
            item.load_balance_fields.begin();
            it != item.load_balance_fields.end(); ++it) {
        const string s = *it;
        if (s == "l2-source-address") {
            attr.l2_source_address = true;
            continue;
        }
        if (s == "l2-destination-address") {
            attr.l2_destination_address = true;
            continue;
        }
        if (s == "l3-source-address") {
            attr.l3_source_address = true;
            continue;
        }
        if (s == "l3-destination-address") {
            attr.l3_destination_address = true;
            continue;
        }
        if (s == "l4-protocol") {
            attr.l4_protocol = true;
            continue;
        }
        if (s == "l4-source-port") {
            attr.l4_source_port = true;
            continue;
        }
        if (s == "l4-destination-port") {
            attr.l4_destination_port = true;
            continue;
        }
    }

    attr.source_bias = item.load_balance_decision.source_bias;
    put_value(&data_[0], 4, attr.value1);
    put_value(&data_[4], 4, attr.value2);
}

void LoadBalance::fillAttribute(LoadBalance::LoadBalanceAttribute &attr) {
    attr.value1 = get_value(&data_[0], 4);
    attr.value2 = get_value(&data_[4], 4);
}

const LoadBalance::LoadBalanceAttribute LoadBalance::ToAttribute() const {
    return LoadBalance::LoadBalanceAttribute(
            get_value_unaligned(&data_[0], 4),
            get_value_unaligned(&data_[4], 4));
}

std::string LoadBalance::ToString() const {
    const LoadBalance::LoadBalanceAttribute attr = ToAttribute();
    ostringstream os;
    os << "loadbalance:";
    if (attr.l2_source_address) os << " L2SA";
    if (attr.l2_destination_address) os << " L2DA";
    if (attr.l3_source_address) os << " L3SA";
    if (attr.l3_destination_address) os << " L3DA";
    if (attr.l4_protocol) os << " L4PR";
    if (attr.l4_source_port) os << " L4SP";
    if (attr.l4_destination_port) os << " L4DP";
    if (attr.source_bias) os << ", SB";
    return os.str();
}
