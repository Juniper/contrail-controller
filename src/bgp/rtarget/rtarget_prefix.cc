/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/rtarget/rtarget_prefix.h"


using boost::system::error_code;
using std::copy;
using std::string;

RTargetPrefix::RTargetPrefix() : as_(0), rtarget_(RouteTarget::null_rtarget) {
}

int RTargetPrefix::FromProtoPrefix(const BgpProtoPrefix &proto_prefix,
                                   RTargetPrefix *prefix) {
    size_t nlri_size = proto_prefix.prefix.size();
    if (nlri_size == 0) {
        prefix->as_ = 0;
        prefix->rtarget_ =  RouteTarget::null_rtarget;
        return 0;
    }

    size_t expected_nlri_size = sizeof(as4_t) + RouteTarget::kSize;
    if (nlri_size != expected_nlri_size)
        return -1;

    size_t as_offset = 0;
    prefix->as_ = get_value(&proto_prefix.prefix[as_offset], sizeof(as4_t));
    size_t rtarget_offset = as_offset + sizeof(as4_t);
    RouteTarget::bytes_type bt = { { 0 } };
    copy(proto_prefix.prefix.begin() + rtarget_offset,
        proto_prefix.prefix.end(), bt.begin());
    prefix->rtarget_ = RouteTarget(bt);

    return 0;
}

void RTargetPrefix::BuildProtoPrefix(BgpProtoPrefix *proto_prefix) const {
    proto_prefix->prefix.clear();
    if (as_ == 0 && rtarget_ == RouteTarget::null_rtarget) {
        proto_prefix->prefixlen = 0;
        return;
    }

    size_t nlri_size = sizeof(as4_t) + RouteTarget::kSize;
    proto_prefix->prefix.resize(nlri_size);
    proto_prefix->prefixlen = nlri_size * 8;
    size_t as_offset = 0;
    put_value(&proto_prefix->prefix[as_offset], sizeof(as4_t), as_);
    size_t rtarget_offset = as_offset + sizeof(as4_t);
    put_value(&proto_prefix->prefix[rtarget_offset], RouteTarget::kSize,
        rtarget_.GetExtCommunityValue());
}

// as:rtarget
RTargetPrefix RTargetPrefix::FromString(const string &str, error_code *errorp) {
    RTargetPrefix prefix;

    size_t pos = str.find(':');
    if (pos == string::npos) {
        if (errorp != NULL)
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        return prefix;
    }

    as4_t as;
    string asstr = str.substr(0, pos);
    stringToInteger(asstr, as);

    string rtargetstr(str, pos + 1);
    error_code rtarget_err;
    RouteTarget rtarget;
    rtarget = RouteTarget::FromString(rtargetstr, &rtarget_err);
    if (rtarget_err != 0) {
        if (errorp != NULL)
            *errorp = rtarget_err;
        return prefix;
    }

    prefix.rtarget_ = rtarget;
    prefix.as_ = as;
    return prefix;
}

string RTargetPrefix::ToString() const {
    return (integerToString(as_) + ":" + rtarget_.ToString());
}

int RTargetPrefix::CompareTo(const RTargetPrefix &rhs) const {
    if (as_ < rhs.as_) {
        return -1;
    }
    if (as_ > rhs.as_) {
        return 1;
    }
    if (rtarget_ < rhs.rtarget_) {
        return -1;
    }
    if (rtarget_ > rhs.rtarget_) {
        return 1;
    }
    return 0;
}

