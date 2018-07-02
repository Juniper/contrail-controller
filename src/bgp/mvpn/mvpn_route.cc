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

const size_t MvpnPrefix::kRdSize = RouteDistinguisher::kSize;
const size_t MvpnPrefix::kAsnSize = 4;
const size_t MvpnPrefix::kIp4AddrSize = Address::kMaxV4Bytes;
const size_t MvpnPrefix::kIp4AddrBitSize = Address::kMaxV4PrefixLen;

const size_t MvpnPrefix::kPrefixBytes = 2;
const size_t MvpnPrefix::kIntraASPMSIADRouteSize = kRdSize + kIp4AddrSize;
const size_t MvpnPrefix::kInterASPMSIADRouteSize = kRdSize + kAsnSize;
const size_t MvpnPrefix::kSPMSIADRouteSize =
                          kRdSize + 2 * (1 + kIp4AddrSize) + kIp4AddrSize;
const size_t MvpnPrefix::kSourceActiveADRouteSize =
                                         kRdSize + 2 * (1 + kIp4AddrSize);
const size_t MvpnPrefix::kSourceTreeJoinRouteSize =
                              kRdSize + kAsnSize + 2 * (1 + kIp4AddrSize);

MvpnPrefix::MvpnPrefix() : type_(MvpnPrefix::Unspecified), ip_prefixlen_(0),
        asn_(0) {
}

MvpnPrefix::MvpnPrefix(uint8_t type, const RouteDistinguisher &rd,
    const uint32_t asn)
    : type_(type), rd_(rd), ip_prefixlen_(0), asn_(asn) {

    assert(type == InterASPMSIADRoute);
}

MvpnPrefix::MvpnPrefix(uint8_t type, const Ip4Address &originator)
    : type_(type), originator_(originator), ip_prefixlen_(0), asn_(0) {

    assert(type == LeafADRoute);
}

MvpnPrefix::MvpnPrefix(uint8_t type, const RouteDistinguisher &rd,
    const Ip4Address &originator)
    : type_(type), rd_(rd), originator_(originator), ip_prefixlen_(0),
      asn_(0) {

    assert(type == IntraASPMSIADRoute);
}

MvpnPrefix::MvpnPrefix(uint8_t type, const RouteDistinguisher &rd,
    const Ip4Address &group, const Ip4Address &source)
    : type_(type), rd_(rd), group_(group), source_(source),
      ip_prefixlen_(0), asn_(0) {

    assert(type == SourceActiveADRoute);
}

MvpnPrefix::MvpnPrefix(uint8_t type, const RouteDistinguisher &rd,
    const Ip4Address &originator,
    const Ip4Address &group, const Ip4Address &source)
    : type_(type), rd_(rd), originator_(originator),
      group_(group), source_(source), ip_prefixlen_(0), asn_(0) {

    assert(type == SPMSIADRoute);
}

MvpnPrefix::MvpnPrefix(uint8_t type, const RouteDistinguisher &rd,
    const uint32_t asn, const Ip4Address &group, const Ip4Address &source)
    : type_(type), rd_(rd), group_(group), source_(source), ip_prefixlen_(0),
      asn_(asn) {

    assert((type == SharedTreeJoinRoute) || (type == SourceTreeJoinRoute));
}

Ip4Address MvpnPrefix::GetType3OriginatorFromType4Route() const {
        size_t originator_offset = rt_key_.size() - kIp4AddrSize;
        return Ip4Address(get_value
                (&rt_key_[originator_offset], kIp4AddrSize));
}

int MvpnPrefix::SpmsiAdRouteFromProtoPrefix(const BgpProtoPrefix &proto_prefix,
    MvpnPrefix *prefix, size_t rd_offset) {
    prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
    size_t source_offset = rd_offset + kRdSize;
    if (proto_prefix.prefix[source_offset++] != kIp4AddrBitSize)
        return -1;
    prefix->source_ = Ip4Address(get_value(
            &proto_prefix.prefix[source_offset], kIp4AddrSize));

    size_t group_offset = source_offset + kIp4AddrSize;
    if (proto_prefix.prefix[group_offset++] != kIp4AddrBitSize)
        return -1;
    prefix->group_ = Ip4Address(get_value(
            &proto_prefix.prefix[group_offset], kIp4AddrSize));
    size_t originator_offset = group_offset + kIp4AddrSize;
    prefix->originator_ = Ip4Address(get_value(
            &proto_prefix.prefix[originator_offset], kIp4AddrSize));
    return 0;
}

