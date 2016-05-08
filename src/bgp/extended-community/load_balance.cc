/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/load_balance.h"

#include <boost/foreach.hpp>

#include <algorithm>
#include <string>
#include <vector>

using std::copy;
using std::string;

// Initialize default attribute statically.
const LoadBalance::LoadBalanceAttribute
    LoadBalance::LoadBalanceAttribute::kDefaultLoadBalanceAttribute =
        LoadBalance::LoadBalanceAttribute();

LoadBalance::LoadBalanceAttribute::LoadBalanceAttribute() {
    value1 = 0;  // reset all fields
    value2 = 0;  // reset all fields
    type = BgpExtendedCommunityType::Opaque;
    sub_type = BgpExtendedCommunityOpaqueSubType::LoadBalance;

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

void LoadBalance::LoadBalanceAttribute::Encode(
        autogen::LoadBalanceType *lb_type) const {
    lb_type->load_balance_fields.load_balance_field_list.clear();
    if (l3_source_address)
        lb_type->load_balance_fields.load_balance_field_list.push_back(
                string("l3-source-address"));
    if (l3_destination_address)
        lb_type->load_balance_fields.load_balance_field_list.push_back(
                string("l3-destination-address"));
    if (l4_protocol)
        lb_type->load_balance_fields.load_balance_field_list.push_back(
                string("l4-protocol"));
    if (l4_source_port)
        lb_type->load_balance_fields.load_balance_field_list.push_back(
                string("l4-source-port"));
    if (l4_destination_port)
        lb_type->load_balance_fields.load_balance_field_list.push_back(
                string("l4-destination-port"));
    lb_type->load_balance_decision = source_bias ? "source-bias" : "field-hash";
}

bool LoadBalance::LoadBalanceAttribute::operator==(
        const LoadBalance::LoadBalanceAttribute &other) const {
    return value1 == other.value1 && value2 == other.value2;
}

bool LoadBalance::LoadBalanceAttribute::operator!=(
        const LoadBalance::LoadBalanceAttribute &other) const {
    return value1 != other.value1 || value2 != other.value2;
}

const bool LoadBalance::LoadBalanceAttribute::IsDefault() const {
    return *this == kDefaultLoadBalanceAttribute;
}

LoadBalance::LoadBalance() {
    LoadBalanceAttribute attr;
    put_value(&data_[0], 4, attr.value1);
    put_value(&data_[4], 4, attr.value2);
}

LoadBalance::LoadBalance(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

LoadBalance::LoadBalance(const BgpPath *path) {
    LoadBalance();
    if (!path)
        return;
    const BgpAttr *attr = path->GetAttr();
    if (!attr)
        return;
    const ExtCommunity *ext_community = attr->ext_community();
    if (!ext_community)
        return;
    BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                  ext_community->communities()) {
        if (ExtCommunity::is_load_balance(comm)) {
            copy(comm.begin(), comm.end(), data_.begin());
            break;
        }
    }
}

LoadBalance::LoadBalance(const LoadBalanceAttribute &attr) {
    put_value(&data_[0], 4, attr.value1);
    put_value(&data_[4], 4, attr.value2);
}

LoadBalance::LoadBalance(const autogen::LoadBalanceType &lb_type) {
    LoadBalanceAttribute attr;

    attr.source_bias = lb_type.load_balance_decision == "source-bias";

    // autogen::LoadBalanceType fields list empty should imply as standard
    // 5-tuple fields set. xml schema does not let one specifify default values
    // for a complex type such as a list in this case.
    bool load_balance_default =
        !attr.source_bias && (lb_type.load_balance_fields.begin() ==
                              lb_type.load_balance_fields.end());
    attr.l3_source_address = load_balance_default || false;
    attr.l3_destination_address = load_balance_default || false;
    attr.l4_protocol = load_balance_default || false;
    attr.l4_source_port = load_balance_default || false;
    attr.l4_destination_port = load_balance_default || false;

    for (autogen::LoadBalanceFieldListType::const_iterator it =
            lb_type.load_balance_fields.begin();
            it != lb_type.load_balance_fields.end(); ++it) {
        const string s = *it;
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

    put_value(&data_[0], 4, attr.value1);
    put_value(&data_[4], 4, attr.value2);
}

void LoadBalance::FillAttribute(LoadBalanceAttribute *attr) {
    attr->value1 = get_value(&data_[0], 4);
    attr->value2 = get_value(&data_[4], 4);
}

const LoadBalance::LoadBalanceAttribute LoadBalance::ToAttribute() const {
    return LoadBalanceAttribute(
            get_value_unaligned(&data_[0], 4),
            get_value_unaligned(&data_[4], 4));
}

void LoadBalance::SetL3SourceAddress(bool flag) {
    LoadBalanceAttribute attr = ToAttribute();
    attr.l3_source_address = flag;

    put_value(&data_[0], 4, attr.value1);
    put_value(&data_[4], 4, attr.value2);
}

void LoadBalance::SetL3DestinationAddress(bool flag) {
    LoadBalanceAttribute attr = ToAttribute();
    attr.l3_destination_address = flag;

    put_value(&data_[0], 4, attr.value1);
    put_value(&data_[4], 4, attr.value2);
}

void LoadBalance::SetL4Protocol(bool flag) {
    LoadBalanceAttribute attr = ToAttribute();
    attr.l4_protocol = flag;

    put_value(&data_[0], 4, attr.value1);
    put_value(&data_[4], 4, attr.value2);
}

void LoadBalance::SetL4SourcePort(bool flag) {
    LoadBalanceAttribute attr = ToAttribute();
    attr.l4_source_port = flag;

    put_value(&data_[0], 4, attr.value1);
    put_value(&data_[4], 4, attr.value2);
}

void LoadBalance::SetL4DestinationPort(bool flag) {
    LoadBalanceAttribute attr = ToAttribute();
    attr.l4_destination_port = flag;

    put_value(&data_[0], 4, attr.value1);
    put_value(&data_[4], 4, attr.value2);
}

void LoadBalance::SetSourceBias(bool flag) {
    LoadBalanceAttribute attr = ToAttribute();
    attr.source_bias = flag;

    put_value(&data_[0], 4, attr.value1);
    put_value(&data_[4], 4, attr.value2);
}

bool LoadBalance::IsPresent(const BgpPath *path) {
    if (!path)
        return false;
    const BgpAttr *attr = path->GetAttr();
    if (!attr)
        return false;
    const ExtCommunity *ext_community = attr->ext_community();
    if (!ext_community)
        return false;
    BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                  ext_community->communities()) {
        if (ExtCommunity::is_load_balance(comm)) {
            return true;
        }
    }
    return false;
}

const bool LoadBalance::IsDefault() const {
    return ToAttribute() == LoadBalanceAttribute::kDefaultLoadBalanceAttribute;
}

bool LoadBalance::operator==(const LoadBalance &other) const {
    return ToAttribute() == other.ToAttribute();
}

bool LoadBalance::operator!=(const LoadBalance &other) const {
    return ToAttribute() != other.ToAttribute();
}

void LoadBalance::ShowAttribute(ShowLoadBalance *show_load_balance) const {
    const LoadBalanceAttribute attr = ToAttribute();
    if (attr.source_bias) {
        show_load_balance->decision_type = "source-bias";
        show_load_balance->fields.clear();
        return;
    }

    show_load_balance->decision_type = "field-hash";
    if (attr.l3_source_address)
        show_load_balance->fields.push_back("l3-source-address");
    if (attr.l3_destination_address)
        show_load_balance->fields.push_back("l3-destination-address");
    if (attr.l4_protocol)
        show_load_balance->fields.push_back("l4-protocol");
    if (attr.l4_source_port)
        show_load_balance->fields.push_back("l4-source-port");
    if (attr.l4_destination_port)
        show_load_balance->fields.push_back("l4-destination-port");
}

string LoadBalance::ToString() const {
    const LoadBalanceAttribute attr = ToAttribute();
    string str("load-balance:");
    str += attr.source_bias ? "source-bias" : "field-hash";
    return str;
}
