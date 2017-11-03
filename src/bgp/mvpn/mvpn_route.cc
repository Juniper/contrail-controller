/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/mvpn/mvpn_route.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/string_util.h"
#include "bgp/mvpn/mvpn_table.h"

using boost::system::errc::invalid_argument;
using boost::system::error_code;
using std::copy;
using std::string;
using std::vector;

MvpnPrefix::MvpnPrefix() : type_(MvpnPrefix::Unspecified), ip_prefixlen_(0),
        asn_(0) {
}

MvpnPrefix::MvpnPrefix(uint8_t type, const RouteDistinguisher &rd,
    const uint32_t asn)
    : type_(type), rd_(rd), asn_(asn) {

    assert(type == InterASPMSIADRoute);
}

MvpnPrefix::MvpnPrefix(uint8_t type, const Ip4Address &originator)
    : type_(type), originator_(originator) {

    assert(type == LeafADRoute);
}

MvpnPrefix::MvpnPrefix(uint8_t type, const RouteDistinguisher &rd,
    const Ip4Address &originator)
    : type_(type), rd_(rd), originator_(originator) {

    assert(type == IntraASPMSIADRoute);
}

MvpnPrefix::MvpnPrefix(uint8_t type, const RouteDistinguisher &rd,
    const Ip4Address &group, const Ip4Address &source)
    : type_(type), rd_(rd), group_(group), source_(source) {

    assert(type == SourceActiveADRoute);
}

MvpnPrefix::MvpnPrefix(uint8_t type, const RouteDistinguisher &rd,
    const Ip4Address &originator,
    const Ip4Address &group, const Ip4Address &source)
    : type_(type), rd_(rd), originator_(originator),
      group_(group), source_(source) {

    assert(type == SPMSIADRoute);
}

MvpnPrefix::MvpnPrefix(uint8_t type, const RouteDistinguisher &rd,
    const uint32_t asn, const Ip4Address &group, const Ip4Address &source)
    : type_(type), rd_(rd), group_(group), source_(source), asn_(asn) {

    assert((type == SharedTreeJoinRoute) || (type == SourceTreeJoinRoute));
}

Ip4Address MvpnPrefix::GetType3OriginatorFromType4Route() const {
        size_t originator_offset = rt_key_.size() - Address::kMaxV4Bytes;
        return Ip4Address(get_value
                (&rt_key_[originator_offset], Address::kMaxV4Bytes));
}