int MvpnPrefix::FromProtoPrefix(const BgpProtoPrefix &proto_prefix,
    MvpnPrefix *prefix) {
    size_t nlri_size = proto_prefix.prefix.size();

    prefix->type_ = proto_prefix.type;
    switch (prefix->type_) {
    case IntraASPMSIADRoute: {
        size_t expected_nlri_size = kIntraASPMSIADRouteSize;
        if (nlri_size != expected_nlri_size)
            return -1;
        size_t rd_offset = 0;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
        size_t originator_offset = rd_offset + kRdSize;
        prefix->originator_ = Ip4Address(get_value(
            &proto_prefix.prefix[originator_offset], kIp4AddrSize));
        break;
    }
    case InterASPMSIADRoute: {
        size_t expected_nlri_size = kInterASPMSIADRouteSize;
        if (nlri_size != expected_nlri_size)
            return -1;
        size_t rd_offset = 0;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
        size_t asn_offset = rd_offset + kRdSize;
        prefix->asn_ = get_value(&proto_prefix.prefix[asn_offset], kAsnSize);
        break;
    }
    case SPMSIADRoute: {
        size_t expected_nlri_size = kSPMSIADRouteSize;
        if (nlri_size != expected_nlri_size)
            return -1;
        if (SpmsiAdRouteFromProtoPrefix(proto_prefix, prefix, 0))
            return -1;
        break;
    }
    case LeafADRoute: {
        size_t expected_nlri_size;
        if (proto_prefix.prefix[0] == SPMSIADRoute) {
            expected_nlri_size = kPrefixBytes + kSPMSIADRouteSize +
                                 kIp4AddrSize;
        } else if (proto_prefix.prefix[0] == InterASPMSIADRoute) {
            expected_nlri_size = kPrefixBytes + kInterASPMSIADRouteSize +
                                 kIp4AddrSize;
        } else {
            return -1;
        }
        if (nlri_size != expected_nlri_size)
            return -1;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[kPrefixBytes]);
        if (proto_prefix.prefix[0] == SPMSIADRoute) {
            if (SpmsiAdRouteFromProtoPrefix(proto_prefix, prefix,
                                                       kPrefixBytes)) {
                return -1;
            }
        } else if (proto_prefix.prefix[0] == InterASPMSIADRoute) {
            size_t asn_offset = kPrefixBytes + kRdSize;
            prefix->asn_ = get_value(&proto_prefix.prefix[asn_offset], kAsnSize);
        }
        size_t originator_offset = nlri_size - kIp4AddrSize;
        prefix->rt_key_.resize(originator_offset);
        copy(proto_prefix.prefix.begin(), proto_prefix.prefix.begin() +
                originator_offset, prefix->rt_key_.begin());
        prefix->originator_ = Ip4Address(get_value(
            &proto_prefix.prefix[originator_offset], kIp4AddrSize));
        break;
    }
    case SourceActiveADRoute: {
        size_t expected_nlri_size = kSourceActiveADRouteSize;
        if (nlri_size != expected_nlri_size)
            return -1;
        size_t rd_offset = 0;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
        size_t source_offset = rd_offset + kRdSize;
        if (proto_prefix.prefix[source_offset++] != kIp4AddrBitSize)
            return -1;
        prefix->source_ = Ip4Address(get_value(
            &proto_prefix.prefix[source_offset], kIp4AddrSize));

        size_t group_offset = source_offset + kIp4AddrSize;
        if (proto_prefix.prefix[group_offset++] != kIp4AddrBitSize)
            return -1;
        prefix->group_ = Ip4Address(get_value(
            &proto_prefix.prefix[group_offset], kIp4AddrSize));
        break;
    }
    case SourceTreeJoinRoute:
    case SharedTreeJoinRoute: {
        size_t expected_nlri_size = kSourceTreeJoinRouteSize;
        if (nlri_size != expected_nlri_size)
            return -1;
        size_t rd_offset = 0;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
        size_t asn_offset = rd_offset + kRdSize;
        uint32_t asn = get_value(&proto_prefix.prefix[asn_offset], kAsnSize);
        prefix->asn_ = asn;
        size_t source_offset = asn_offset + kAsnSize;
        if (proto_prefix.prefix[source_offset++] != kIp4AddrBitSize)
            return -1;
        prefix->source_ = Ip4Address(get_value(
            &proto_prefix.prefix[source_offset], kIp4AddrSize));

        size_t group_offset = source_offset + kIp4AddrSize;
        if (proto_prefix.prefix[group_offset++] != kIp4AddrBitSize)
            return -1;
        prefix->group_ = Ip4Address(get_value(
            &proto_prefix.prefix[group_offset], kIp4AddrSize));
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
                                const BgpAttr *attr,
                                const Address::Family family,
                                MvpnPrefix *prefix,
                                BgpAttrPtr *new_attr, uint32_t *label,
                                uint32_t *l3_label) {
    return FromProtoPrefix(proto_prefix, prefix);
}

void MvpnPrefix::BuildProtoPrefix(BgpProtoPrefix *proto_prefix) const {
    proto_prefix->type = type_;
    proto_prefix->prefix.clear();

    switch (type_) {
    case IntraASPMSIADRoute: {
        size_t nlri_size = kIntraASPMSIADRouteSize;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t rd_offset = 0;
        copy(rd_.GetData(), rd_.GetData() + kRdSize,
            proto_prefix->prefix.begin() + rd_offset);
        size_t originator_offset = rd_offset + kRdSize;
        const Ip4Address::bytes_type &source_bytes = originator_.to_bytes();
        copy(source_bytes.begin(), source_bytes.begin() + kIp4AddrSize,
            proto_prefix->prefix.begin() + originator_offset);
        break;
    }
    case InterASPMSIADRoute: {
        size_t nlri_size = kInterASPMSIADRouteSize;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t rd_offset = 0;
        copy(rd_.GetData(), rd_.GetData() + kRdSize,
            proto_prefix->prefix.begin() + rd_offset);
        size_t asn_offset = rd_offset + kRdSize;
        put_value(&proto_prefix->prefix[asn_offset], kAsnSize, asn_);
        break;
    }
    case SPMSIADRoute: {
        size_t nlri_size = kSPMSIADRouteSize;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t rd_offset = 0;
        copy(rd_.GetData(), rd_.GetData() + kRdSize,
            proto_prefix->prefix.begin() + rd_offset);
        size_t source_offset = rd_offset + kRdSize;
        proto_prefix->prefix[source_offset++] = kIp4AddrBitSize;
        const Ip4Address::bytes_type &source_bytes = source_.to_bytes();
        copy(source_bytes.begin(), source_bytes.begin() + kIp4AddrSize,
            proto_prefix->prefix.begin() + source_offset);

        size_t group_offset = source_offset + kIp4AddrSize;
        proto_prefix->prefix[group_offset++] = kIp4AddrBitSize;
        const Ip4Address::bytes_type &group_bytes = group_.to_bytes();
        copy(group_bytes.begin(), group_bytes.begin() + kIp4AddrSize,
            proto_prefix->prefix.begin() + group_offset);

        size_t originator_offset = group_offset + kIp4AddrSize;
        const Ip4Address::bytes_type &originator_bytes = originator_.to_bytes();
        copy(originator_bytes.begin(), originator_bytes.begin() +
                kIp4AddrSize, proto_prefix->prefix.begin() +
                originator_offset);
        break;
    }
    case LeafADRoute: {
        size_t keySize = rt_key_.size();
        size_t nlri_size = keySize + kIp4AddrSize;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        copy(rt_key_.begin(), rt_key_.begin() + keySize,
                proto_prefix->prefix.begin());

        const Ip4Address::bytes_type &originator_bytes = originator_.to_bytes();
        copy(originator_bytes.begin(), originator_bytes.begin() +
                kIp4AddrSize, proto_prefix->prefix.begin() + keySize);
        break;
    }
    case SourceActiveADRoute: {
        size_t nlri_size = kSourceActiveADRouteSize;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t rd_offset = 0;
        copy(rd_.GetData(), rd_.GetData() + kRdSize,
            proto_prefix->prefix.begin() + rd_offset);
        size_t source_offset = rd_offset + kRdSize;
        proto_prefix->prefix[source_offset++] = kIp4AddrBitSize;
        const Ip4Address::bytes_type &source_bytes = source_.to_bytes();
        copy(source_bytes.begin(), source_bytes.begin() + kIp4AddrSize,
            proto_prefix->prefix.begin() + source_offset);

        size_t group_offset = source_offset + kIp4AddrSize;
        proto_prefix->prefix[group_offset++] = kIp4AddrBitSize;
        const Ip4Address::bytes_type &group_bytes = group_.to_bytes();
        copy(group_bytes.begin(), group_bytes.begin() + kIp4AddrSize,
            proto_prefix->prefix.begin() + group_offset);
        break;
    }
    case SourceTreeJoinRoute:
    case SharedTreeJoinRoute: {
        size_t nlri_size = kSourceTreeJoinRouteSize;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t rd_offset = 0;
        copy(rd_.GetData(), rd_.GetData() + kRdSize,
            proto_prefix->prefix.begin() + rd_offset);
        size_t asn_offset = rd_offset + kRdSize;
        put_value(&proto_prefix->prefix[asn_offset], kAsnSize, asn_);
        size_t source_offset = asn_offset + kAsnSize;
        proto_prefix->prefix[source_offset++] = kIp4AddrBitSize;
        const Ip4Address::bytes_type &source_bytes = source_.to_bytes();
        copy(source_bytes.begin(), source_bytes.begin() + kIp4AddrSize,
            proto_prefix->prefix.begin() + source_offset);

        size_t group_offset = source_offset + kIp4AddrSize;
        proto_prefix->prefix[group_offset++] = kIp4AddrBitSize;
        const Ip4Address::bytes_type &group_bytes = group_.to_bytes();
        copy(group_bytes.begin(), group_bytes.begin() + kIp4AddrSize,
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

MvpnPrefix MvpnPrefix::FromString(const string &str, error_code *errorp) {
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
        size_t key_size = 0;
        if (src_rt_type == InterASPMSIADRoute) {
            key_size = kPrefixBytes + kInterASPMSIADRouteSize;
            prefix.rt_key_.resize(key_size);
            prefix.rt_key_[0] = InterASPMSIADRoute;
            prefix.rt_key_[1] = kInterASPMSIADRouteSize;
        } else if (src_rt_type == SPMSIADRoute) {
            key_size = kPrefixBytes + kSPMSIADRouteSize;
            prefix.rt_key_.resize(key_size);
            prefix.rt_key_[0] = SPMSIADRoute;
            prefix.rt_key_[1] = kSPMSIADRouteSize;
        }
        size_t key_offset = 2;
        // Look for RD.
        size_t pos3;
        pos2 = pos1 + 1 + pos2;
        if (!GetRDFromString(&prefix, str, pos2, &pos3, errorp))
            return null_prefix;
        copy(prefix.rd_.GetData(), prefix.rd_.GetData() + kRdSize,
                prefix.rt_key_.begin() + key_offset);
        key_offset += kRdSize;
        // check if source ip or asn
        size_t pos4;
        if (src_rt_type == InterASPMSIADRoute) {
            // check for asn
            if (!GetAsnFromString(&prefix, str, pos3, &pos4, errorp))
                return null_prefix;
            size_t asn_size = sizeof(prefix.asn_);
            put_value(&prefix.rt_key_[key_offset], asn_size, prefix.asn_);
            break;
        }
        if (!GetSourceFromString(&prefix, str, pos3, &pos4, errorp))
            return null_prefix;
        const Ip4Address::bytes_type &source_bytes = prefix.source_.to_bytes();
        prefix.rt_key_[key_offset++] = kIp4AddrBitSize;
        copy(source_bytes.begin(), source_bytes.begin() +
                    kIp4AddrSize, prefix.rt_key_.begin() + key_offset);
        key_offset += kIp4AddrSize;

        // Look for group.
        size_t pos5;
        if (!GetGroupFromString(&prefix, str, pos4, &pos5, errorp))
            return null_prefix;
        const Ip4Address::bytes_type &group_bytes = prefix.group_.to_bytes();
        prefix.rt_key_[key_offset++] = kIp4AddrBitSize;
        copy(group_bytes.begin(), group_bytes.begin() +
                    kIp4AddrSize, prefix.rt_key_.begin() + key_offset);
        key_offset += kIp4AddrSize;

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
                    kIp4AddrSize, prefix.rt_key_.begin() + key_offset);
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
            size_t key_offset = kPrefixBytes;
            RouteDistinguisher rd(&rt_key_[key_offset]);
            uint8_t rt_type = rt_key_[0];
            if (rt_type == SPMSIADRoute) {
                repr += "-3";
                repr += "-" + rd.ToString();
                key_offset += kRdSize + 1;
                Ip4Address ip = Ip4Address(get_value
                                     (&rt_key_[key_offset], kIp4AddrSize));
                repr += "," + ip.to_string();
                key_offset += kIp4AddrSize + 1;
                ip = Ip4Address(get_value (&rt_key_[key_offset], kIp4AddrSize));
                key_offset += kIp4AddrSize;
                repr += "," + ip.to_string();
                ip = Ip4Address(get_value (&rt_key_[key_offset], kIp4AddrSize));
                repr += "," + ip.to_string();
            } else if (rt_type == InterASPMSIADRoute) {
                repr += "-2";
                repr += "-" + rd.ToString();
                key_offset += kRdSize;
                uint32_t asn = get_value(&rt_key_[key_offset], kAsnSize);
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
void MvpnPrefix::SetLeafADPrefixFromSPMSIPrefix(const MvpnPrefix &prefix) {
    assert(prefix.type() == SPMSIADRoute);

    size_t key_size = kPrefixBytes + kSPMSIADRouteSize;
    rt_key_.resize(key_size);
    size_t key_offset = 0;
    rt_key_[key_offset++] = SPMSIADRoute;
    rt_key_[key_offset++] = (uint8_t)kSPMSIADRouteSize;
    copy(prefix.route_distinguisher().GetData(),
            prefix.route_distinguisher().GetData() + kRdSize,
            rt_key_.begin() + key_offset);
    size_t source_offset = key_offset+ kRdSize;
    rt_key_[source_offset++] = kIp4AddrBitSize;
    RouteDistinguisher rd(prefix.route_distinguisher().GetData());
    rd_ = rd;
    const Ip4Address::bytes_type &source_bytes = prefix.source().to_bytes();
    copy(source_bytes.begin(), source_bytes.begin() + kIp4AddrSize,
                                           rt_key_.begin() + source_offset);
    size_t group_offset = source_offset + kIp4AddrSize;
    source_ = Ip4Address(prefix.source().to_ulong());
    rt_key_[group_offset++] = kIp4AddrBitSize;
    const Ip4Address::bytes_type &group_bytes = prefix.group().to_bytes();
    copy(group_bytes.begin(), group_bytes.begin() +
                kIp4AddrSize, rt_key_.begin() + group_offset);
    size_t originator_offset = group_offset + kIp4AddrSize;
    group_ = Ip4Address(prefix.group().to_ulong());
    const Ip4Address::bytes_type &originator_bytes =
            prefix.originator().to_bytes();
    copy(originator_bytes.begin(), originator_bytes.begin() +
                kIp4AddrSize, rt_key_.begin() + originator_offset);
}

void MvpnPrefix::SetSPMSIPrefixFromLeafADPrefix(const MvpnPrefix &prefix) {
    assert(prefix.type() == LeafADRoute);
    RouteDistinguisher rd(prefix.route_distinguisher().GetData());
    rd_ = rd;
    source_ = Ip4Address(prefix.source().to_ulong());
    group_ = Ip4Address(prefix.group().to_ulong());
    originator_ = prefix.GetType3OriginatorFromType4Route();
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

const string MvpnPrefix::GetType() const {
    switch (type_) {
        case Unspecified:
            return "Unspecified";
        case IntraASPMSIADRoute:
            return "IntraASPMSIADRoute";
        case InterASPMSIADRoute:
            return "InterASPMSIADRoute";
        case SPMSIADRoute:
            return "SPMSIADRoute";
        case LeafADRoute:
            return "LeafADRoute";
        case SourceActiveADRoute:
            return "SourceActiveADRoute";
        case SharedTreeJoinRoute:
            return "SharedTreeJoinRoute";
        case SourceTreeJoinRoute:
            return "SourceTreeJoinRoute";
    }
    return "";
}

const string MvpnRoute::GetType() const {
    return GetPrefix().GetType();
}
