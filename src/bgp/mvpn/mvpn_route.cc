/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/mvpn/mvpn_route.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/string_util.h"
#include "bgp/mvpn/mvpn_table.h"

using std::copy;
using std::string;
using std::vector;

MvpnPrefix::MvpnPrefix() : type_(MvpnPrefix::Unspecified) {
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
        prefix->originator_ = Ip4Address(get_value
                (&proto_prefix.prefix[originator_offset], Address::kMaxV4Bytes));
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
        prefix->source_ = Ip4Address(
            get_value(&proto_prefix.prefix[source_offset], Address::kMaxV4Bytes));

        size_t group_offset = source_offset + Address::kMaxV4Bytes + 1;
        if (proto_prefix.prefix[group_offset - 1] != Address::kMaxV4PrefixLen)
            return -1;
        prefix->group_ = Ip4Address(
            get_value(&proto_prefix.prefix[group_offset], Address::kMaxV4Bytes));
        size_t originator_offset = group_offset + Address::kMaxV4Bytes;
        prefix->originator_ = Ip4Address(get_value
                (&proto_prefix.prefix[originator_offset], Address::kMaxV4Bytes));
        break;
    }
    case LeafADRoute: {
        if (nlri_size <= Address::kMaxV4Bytes)
            return -1;
        size_t key_offset = 0;
        size_t originator_offset = nlri_size - Address::kMaxV4Bytes;
        copy(proto_prefix.prefix.begin(), proto_prefix.prefix.begin() +
                originator_offset, prefix->rt_key_.begin() + key_offset);
        prefix->originator_ = Ip4Address(get_value
                (&proto_prefix.prefix[originator_offset], Address::kMaxV4Bytes));
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
        prefix->source_ = Ip4Address(
            get_value(&proto_prefix.prefix[source_offset], Address::kMaxV4Bytes));

        size_t group_offset = source_offset + Address::kMaxV4Bytes + 1;
        if (proto_prefix.prefix[group_offset - 1] != Address::kMaxV4PrefixLen)
            return -1;
        prefix->group_ = Ip4Address(
            get_value(&proto_prefix.prefix[group_offset], Address::kMaxV4Bytes));
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
        size_t source_offset = asn_offset + asn_size;
        if (proto_prefix.prefix[source_offset - 1] != Address::kMaxV4PrefixLen)
            return -1;
        prefix->source_ = Ip4Address(
            get_value(&proto_prefix.prefix[source_offset], Address::kMaxV4Bytes));

        size_t group_offset = source_offset + Address::kMaxV4Bytes + 1;
        if (proto_prefix.prefix[group_offset - 1] != Address::kMaxV4PrefixLen)
            return -1;
        prefix->group_ = Ip4Address(
            get_value(&proto_prefix.prefix[group_offset], Address::kMaxV4Bytes));
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
        size_t keySize = sizeof(rt_key_);
        size_t nlri_size = keySize + Address::kMaxV4Bytes;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t key_offset = 0;
        copy(rt_key_.begin(), rt_key_.begin() + keySize,
                proto_prefix->prefix.begin() + key_offset);

        size_t originator_offset = key_offset + keySize;
        const Ip4Address::bytes_type &originator_bytes = originator_.to_bytes();
        copy(originator_bytes.begin(), originator_bytes.begin() +
                Address::kMaxV4Bytes, proto_prefix->prefix.begin() +
                originator_offset);
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

MvpnPrefix MvpnPrefix::FromString(const string &str,
    boost::system::error_code *errorp) {
    MvpnPrefix prefix, null_prefix;
    string temp_str;

    // Look for Type.
    size_t pos1 = str.find('-');
    if (pos1 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return null_prefix;
    }
    temp_str = str.substr(0, pos1);
    stringToInteger(temp_str, prefix.type_);
    if (!IsValid(prefix.type_)) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return null_prefix;
    }

    switch (prefix.type_) {
    case IntraASPMSIADRoute: {
        // Look for RD.
        size_t pos2 = str.find(',', pos1 + 1);
        if (pos2 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos1 + 1, pos2 - pos1 - 1);
        boost::system::error_code rd_err;
        prefix.rd_ = RouteDistinguisher::FromString(temp_str, &rd_err);
        if (rd_err != 0) {
            if (errorp != NULL) {
                *errorp = rd_err;
            }
            return null_prefix;
        }
        // rest is originator
        temp_str = str.substr(pos2 + 1, string::npos);
        boost::system::error_code originator_err;
        prefix.originator_ = Ip4Address::from_string(temp_str, originator_err);
        if (originator_err != 0) {
            if (errorp != NULL) {
                *errorp = originator_err;
            }
            return null_prefix;
        }
        break;
    }
    case InterASPMSIADRoute: {
        // Look for RD.
        size_t pos2 = str.find(',', pos1 + 1);
        if (pos2 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos1 + 1, pos2 - pos1 - 1);
        boost::system::error_code rd_err;
        prefix.rd_ = RouteDistinguisher::FromString(temp_str, &rd_err);
        if (rd_err != 0) {
            if (errorp != NULL) {
                *errorp = rd_err;
            }
            return null_prefix;
        }
        // rest is asn
        temp_str = str.substr(pos2 + 1, string::npos);
        if (!stringToInteger(temp_str, prefix.asn_)) {
            return null_prefix;
        }
        break;
    }
    case SPMSIADRoute: {
        // Look for RD.
        size_t pos2 = str.find(',', pos1 + 1);
        if (pos2 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos1 + 1, pos2 - pos1 - 1);
        boost::system::error_code rd_err;
        prefix.rd_ = RouteDistinguisher::FromString(temp_str, &rd_err);
        if (rd_err != 0) {
            if (errorp != NULL) {
                *errorp = rd_err;
            }
            return null_prefix;
        }
        // Look for source.
        size_t pos3 = str.find(',', pos2 + 1);
        if (pos3 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos2 + 1, pos3 - pos2 - 1);
        boost::system::error_code source_err;
        prefix.source_ = Ip4Address::from_string(temp_str, source_err);
        if (source_err != 0) {
            if (errorp != NULL) {
                *errorp = source_err;
            }
            return null_prefix;
        }

        // Look for group.
        size_t pos4 = str.find(',', pos3 + 1);
        if (pos4 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos3 + 1, pos4 - pos3 - 1);
        boost::system::error_code group_err;
        prefix.group_ = Ip4Address::from_string(temp_str, group_err);
        if (group_err != 0) {
            if (errorp != NULL) {
                *errorp = group_err;
            }
            return null_prefix;
        }

        // rest is originator
        temp_str = str.substr(pos4 + 1, string::npos);
        boost::system::error_code originator_err;
        prefix.originator_ = Ip4Address::from_string(temp_str, originator_err);
        if (originator_err != 0) {
            if (errorp != NULL) {
                *errorp = originator_err;
            }
            return null_prefix;
        }
        break;
    }
    case LeafADRoute: {
        // First get the originator from the end
        size_t pos_last = str.find_last_of(',');
        if (pos_last == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos_last + 1, string::npos);
        boost::system::error_code originator_err;
        prefix.originator_ = Ip4Address::from_string(temp_str, originator_err);
        if (originator_err != 0) {
            if (errorp != NULL) {
                *errorp = originator_err;
            }
            return null_prefix;
        }
        // Look for type.
        size_t pos2 = str.find('-', pos1 + 1);
        if (pos2 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos1 + 1, pos2 - pos1 - 1);
        uint8_t src_rt_type;
        stringToInteger(temp_str, src_rt_type);
        size_t rd_size = RouteDistinguisher::kSize;
        size_t total_key_size = 0;
        if (src_rt_type == MvpnPrefix::InterASPMSIADRoute) {
            total_key_size = 3 + rd_size + sizeof(prefix.asn_);
        } else if (src_rt_type == MvpnPrefix::SPMSIADRoute) {
            total_key_size = 5 + rd_size + Address::kMaxV4Bytes * 3;
        }
        size_t key_size = 0;
        prefix.rt_key_.resize(total_key_size);
        prefix.rt_key_[key_size] = temp_str[0];
        prefix.rt_key_[key_size + 1] = '-';
        key_size += 2;
        // Look for RD.
        size_t pos3 = str.find(',', pos2 + 1);
        if (pos3 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos2 + 1, pos3 - pos2 - 1);
        boost::system::error_code rd_err;
        prefix.rd_ = RouteDistinguisher::FromString(temp_str, &rd_err);
        if (rd_err != 0) {
            if (errorp != NULL) {
                *errorp = rd_err;
            }
            return null_prefix;
        }
        copy(prefix.rd_.GetData(), prefix.rd_.GetData() + rd_size,
                prefix.rt_key_.begin() + key_size);
        key_size += rd_size;
        prefix.rt_key_[key_size] = ',';
        key_size += 1;
        // check if source ip or asn
        size_t pos4 = str.find(',', pos3 + 1);
        if (pos4 == pos_last) {
            if(src_rt_type == MvpnPrefix::InterASPMSIADRoute) {
                // check for asn
                temp_str = str.substr(pos3 + 1, pos4 - pos3 - 1);
                if (!stringToInteger(temp_str, prefix.asn_)) {
                    if (errorp != NULL) {
                        *errorp = make_error_code(
                                boost::system::errc::invalid_argument);
                    }
                    return null_prefix;
                }
                size_t asn_size = sizeof(prefix.asn_);
                put_value(&prefix.rt_key_[key_size], asn_size, prefix.asn_);
                break;
            } else {
                if (errorp != NULL) {
                    *errorp = make_error_code(boost::system::errc::invalid_argument);
                }
                return null_prefix;
            }
        }
        temp_str = str.substr(pos3 + 1, pos4 - pos3 - 1);
        boost::system::error_code source_err;
        prefix.source_ = Ip4Address::from_string(temp_str, source_err);
        if (source_err != 0) {
            if (errorp != NULL) {
                *errorp = source_err;
            }
            return null_prefix;
        }
        const Ip4Address::bytes_type &source_bytes = prefix.source_.to_bytes();
        copy(source_bytes.begin(), source_bytes.begin() +
                    Address::kMaxV4Bytes, prefix.rt_key_.begin() + key_size);
        key_size += Address::kMaxV4Bytes;
        prefix.rt_key_[key_size] = ',';
        key_size += 1;

        // Look for group.
        size_t pos5 = str.find(',', pos4 + 1);
        if (pos5 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos4 + 1, pos5 - pos4 - 1);
        boost::system::error_code group_err;
        prefix.group_ = Ip4Address::from_string(temp_str, group_err);
        if (group_err != 0) {
            if (errorp != NULL) {
                *errorp = group_err;
            }
            return null_prefix;
        }
        const Ip4Address::bytes_type &group_bytes = prefix.group_.to_bytes();
        copy(group_bytes.begin(), group_bytes.begin() +
                    Address::kMaxV4Bytes, prefix.rt_key_.begin() + key_size);
        key_size += Address::kMaxV4Bytes;
        prefix.rt_key_[key_size] = ',';
        key_size += 1;

        // Look for originator of rt_key and ignore it.
        size_t pos6 = str.find(',', pos5 + 1);
        if (pos6 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }

        // rest is originator
        temp_str = str.substr(pos5 + 1, pos6 - pos5 - 1);
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
        size_t pos2 = str.find(',', pos1 + 1);
        if (pos2 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos1 + 1, pos2 - pos1 - 1);
        boost::system::error_code rd_err;
        prefix.rd_ = RouteDistinguisher::FromString(temp_str, &rd_err);
        if (rd_err != 0) {
            if (errorp != NULL) {
                *errorp = rd_err;
            }
            return null_prefix;
        }
        // Look for source.
        size_t pos3 = str.find(',', pos2 + 1);
        if (pos3 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos2 + 1, pos3 - pos2 - 1);
        boost::system::error_code source_err;
        prefix.source_ = Ip4Address::from_string(temp_str, source_err);
        if (source_err != 0) {
            if (errorp != NULL) {
                *errorp = source_err;
            }
            return null_prefix;
        }

        // rest is group.
        temp_str = str.substr(pos3 + 1, string::npos);
        boost::system::error_code group_err;
        prefix.group_ = Ip4Address::from_string(temp_str, group_err);
        if (group_err != 0) {
            if (errorp != NULL) {
                *errorp = group_err;
            }
            return null_prefix;
        }
        break;
    }
    case SharedTreeJoinRoute:
    case SourceTreeJoinRoute: {
        // Look for RD.
        size_t pos2 = str.find(',', pos1 + 1);
        if (pos2 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos1 + 1, pos2 - pos1 - 1);
        boost::system::error_code rd_err;
        prefix.rd_ = RouteDistinguisher::FromString(temp_str, &rd_err);
        if (rd_err != 0) {
            if (errorp != NULL) {
                *errorp = rd_err;
            }
            return null_prefix;
        }
        // Look for asn
        size_t pos3 = str.find(',', pos2 + 1);
        if (pos3 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos2 + 1, pos3 - pos2 - 1);
        if (!stringToInteger(temp_str, prefix.asn_)) {
            return null_prefix;
        }
        // Look for source.
        size_t pos4 = str.find(',', pos3 + 1);
        if (pos4 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return null_prefix;
        }
        temp_str = str.substr(pos3 + 1, pos4 - pos3 - 1);
        boost::system::error_code source_err;
        prefix.source_ = Ip4Address::from_string(temp_str, source_err);
        if (source_err != 0) {
            if (errorp != NULL) {
                *errorp = source_err;
            }
            return null_prefix;
        }

        // rest is group.
        temp_str = str.substr(pos4 + 1, string::npos);
        boost::system::error_code group_err;
        prefix.group_ = Ip4Address::from_string(temp_str, group_err);
        if (group_err != 0) {
            if (errorp != NULL) {
                *errorp = group_err;
            }
            return null_prefix;
        }
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
            RouteDistinguisher rd(&rt_key_[2]);
            char rt_type = rt_key_[0];
            if (rt_type == integerToString(MvpnPrefix::SPMSIADRoute)[0]) {
                repr += "-3";
                repr += "-" + rd.ToString();
                size_t key_size = RouteDistinguisher::kSize + 3;
                Ip4Address ip = Ip4Address(get_value
                    (&rt_key_[key_size], Address::kMaxV4Bytes));
                repr += "," + ip.to_string();
                key_size += Address::kMaxV4Bytes + 1;
                ip = Ip4Address(get_value
                    (&rt_key_[key_size], Address::kMaxV4Bytes));
                key_size += Address::kMaxV4Bytes + 1;
                repr += "," + ip.to_string();
                ip = Ip4Address(get_value
                    (&rt_key_[key_size], Address::kMaxV4Bytes));
                repr += "," + ip.to_string();
            } else if (rt_type == integerToString(
                        MvpnPrefix::InterASPMSIADRoute)[0]) {
                repr += "-2";
                repr += "-" + rd.ToString();
                size_t key_size = RouteDistinguisher::kSize + 3;
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
void MvpnPrefix::SetRtKeyFromSPMSIADRoute(const MvpnPrefix prefix) {
    if (prefix.type() == SPMSIADRoute) {
        size_t rd_size = RouteDistinguisher::kSize;
        size_t key_size = 0;
        size_t total_key_size = 5 + rd_size +
                Address::kMaxV4Bytes * 2 + Address::kMaxV4Bytes;
        rt_key_.resize(total_key_size);
        rt_key_[0] = integerToString(SPMSIADRoute)[0];
        rt_key_[1] = '-';
        key_size = 2;
        copy(prefix.route_distinguisher().GetData(),
                prefix.route_distinguisher().GetData() + rd_size,
                rt_key_.begin() + key_size);
        RouteDistinguisher rd(prefix.route_distinguisher().GetData());
        rd_ = rd;
        key_size += rd_size;
        rt_key_[key_size] = ',';
        key_size += 1;
        const Ip4Address::bytes_type &source_bytes =
                prefix.source().to_bytes();
        copy(source_bytes.begin(), source_bytes.begin() +
                    Address::kMaxV4Bytes, rt_key_.begin() + key_size);
        Ip4Address source(prefix.source().to_ulong());
        source_ = source;
        key_size += Address::kMaxV4Bytes;
        rt_key_[key_size] = ',';
        key_size += 1;
        const Ip4Address::bytes_type &group_bytes = prefix.group().to_bytes();
        copy(group_bytes.begin(), group_bytes.begin() +
                    Address::kMaxV4Bytes, rt_key_.begin() + key_size);
        Ip4Address group(prefix.group().to_ulong());
        group_ = group;
        key_size += Address::kMaxV4Bytes;
        rt_key_[key_size] = ',';
        key_size += 1;
        const Ip4Address::bytes_type &originator_bytes =
                prefix.originator().to_bytes();
        copy(originator_bytes.begin(), originator_bytes.begin() +
                    Address::kMaxV4Bytes, rt_key_.begin() + key_size);
        type_ = LeafADRoute;
    }
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