int MvpnPrefix::FromProtoPrefix(const BgpProtoPrefix &proto_prefix,
    MvpnPrefix *prefix) {
    size_t rd_size = RouteDistinguisher::kSize;
    size_t nlri_size = proto_prefix.prefix.size();

    prefix->type_ = proto_prefix.type;
    switch (prefix->type_) {
    case IntraASPMSIADRoute: {
        size_t expected_nlri_size = rd_size + Address::kMaxV4Bytes;
        if (nlri_size != expected_nlri_size)
            return -1;
        size_t rd_offset = 0;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
        size_t originator_offset = rd_offset + rd_size;
        prefix->originator_ = Ip4Address(get_value(
            &proto_prefix.prefix[originator_offset], Address::kMaxV4Bytes));
        break;
    }
    case InterASPMSIADRoute: {
        uint32_t asn;
        size_t asn_size = sizeof(asn);
        size_t expected_nlri_size = rd_size + asn_size;
        if (nlri_size != expected_nlri_size)
            return -1;
        size_t rd_offset = 0;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
        size_t asn_offset = rd_offset + rd_size;
        asn = get_value(&proto_prefix.prefix[asn_offset], sizeof(uint32_t));
        prefix->asn_ = asn;
        break;
    }
    case SPMSIADRoute: {
        size_t expected_nlri_size = rd_size + 2 *
            (Address::kMaxV4Bytes + 1) + Address::kMaxV4Bytes;
        if (nlri_size != expected_nlri_size)
            return -1;
        size_t rd_offset = 0;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
        size_t source_offset = rd_offset + rd_size + 1;
        if (proto_prefix.prefix[source_offset - 1] != Address::kMaxV4PrefixLen)
            return -1;
        prefix->source_ = Ip4Address(get_value(
            &proto_prefix.prefix[source_offset], Address::kMaxV4Bytes));

        size_t group_offset = source_offset + Address::kMaxV4Bytes + 1;
        if (proto_prefix.prefix[group_offset - 1] != Address::kMaxV4PrefixLen)
            return -1;
        prefix->group_ = Ip4Address(get_value(
            &proto_prefix.prefix[group_offset], Address::kMaxV4Bytes));
        size_t originator_offset = group_offset + Address::kMaxV4Bytes;
        prefix->originator_ = Ip4Address(get_value(
            &proto_prefix.prefix[originator_offset], Address::kMaxV4Bytes));
        break;
    }
    case LeafADRoute: {
        if (proto_prefix.prefix[0] != integerToString(SPMSIADRoute)[0])
            return -1;
        size_t expected_nlri_size = 1 + rd_size + 4 * Address::kMaxV4Bytes;
        if (nlri_size != expected_nlri_size)
            return -1;
        size_t rd_offset = 1;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
        size_t source_offset = 1 + rd_size;
        prefix->source_ = Ip4Address(get_value(
            &proto_prefix.prefix[source_offset], Address::kMaxV4Bytes));

        size_t group_offset = source_offset + Address::kMaxV4Bytes;
        prefix->group_ = Ip4Address(get_value(
            &proto_prefix.prefix[group_offset], Address::kMaxV4Bytes));
        size_t originator_offset = nlri_size - Address::kMaxV4Bytes;
        prefix->rt_key_.resize(originator_offset);
        copy(proto_prefix.prefix.begin(), proto_prefix.prefix.begin() +
                originator_offset, prefix->rt_key_.begin());
        prefix->originator_ = Ip4Address(get_value(
            &proto_prefix.prefix[originator_offset], Address::kMaxV4Bytes));
        break;
    }
    case SourceActiveADRoute: {
        size_t expected_nlri_size = rd_size + 2 * (Address::kMaxV4Bytes + 1);
        if (nlri_size != expected_nlri_size)
            return -1;
        size_t rd_offset = 0;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
        size_t source_offset = rd_offset + rd_size + 1;
        if (proto_prefix.prefix[source_offset - 1] != Address::kMaxV4PrefixLen)
            return -1;
        prefix->source_ = Ip4Address(get_value(
            &proto_prefix.prefix[source_offset], Address::kMaxV4Bytes));

        size_t group_offset = source_offset + Address::kMaxV4Bytes + 1;
        if (proto_prefix.prefix[group_offset - 1] != Address::kMaxV4PrefixLen)
            return -1;
        prefix->group_ = Ip4Address(get_value(
            &proto_prefix.prefix[group_offset], Address::kMaxV4Bytes));
        break;
    }
    case SourceTreeJoinRoute:
    case SharedTreeJoinRoute: {
        uint32_t asn;
        size_t asn_size = sizeof(asn);
        size_t expected_nlri_size = rd_size + 2 *
            (Address::kMaxV4Bytes + 1) + asn_size;
        if (nlri_size != expected_nlri_size)
            return -1;
        size_t rd_offset = 0;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
        size_t asn_offset = rd_offset + rd_size;
        asn = get_value(&proto_prefix.prefix[asn_offset], asn_size);
        prefix->asn_ = asn;
        size_t source_offset = asn_offset + asn_size + 1;
        if (proto_prefix.prefix[source_offset - 1] != Address::kMaxV4PrefixLen)
            return -1;
        prefix->source_ = Ip4Address(get_value(
            &proto_prefix.prefix[source_offset], Address::kMaxV4Bytes));

        size_t group_offset = source_offset + Address::kMaxV4Bytes + 1;
        if (proto_prefix.prefix[group_offset - 1] != Address::kMaxV4PrefixLen)
            return -1;
        prefix->group_ = Ip4Address(get_value(
            &proto_prefix.prefix[group_offset], Address::kMaxV4Bytes));
        break;
    }
    default: {
        return -1;
    }
    }

    return 0;
}

void MvpnPrefix::set_originator(const Ip4Address &originator) {
    originator_ = originator;
}

int MvpnPrefix::FromProtoPrefix(BgpServer *server,
                                  const BgpProtoPrefix &proto_prefix,
                                  const BgpAttr *attr, MvpnPrefix *prefix,
                                  BgpAttrPtr *new_attr, uint32_t *label,
                                  uint32_t *l3_label) {
    return FromProtoPrefix(proto_prefix, prefix);
}

void MvpnPrefix::BuildProtoPrefix(BgpProtoPrefix *proto_prefix) const {
    size_t rd_size = RouteDistinguisher::kSize;

    proto_prefix->type = type_;
    proto_prefix->prefix.clear();


    switch (type_) {
    case IntraASPMSIADRoute: {
        size_t nlri_size = rd_size + Address::kMaxV4Bytes;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t rd_offset = 0;
        copy(rd_.GetData(), rd_.GetData() + rd_size,
            proto_prefix->prefix.begin() + rd_offset);
        size_t originator_offset = rd_offset + rd_size;
        const Ip4Address::bytes_type &source_bytes = originator_.to_bytes();
        copy(source_bytes.begin(), source_bytes.begin() + Address::kMaxV4Bytes,
            proto_prefix->prefix.begin() + originator_offset);
        break;
    }
    case InterASPMSIADRoute: {
        uint32_t asn = asn_;
        size_t asn_size = sizeof(asn);
        size_t nlri_size = rd_size + asn_size;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t rd_offset = 0;
        copy(rd_.GetData(), rd_.GetData() + rd_size,
            proto_prefix->prefix.begin() + rd_offset);
        size_t asn_offset = rd_offset + rd_size;
        put_value(&proto_prefix->prefix[asn_offset], asn_size, asn);
        break;
    }
    case SPMSIADRoute: {
        size_t nlri_size = rd_size + 2 * (1 + Address::kMaxV4Bytes) +
            Address::kMaxV4Bytes;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t rd_offset = 0;
        copy(rd_.GetData(), rd_.GetData() + rd_size,
            proto_prefix->prefix.begin() + rd_offset);
        size_t source_offset = rd_offset + rd_size + 1;
        proto_prefix->prefix[source_offset - 1] = Address::kMaxV4PrefixLen;
        const Ip4Address::bytes_type &source_bytes = source_.to_bytes();
        copy(source_bytes.begin(), source_bytes.begin() + Address::kMaxV4Bytes,
            proto_prefix->prefix.begin() + source_offset);

        size_t group_offset = source_offset + Address::kMaxV4Bytes + 1;
        proto_prefix->prefix[group_offset - 1] = Address::kMaxV4PrefixLen;
        const Ip4Address::bytes_type &group_bytes = group_.to_bytes();
        copy(group_bytes.begin(), group_bytes.begin() + Address::kMaxV4Bytes,
            proto_prefix->prefix.begin() + group_offset);

        size_t originator_offset = group_offset + Address::kMaxV4Bytes;
        const Ip4Address::bytes_type &originator_bytes = originator_.to_bytes();
        copy(originator_bytes.begin(), originator_bytes.begin() +
                Address::kMaxV4Bytes, proto_prefix->prefix.begin() +
                originator_offset);
        break;
    }
    case LeafADRoute: {
        size_t keySize = rt_key_.size();
        size_t nlri_size = keySize + Address::kMaxV4Bytes;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        copy(rt_key_.begin(), rt_key_.begin() + keySize,
                proto_prefix->prefix.begin());

        const Ip4Address::bytes_type &originator_bytes = originator_.to_bytes();
        copy(originator_bytes.begin(), originator_bytes.begin() +
                Address::kMaxV4Bytes, proto_prefix->prefix.begin() +
                keySize);
        break;
    }
    case SourceActiveADRoute: {
        size_t nlri_size = rd_size + 2 * (1 + Address::kMaxV4Bytes);
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t rd_offset = 0;
        copy(rd_.GetData(), rd_.GetData() + rd_size,
            proto_prefix->prefix.begin() + rd_offset);
        size_t source_offset = rd_offset + rd_size + 1;
        proto_prefix->prefix[source_offset - 1] = Address::kMaxV4PrefixLen;
        const Ip4Address::bytes_type &source_bytes = source_.to_bytes();
        copy(source_bytes.begin(), source_bytes.begin() + Address::kMaxV4Bytes,
            proto_prefix->prefix.begin() + source_offset);

        size_t group_offset = source_offset + Address::kMaxV4Bytes + 1;
        proto_prefix->prefix[group_offset - 1] = Address::kMaxV4PrefixLen;
        const Ip4Address::bytes_type &group_bytes = group_.to_bytes();
        copy(group_bytes.begin(), group_bytes.begin() + Address::kMaxV4Bytes,
            proto_prefix->prefix.begin() + group_offset);
        break;
    }
    case SourceTreeJoinRoute:
    case SharedTreeJoinRoute: {
        uint32_t asn = asn_;
        size_t asn_size = sizeof(asn);
        size_t nlri_size = rd_size + asn_size + 2 * (1 + Address::kMaxV4Bytes);
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t rd_offset = 0;
        copy(rd_.GetData(), rd_.GetData() + rd_size,
            proto_prefix->prefix.begin() + rd_offset);
        size_t asn_offset = rd_offset + rd_size;
        put_value(&proto_prefix->prefix[asn_offset], asn_size, asn);
        size_t source_offset = asn_offset + asn_size + 1;
        proto_prefix->prefix[source_offset - 1] = Address::kMaxV4PrefixLen;
        const Ip4Address::bytes_type &source_bytes = source_.to_bytes();
        copy(source_bytes.begin(), source_bytes.begin() + Address::kMaxV4Bytes,
            proto_prefix->prefix.begin() + source_offset);

        size_t group_offset = source_offset + Address::kMaxV4Bytes + 1;
        proto_prefix->prefix[group_offset - 1] = Address::kMaxV4PrefixLen;
        const Ip4Address::bytes_type &group_bytes = group_.to_bytes();
        copy(group_bytes.begin(), group_bytes.begin() + Address::kMaxV4Bytes,
            proto_prefix->prefix.begin() + group_offset);
        break;
    }
    default: {
        assert(false);
        break;
    }
    }
}

bool MvpnPrefix::GetTypeFromString(MvpnPrefix *prefix, const string &str,
    error_code *errorp, size_t *pos1) {
    *pos1 = str.find('-');
    if (*pos1 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(invalid_argument);
        }
        return false;
    }
    string temp_str = str.substr(0, *pos1);
    stringToInteger(temp_str, prefix->type_);
    if (!IsValid(prefix->type_)) {
        if (errorp != NULL) {
            *errorp = make_error_code(invalid_argument);
        }
        return false;
    }
    return true;
}

bool MvpnPrefix::GetRDFromString(MvpnPrefix *prefix, const string &str,
        size_t pos1, size_t *pos2, error_code *errorp) {
    *pos2 = str.find(',', pos1 + 1);
    if (*pos2 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(invalid_argument);
        }
        return false;
    }
    string temp_str = str.substr(pos1 + 1, *pos2 - pos1 - 1);
    error_code rd_err;
    prefix->rd_ = RouteDistinguisher::FromString(temp_str, &rd_err);
    if (rd_err != 0) {
        if (errorp != NULL) {
            *errorp = rd_err;
        }
        return false;
    }
    return true;
}

bool MvpnPrefix::GetOriginatorFromString(MvpnPrefix *prefix,
        const string &str, size_t pos1, error_code *errorp) {
    string temp_str = str.substr(pos1 + 1, string::npos);
    error_code originator_err;
    prefix->originator_ = Ip4Address::from_string(temp_str, originator_err);
    if (originator_err != 0) {
        if (errorp != NULL) {
            *errorp = originator_err;
        }
        return false;
    }
    return true;
}

bool MvpnPrefix::GetSourceFromString(MvpnPrefix *prefix, const string &str,
        size_t pos1, size_t *pos2, error_code *errorp) {
    *pos2 = str.find(',', pos1 + 1);
    if (*pos2 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(invalid_argument);
        }
        return false;
    }
    string temp_str = str.substr(pos1 + 1, *pos2 - pos1 - 1);
    error_code source_err;
    prefix->source_ = Ip4Address::from_string(temp_str, source_err);
    if (source_err != 0) {
        if (errorp != NULL) {
            *errorp = source_err;
        }
        return false;
    }
    return true;
}

bool MvpnPrefix::GetGroupFromString(MvpnPrefix *prefix, const string &str,
        size_t pos1, size_t *pos2, error_code *errorp,
        bool last) {
    *pos2 = str.find(',', pos1 + 1);
    if (!last && *pos2 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(invalid_argument);
        }
        return false;
    }
    string temp_str;
    if (last)
        temp_str = str.substr(pos1 + 1, string::npos);
    else
        temp_str = str.substr(pos1 + 1, *pos2 - pos1 - 1);
    error_code group_err;
    prefix->group_ = Ip4Address::from_string(temp_str, group_err);
    if (group_err != 0) {
        if (errorp != NULL) {
            *errorp = group_err;
        }
        return false;
    }
    return true;
}

bool MvpnPrefix::GetAsnFromString(MvpnPrefix *prefix, const string &str,
        size_t pos1, size_t *pos2, error_code *errorp) {
    *pos2 = str.find(',', pos1 + 1);
    if (*pos2 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(invalid_argument);
        }
        return false;
    }
    string temp_str = str.substr(pos1 + 1, *pos2 - pos1 - 1);
    if (!stringToInteger(temp_str, prefix->asn_)) {
        return false;
    }
    return true;
}

MvpnPrefix MvpnPrefix::FromString(const string &str,
    error_code *errorp) {
    MvpnPrefix prefix, null_prefix;
    string temp_str;

    // Look for Type.
    size_t pos1;
    if (!GetTypeFromString(&prefix, str, errorp, &pos1))
        return null_prefix;

    switch (prefix.type_) {
    case IntraASPMSIADRoute: {
        // Look for RD.
        size_t pos2;
        if (!GetRDFromString(&prefix, str, pos1, &pos2, errorp))
            return null_prefix;
        // rest is originator
        if (!GetOriginatorFromString(&prefix, str, pos2, errorp))
            return null_prefix;
        break;
    }
    case InterASPMSIADRoute: {
        // Look for RD.
        size_t pos2;
        if (!GetRDFromString(&prefix, str, pos1, &pos2, errorp))
            return null_prefix;
        // rest is asn
        temp_str = str.substr(pos2 + 1, string::npos);
        if (!stringToInteger(temp_str, prefix.asn_)) {
            return null_prefix;
        }
        break;
    }
    case SPMSIADRoute: {
        // Look for RD.
        size_t pos2;
        if (!GetRDFromString(&prefix, str, pos1, &pos2, errorp))
            return null_prefix;
        // Look for source.
        size_t pos3;
        if (!GetSourceFromString(&prefix, str, pos2, &pos3, errorp))
            return null_prefix;

        // Look for group.
        size_t pos4;
        if (!GetGroupFromString(&prefix, str, pos3, &pos4, errorp))
            return null_prefix;

        // rest is originator
        if (!GetOriginatorFromString(&prefix, str, pos4, errorp))
            return null_prefix;
        break;
    }
    case LeafADRoute: {
        // First get the originator from the end
        size_t pos_last = str.find_last_of(',');
        if (pos_last == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(invalid_argument);
            }
            return null_prefix;
        }
        if (!GetOriginatorFromString(&prefix, str, pos_last, errorp))
            return null_prefix;
        temp_str = str.substr(pos1 + 1, string::npos);
        // Look for type.
        size_t pos2;
        if (!GetTypeFromString(&prefix, temp_str, errorp, &pos2))
            return null_prefix;
        uint8_t src_rt_type = prefix.type_;
        prefix.type_ = LeafADRoute;
        size_t rd_size = RouteDistinguisher::kSize;
        size_t total_key_size = 0;
        if (src_rt_type == MvpnPrefix::InterASPMSIADRoute) {
            total_key_size = 1 + rd_size + sizeof(prefix.asn_);
        } else if (src_rt_type == MvpnPrefix::SPMSIADRoute) {
            total_key_size = 1 + rd_size + Address::kMaxV4Bytes * 3;
        }
        size_t key_size = 0;
        prefix.rt_key_.resize(total_key_size);
        prefix.rt_key_[key_size++] = temp_str[0];
        // Look for RD.
        size_t pos3;
        pos2 = pos1 + 1 + pos2;
        if (!GetRDFromString(&prefix, str, pos2, &pos3, errorp))
            return null_prefix;
        copy(prefix.rd_.GetData(), prefix.rd_.GetData() + rd_size,
                prefix.rt_key_.begin() + key_size);
        key_size += rd_size;
        // check if source ip or asn
        size_t pos4;
        if (src_rt_type == MvpnPrefix::InterASPMSIADRoute) {
            // check for asn
            if (!GetAsnFromString(&prefix, str, pos3, &pos4, errorp))
                return null_prefix;
            size_t asn_size = sizeof(prefix.asn_);
            put_value(&prefix.rt_key_[key_size], asn_size, prefix.asn_);
            break;
        }
        if (!GetSourceFromString(&prefix, str, pos3, &pos4, errorp))
            return null_prefix;
        const Ip4Address::bytes_type &source_bytes = prefix.source_.to_bytes();
        copy(source_bytes.begin(), source_bytes.begin() +
                    Address::kMaxV4Bytes, prefix.rt_key_.begin() + key_size);
        key_size += Address::kMaxV4Bytes;

        // Look for group.
        size_t pos5;
        if (!GetGroupFromString(&prefix, str, pos4, &pos5, errorp))
            return null_prefix;
        const Ip4Address::bytes_type &group_bytes = prefix.group_.to_bytes();
        copy(group_bytes.begin(), group_bytes.begin() +
                    Address::kMaxV4Bytes, prefix.rt_key_.begin() + key_size);
        key_size += Address::kMaxV4Bytes;

        // Look for originator of rt_key and ignore it.
        size_t pos6 = str.find(',', pos5 + 1);
        if (pos6 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos5 + 1, pos6 - pos5 - 1);
        error_code originator_err;
        Ip4Address ip = Ip4Address::from_string(temp_str, originator_err);
        if (originator_err != 0) {
            if (errorp != NULL) {
                *errorp = originator_err;
            }
            return null_prefix;
        }
        const Ip4Address::bytes_type &originator_bytes = ip.to_bytes();
        copy(originator_bytes.begin(), originator_bytes.begin() +
                    Address::kMaxV4Bytes, prefix.rt_key_.begin() + key_size);
        break;
    }
    case SourceActiveADRoute: {
        // Look for RD.
        size_t pos2;
        if (!GetRDFromString(&prefix, str, pos1, &pos2, errorp))
            return null_prefix;
        // Look for source.
        size_t pos3;
        if (!GetSourceFromString(&prefix, str, pos2, &pos3, errorp))
            return null_prefix;

        // rest is group.
        size_t pos4;
        if (!GetGroupFromString(&prefix, str, pos3, &pos4, errorp, true))
            return null_prefix;
        break;
    }
    case SharedTreeJoinRoute:
    case SourceTreeJoinRoute: {
        // Look for RD.
        size_t pos2;
        if (!GetRDFromString(&prefix, str, pos1, &pos2, errorp))
            return null_prefix;
        // Look for asn
        size_t pos3;
        if (!GetAsnFromString(&prefix, str, pos2, &pos3, errorp))
            return null_prefix;
        // Look for source.
        size_t pos4;
        if (!GetSourceFromString(&prefix, str, pos3, &pos4, errorp))
            return null_prefix;

        // rest is group.
        size_t pos5;
        if (!GetGroupFromString(&prefix, str, pos4, &pos5, errorp, true))
            return null_prefix;
        break;
    }
    }

    return prefix;
}

string MvpnPrefix::ToString() const {
    string repr = integerToString(type_);
    switch (type_) {
        case IntraASPMSIADRoute:
            repr += "-" + rd_.ToString();
            repr += "," + originator_.to_string();
            break;
        case InterASPMSIADRoute:
            repr += "-" + rd_.ToString();
            repr += "," + integerToString(asn_);
            break;
        case SPMSIADRoute:
            repr += "-" + rd_.ToString();
            repr += "," + source_.to_string();
            repr += "," + group_.to_string();
            repr += "," + originator_.to_string();
            break;
        case LeafADRoute: {
            size_t key_size = 1;
            RouteDistinguisher rd(&rt_key_[key_size]);
            char rt_type = rt_key_[0];
            if (rt_type == integerToString(MvpnPrefix::SPMSIADRoute)[0]) {
                repr += "-3";
                repr += "-" + rd.ToString();
                key_size += RouteDistinguisher::kSize;
                Ip4Address ip = Ip4Address(get_value
                    (&rt_key_[key_size], Address::kMaxV4Bytes));
                repr += "," + ip.to_string();
                key_size += Address::kMaxV4Bytes;
                ip = Ip4Address(get_value
                    (&rt_key_[key_size], Address::kMaxV4Bytes));
                key_size += Address::kMaxV4Bytes;
                repr += "," + ip.to_string();
                ip = Ip4Address(get_value
                    (&rt_key_[key_size], Address::kMaxV4Bytes));
                repr += "," + ip.to_string();
            } else if (rt_type == integerToString(
                        MvpnPrefix::InterASPMSIADRoute)[0]) {
                repr += "-2";
                repr += "-" + rd.ToString();
                key_size += RouteDistinguisher::kSize;
                uint32_t asn;
                asn = get_value(&rt_key_[key_size], sizeof(asn));
                repr += "," + integerToString(asn);
            }
            repr += "," + originator_.to_string();
            break;
        }
        case SourceActiveADRoute:
            repr += "-" + rd_.ToString();
            repr += "," + source_.to_string();
            repr += "," + group_.to_string();
            break;
        case SharedTreeJoinRoute:
        case SourceTreeJoinRoute:
            repr += "-" + rd_.ToString();
            repr += "," + integerToString(asn_);
            repr += "," + source_.to_string();
            repr += "," + group_.to_string();
            break;
    }
    return repr;
}

int MvpnPrefix::CompareTo(const MvpnPrefix &rhs) const {
    KEY_COMPARE(type_, rhs.type_);

    switch (type_) {
    case IntraASPMSIADRoute:
        KEY_COMPARE(rd_, rhs.rd_);
        KEY_COMPARE(originator_, rhs.originator_);
        break;
    case InterASPMSIADRoute:
        KEY_COMPARE(rd_, rhs.rd_);
        KEY_COMPARE(asn_, rhs.asn_);
        break;
    case SPMSIADRoute:
        KEY_COMPARE(rd_, rhs.rd_);
        KEY_COMPARE(source_, rhs.source_);
        KEY_COMPARE(group_, rhs.group_);
        KEY_COMPARE(originator_, rhs.originator_);
        break;
    case LeafADRoute:
        KEY_COMPARE(rt_key_, rhs.rt_key_);
        KEY_COMPARE(originator_, rhs.originator_);
        break;
    case SourceActiveADRoute:
        KEY_COMPARE(rd_, rhs.rd_);
        KEY_COMPARE(source_, rhs.source_);
        KEY_COMPARE(group_, rhs.group_);
        break;
    case SourceTreeJoinRoute:
    case SharedTreeJoinRoute:
        KEY_COMPARE(rd_, rhs.rd_);
        KEY_COMPARE(asn_, rhs.asn_);
        KEY_COMPARE(source_, rhs.source_);
        KEY_COMPARE(group_, rhs.group_);
        break;
    default:
        break;
    }
    return 0;
}

// Populate LeafADRoute(Type4) rt_key_ from SPMSIADRoute(Type3)
void MvpnPrefix::SetRtKeyFromSPMSIADRoute(const MvpnPrefix &prefix) {
    assert(prefix.type() == SPMSIADRoute);

    size_t rd_size = RouteDistinguisher::kSize;
    size_t key_size = 0;
    size_t total_key_size = 1 + rd_size + Address::kMaxV4Bytes * 3;
    rt_key_.resize(total_key_size);
    rt_key_[key_size++] = integerToString(SPMSIADRoute)[0];
    copy(prefix.route_distinguisher().GetData(),
            prefix.route_distinguisher().GetData() + rd_size,
            rt_key_.begin() + key_size);
    RouteDistinguisher rd(prefix.route_distinguisher().GetData());
    rd_ = rd;
    key_size += rd_size;
    const Ip4Address::bytes_type &source_bytes =
            prefix.source().to_bytes();
    copy(source_bytes.begin(), source_bytes.begin() +
                Address::kMaxV4Bytes, rt_key_.begin() + key_size);
    Ip4Address source(prefix.source().to_ulong());
    source_ = source;
    key_size += Address::kMaxV4Bytes;
    const Ip4Address::bytes_type &group_bytes = prefix.group().to_bytes();
    copy(group_bytes.begin(), group_bytes.begin() +
                Address::kMaxV4Bytes, rt_key_.begin() + key_size);
    Ip4Address group(prefix.group().to_ulong());
    group_ = group;
    key_size += Address::kMaxV4Bytes;
    const Ip4Address::bytes_type &originator_bytes =
            prefix.originator().to_bytes();
    copy(originator_bytes.begin(), originator_bytes.begin() +
                Address::kMaxV4Bytes, rt_key_.begin() + key_size);
    type_ = LeafADRoute;
}

void MvpnPrefix::SetRtKeyFromLeafADRoute(const MvpnPrefix &prefix) {
    assert(prefix.type() == LeafADRoute);
    size_t rd_size = RouteDistinguisher::kSize;
    size_t key_size = 0;
    size_t total_key_size = 1 + rd_size + Address::kMaxV4Bytes * 3;
    rt_key_.resize(total_key_size);
    rt_key_[key_size++] = integerToString(SPMSIADRoute)[0];
    copy(prefix.route_distinguisher().GetData(),
            prefix.route_distinguisher().GetData() + rd_size,
            rt_key_.begin() + key_size);
    RouteDistinguisher rd(prefix.route_distinguisher().GetData());
    rd_ = rd;
    key_size += rd_size;
    const Ip4Address::bytes_type &source_bytes =
            prefix.source().to_bytes();
    copy(source_bytes.begin(), source_bytes.begin() +
                Address::kMaxV4Bytes, rt_key_.begin() + key_size);
    Ip4Address source(prefix.source().to_ulong());
    source_ = source;
    key_size += Address::kMaxV4Bytes;
    const Ip4Address::bytes_type &group_bytes = prefix.group().to_bytes();
    copy(group_bytes.begin(), group_bytes.begin() +
                Address::kMaxV4Bytes, rt_key_.begin() + key_size);
    Ip4Address group(prefix.group().to_ulong());
    group_ = group;
    key_size += Address::kMaxV4Bytes;
    originator_ = prefix.GetType3OriginatorFromType4Route();
    const Ip4Address::bytes_type &originator_bytes =
            originator_.to_bytes();
    copy(originator_bytes.begin(), originator_bytes.begin() +
                Address::kMaxV4Bytes, rt_key_.begin() + key_size);
    type_ = SPMSIADRoute;
}

string MvpnPrefix::ToXmppIdString() const {
    string repr = rd_.ToString();
    repr += ":" + group_.to_string();
    repr += "," + source_.to_string();
    return repr;
}

bool MvpnPrefix::IsValid(uint8_t type) {
    return (type > Unspecified) && (type <= SourceTreeJoinRoute);
}

bool MvpnPrefix::operator==(const MvpnPrefix &rhs) const {
    return (
        type_ == rhs.type_ &&
        rd_ == rhs.rd_ &&
        originator_ == rhs.originator_ &&
        group_ == rhs.group_ &&
        source_ == rhs.source_);
}

MvpnRoute::MvpnRoute(const MvpnPrefix &prefix) : prefix_(prefix) {
}

int MvpnRoute::CompareTo(const Route &rhs) const {
    const MvpnRoute &other = static_cast<const MvpnRoute &>(rhs);
    return prefix_.CompareTo(other.prefix_);
    KEY_COMPARE(prefix_.type(), other.prefix_.type());
    KEY_COMPARE(
        prefix_.route_distinguisher(), other.prefix_.route_distinguisher());
    KEY_COMPARE(prefix_.originator(), other.prefix_.originator());
    KEY_COMPARE(prefix_.source(), other.prefix_.source());
    KEY_COMPARE(prefix_.group(), other.prefix_.group());
    KEY_COMPARE(prefix_.asn(), other.prefix_.asn());
    return 0;
}

string MvpnRoute::ToString() const {
    return prefix_.ToString();
}

string MvpnRoute::ToXmppIdString() const {
    if (xmpp_id_str_.empty())
        xmpp_id_str_ = prefix_.ToXmppIdString();
    return xmpp_id_str_;
}

bool MvpnRoute::IsValid() const {
    if (!BgpRoute::IsValid())
        return false;

    return true;
}

void MvpnRoute::SetKey(const DBRequestKey *reqkey) {
    const MvpnTable::RequestKey *key =
        static_cast<const MvpnTable::RequestKey *>(reqkey);
    prefix_ = key->prefix;
}

void MvpnRoute::BuildProtoPrefix(BgpProtoPrefix *prefix,
    const BgpAttr *attr, uint32_t label, uint32_t l3_label) const {
    prefix_.BuildProtoPrefix(prefix);
}

void MvpnRoute::BuildBgpProtoNextHop(
    vector<uint8_t> &nh, IpAddress nexthop) const {
    nh.resize(4);
    const Ip4Address::bytes_type &addr_bytes = nexthop.to_v4().to_bytes();
    copy(addr_bytes.begin(), addr_bytes.end(), nh.begin());
}

DBEntryBase::KeyPtr MvpnRoute::GetDBRequestKey() const {
    MvpnTable::RequestKey *key;
    key = new MvpnTable::RequestKey(GetPrefix(), NULL);
    return KeyPtr(key);
}
